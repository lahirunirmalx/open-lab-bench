/**
 * Generic SCPI PSU driver — one impl, multiple profiles.
 *
 * Models that put the channel inside the command (Siglent SPD: "CH1:VOLT 5.0")
 * use channel_in_command=true and format strings carrying both %d (channel)
 * and %.3f (value).
 *
 * Models that use a separate channel-select step (Keysight E3631A:
 * "INST:NSEL 1" then "VOLT 5.0") use channel_in_command=false; the driver
 * sends select_channel_fmt first, then commands with no channel placeholder.
 *
 * A reader thread polls each channel for V/A/P every poll_ms; setpoints and
 * output toggles go through immediately. Both share the scpi_t mutex.
 */

#include "scpi_psu.h"

#include "transport/scpi.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_CHANNELS 4

typedef struct {
    const char *model_name;
    int   n_channels;
    float v_max[MAX_CHANNELS];
    float i_max[MAX_CHANNELS];

    bool        channel_in_command;
    const char *select_channel_fmt;   /* used when channel_in_command == false */

    /* Value-setting commands. With channel_in_command, contains "%d" and "%.3f".
     * Without, contains only "%.3f"; the channel is implicit via select. */
    const char *set_voltage_fmt;
    const char *set_current_fmt;

    /* Output toggle. With channel_in_command, contains "%d". Without, no specifier. */
    const char *set_output_on_fmt;
    const char *set_output_off_fmt;

    /* Measurement queries. With channel_in_command, contains "%d". Without, no specifier.
     * meas_power_fmt may be NULL → compute as V * A. */
    const char *meas_voltage_fmt;
    const char *meas_current_fmt;
    const char *meas_power_fmt;

    /* Tracking / series/parallel coupling. Both NULL means the driver does
     * not expose set_tracking() and caps.supports_tracking stays false.
     * If non-NULL the strings have no format specifiers — they are sent
     * verbatim (no per-channel state, this is a global command).
     *
     * For Siglent SPD3303 series we send OUTP:TRACK 1 (series tracking ON)
     * and OUTP:TRACK 0 (back to independent). The instrument must already
     * be in its tracking-capable mode (front-panel "Series"/"Parallel"
     * selection on older firmware). */
    const char *set_tracking_on;
    const char *set_tracking_off;
} scpi_psu_profile_t;

/* ---- profile table ---- */

static const scpi_psu_profile_t k_siglent_spd3303 = {
    .model_name = "Siglent SPD3303",
    .n_channels = 2,
    .v_max = {32.0f, 32.0f},
    .i_max = {3.2f,  3.2f},
    .channel_in_command = true,
    .set_voltage_fmt    = "CH%d:VOLT %.3f",
    .set_current_fmt    = "CH%d:CURR %.3f",
    .set_output_on_fmt  = "OUTP CH%d,ON",
    .set_output_off_fmt = "OUTP CH%d,OFF",
    .meas_voltage_fmt   = "MEAS:VOLT? CH%d",
    .meas_current_fmt   = "MEAS:CURR? CH%d",
    .meas_power_fmt     = "MEAS:POWE? CH%d",
    /* OUTP:TRACK <0|1|2> — 0=independent, 1=series, 2=parallel.
     * We toggle series-track on, independent off; user picks parallel via
     * the front panel if needed. Requires the instrument to be in a
     * tracking-capable wiring/mode. */
    .set_tracking_on    = "OUTP:TRACK 1",
    .set_tracking_off   = "OUTP:TRACK 0",
};

static const scpi_psu_profile_t k_keysight_e3631a = {
    .model_name = "Keysight E3631A",
    .n_channels = 3,
    .v_max = {6.0f, 25.0f, 25.0f},   /* P6V, P25V, N25V (absolute) */
    .i_max = {5.0f,  1.0f,  1.0f},
    .channel_in_command = false,
    .select_channel_fmt = "INST:NSEL %d",
    .set_voltage_fmt    = "VOLT %.3f",
    .set_current_fmt    = "CURR %.3f",
    .set_output_on_fmt  = "OUTP ON",
    .set_output_off_fmt = "OUTP OFF",
    .meas_voltage_fmt   = "MEAS:VOLT?",
    .meas_current_fmt   = "MEAS:CURR?",
    .meas_power_fmt     = NULL,
};

static const scpi_psu_profile_t k_keysight_e3633a = {
    .model_name = "Keysight E3633A",
    .n_channels = 1,
    .v_max = {20.0f},   /* high range; low range is 8V */
    .i_max = {10.0f},
    .channel_in_command = false,
    .select_channel_fmt = NULL,
    .set_voltage_fmt    = "VOLT %.3f",
    .set_current_fmt    = "CURR %.3f",
    .set_output_on_fmt  = "OUTP ON",
    .set_output_off_fmt = "OUTP OFF",
    .meas_voltage_fmt   = "MEAS:VOLT?",
    .meas_current_fmt   = "MEAS:CURR?",
    .meas_power_fmt     = NULL,
};

static const scpi_psu_profile_t k_keysight_e3634a = {
    .model_name = "Keysight E3634A",
    .n_channels = 1,
    .v_max = {25.0f},
    .i_max = {7.0f},
    .channel_in_command = false,
    .select_channel_fmt = NULL,
    .set_voltage_fmt    = "VOLT %.3f",
    .set_current_fmt    = "CURR %.3f",
    .set_output_on_fmt  = "OUTP ON",
    .set_output_off_fmt = "OUTP OFF",
    .meas_voltage_fmt   = "MEAS:VOLT?",
    .meas_current_fmt   = "MEAS:CURR?",
    .meas_power_fmt     = NULL,
};

static const scpi_psu_profile_t k_keysight_e3645a = {
    .model_name = "Keysight E3645A",
    .n_channels = 1,
    .v_max = {20.0f},
    .i_max = {5.0f},
    .channel_in_command = false,
    .select_channel_fmt = NULL,
    .set_voltage_fmt    = "VOLT %.3f",
    .set_current_fmt    = "CURR %.3f",
    .set_output_on_fmt  = "OUTP ON",
    .set_output_off_fmt = "OUTP OFF",
    .meas_voltage_fmt   = "MEAS:VOLT?",
    .meas_current_fmt   = "MEAS:CURR?",
    .meas_power_fmt     = NULL,
};

/* ---- driver state ---- */

#define DEFAULT_POLL_MS 250
#define QUERY_TIMEOUT_MS 600

typedef struct {
    scpi_t *scpi;
    const scpi_psu_profile_t *prof;

    pthread_t reader;
    volatile bool running;

    pthread_mutex_t state_lock;
    psu_channel_state_t state[MAX_CHANNELS];

    /* Cached setpoint / output values — instruments often don't report these
     * back symmetrically, so we trust local last-set. */
    float set_v[MAX_CHANNELS];
    float set_a[MAX_CHANNELS];
    bool  out_on[MAX_CHANNELS];

    /* For the channel-select-first style, remember the last-selected channel
     * to skip redundant INST commands. */
    int last_selected_ch;

    volatile bool connected;
    volatile uint32_t err_count;
    volatile uint32_t rx_count;
} scpi_psu_state_t;

static scpi_psu_state_t *st_of(psu_driver_t *d) { return (scpi_psu_state_t *)d->state; }

static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000u + tv.tv_usec / 1000u;
}

/* Send a command optionally prefixed by a channel-select. With
 * channel_in_command, the channel is baked into the command and select is
 * skipped. */
static bool send_channel_cmd(scpi_psu_state_t *s, int ch, const char *fmt_or_value_cmd,
                             bool has_value, double value) {
    char buf[160];
    if (s->prof->channel_in_command) {
        if (has_value) snprintf(buf, sizeof(buf), fmt_or_value_cmd, ch, value);
        else           snprintf(buf, sizeof(buf), fmt_or_value_cmd, ch);
    } else {
        if (s->prof->select_channel_fmt && ch != s->last_selected_ch) {
            char sel[64];
            snprintf(sel, sizeof(sel), s->prof->select_channel_fmt, ch);
            if (!scpi_send(s->scpi, sel)) {
                s->err_count++;
                return false;
            }
            s->last_selected_ch = ch;
        }
        if (has_value) snprintf(buf, sizeof(buf), fmt_or_value_cmd, value);
        else           snprintf(buf, sizeof(buf), "%s", fmt_or_value_cmd);
    }
    bool ok = scpi_send(s->scpi, buf);
    if (!ok) s->err_count++;
    return ok;
}

static bool query_float(scpi_psu_state_t *s, int ch, const char *fmt, float *out) {
    char cmd[160];
    char resp[160];

    if (s->prof->channel_in_command) {
        snprintf(cmd, sizeof(cmd), fmt, ch);
    } else {
        if (s->prof->select_channel_fmt && ch != s->last_selected_ch) {
            char sel[64];
            snprintf(sel, sizeof(sel), s->prof->select_channel_fmt, ch);
            if (!scpi_send(s->scpi, sel)) { s->err_count++; return false; }
            s->last_selected_ch = ch;
        }
        snprintf(cmd, sizeof(cmd), "%s", fmt);
    }

    if (!scpi_query(s->scpi, cmd, resp, sizeof(resp), QUERY_TIMEOUT_MS)) {
        s->err_count++;
        return false;
    }
    s->rx_count++;
    *out = (float)atof(resp);
    return true;
}

/* ---- reader thread ---- */

static void *reader_main(void *arg) {
    psu_driver_t *d = (psu_driver_t *)arg;
    scpi_psu_state_t *s = st_of(d);

    /* Identify the instrument (informational). */
    char idn[160] = {0};
    if (scpi_query(s->scpi, "*IDN?", idn, sizeof(idn), 800)) {
        fprintf(stderr, "scpi-psu: %s says: %s", s->prof->model_name, idn);
        if (strchr(idn, '\n') == NULL) fputc('\n', stderr);
        s->connected = true;
        s->rx_count++;
    } else {
        fprintf(stderr, "scpi-psu: *IDN? timed out — continuing anyway\n");
        s->err_count++;
    }

    while (s->running) {
        for (int ch = 1; ch <= s->prof->n_channels && s->running; ch++) {
            float v = 0, a = 0, p = 0;
            bool ok_v = query_float(s, ch, s->prof->meas_voltage_fmt, &v);
            bool ok_a = query_float(s, ch, s->prof->meas_current_fmt, &a);
            bool ok_p = false;
            if (s->prof->meas_power_fmt)
                ok_p = query_float(s, ch, s->prof->meas_power_fmt, &p);
            if (!ok_p) p = v * a;

            pthread_mutex_lock(&s->state_lock);
            psu_channel_state_t *cs = &s->state[ch - 1];
            cs->set_v = s->set_v[ch - 1];
            cs->set_a = s->set_a[ch - 1];
            if (ok_v) cs->out_v = v;
            if (ok_a) cs->out_a = a;
            cs->out_p = p;
            cs->out_on = s->out_on[ch - 1];
            /* Heuristic CV/CC: in CC the output current is close to the
             * setpoint and the output voltage has dropped below the setting. */
            cs->cv_mode = (s->out_on[ch - 1] && cs->out_v < cs->set_v - 0.05f
                           && cs->out_a >= cs->set_a - 0.01f) ? false : true;
            cs->valid = (ok_v && ok_a);
            if (cs->valid) {
                cs->timestamp_ms = now_ms();
                s->connected = true;
            }
            pthread_mutex_unlock(&s->state_lock);
        }
        /* Coarse poll; SCPI/GPIB instruments don't like being hammered. */
        for (int i = 0; i < DEFAULT_POLL_MS / 20 && s->running; i++)
            usleep(20 * 1000);
    }
    return NULL;
}

/* ---- vtable ---- */

static void v_close(psu_driver_t *self) {
    if (!self) return;
    scpi_psu_state_t *s = st_of(self);
    if (s) {
        s->running = false;
        pthread_join(s->reader, NULL);
        if (s->scpi) scpi_close(s->scpi);
        pthread_mutex_destroy(&s->state_lock);
        free(s);
    }
    free(self);
}

static bool v_is_connected(psu_driver_t *self) { return st_of(self)->connected; }

static void v_get_channel(psu_driver_t *self, int ch, psu_channel_state_t *out) {
    scpi_psu_state_t *s = st_of(self);
    if (ch < 1 || ch > s->prof->n_channels) { memset(out, 0, sizeof(*out)); return; }
    pthread_mutex_lock(&s->state_lock);
    *out = s->state[ch - 1];
    pthread_mutex_unlock(&s->state_lock);
}

static bool v_set_voltage(psu_driver_t *self, int ch, float v) {
    scpi_psu_state_t *s = st_of(self);
    if (ch < 1 || ch > s->prof->n_channels) return false;
    if (v < 0 || v > s->prof->v_max[ch - 1]) return false;
    pthread_mutex_lock(&s->state_lock); s->set_v[ch - 1] = v; pthread_mutex_unlock(&s->state_lock);
    return send_channel_cmd(s, ch, s->prof->set_voltage_fmt, true, (double)v);
}

static bool v_set_current(psu_driver_t *self, int ch, float a) {
    scpi_psu_state_t *s = st_of(self);
    if (ch < 1 || ch > s->prof->n_channels) return false;
    if (a < 0 || a > s->prof->i_max[ch - 1]) return false;
    pthread_mutex_lock(&s->state_lock); s->set_a[ch - 1] = a; pthread_mutex_unlock(&s->state_lock);
    return send_channel_cmd(s, ch, s->prof->set_current_fmt, true, (double)a);
}

static bool v_set_output(psu_driver_t *self, int ch, bool on) {
    scpi_psu_state_t *s = st_of(self);
    if (ch < 1 || ch > s->prof->n_channels) return false;
    pthread_mutex_lock(&s->state_lock); s->out_on[ch - 1] = on; pthread_mutex_unlock(&s->state_lock);
    const char *fmt = on ? s->prof->set_output_on_fmt : s->prof->set_output_off_fmt;
    return send_channel_cmd(s, ch, fmt, false, 0.0);
}

/* Global tracking toggle (no channel argument). Only wired into the vtable
 * when the profile defines both commands. */
static bool v_set_tracking(psu_driver_t *self, bool on) {
    scpi_psu_state_t *s = st_of(self);
    const char *cmd = on ? s->prof->set_tracking_on : s->prof->set_tracking_off;
    if (!cmd) return false;
    bool ok = scpi_send(s->scpi, cmd);
    if (!ok) s->err_count++;
    return ok;
}

static void v_get_stats(psu_driver_t *self, uint32_t *rx, uint32_t *err) {
    scpi_psu_state_t *s = st_of(self);
    if (rx)  *rx  = s->rx_count;
    if (err) *err = s->err_count;
}

/* ---- factory shared open() ---- */

static psu_driver_t *scpi_psu_open(const scpi_psu_profile_t *prof,
                                   const char *port_spec,
                                   int default_baud) {
    if (!prof || !port_spec) return NULL;

    scpi_t *scpi = scpi_open(port_spec, default_baud);
    if (!scpi) {
        fprintf(stderr, "scpi-psu: failed to open transport for '%s'\n", port_spec);
        return NULL;
    }

    scpi_psu_state_t *s = calloc(1, sizeof(*s));
    psu_driver_t     *d = calloc(1, sizeof(*d));
    if (!s || !d) { free(s); free(d); scpi_close(scpi); return NULL; }

    s->scpi    = scpi;
    s->prof    = prof;
    s->running = true;
    s->last_selected_ch = -1;
    pthread_mutex_init(&s->state_lock, NULL);

    /* Sensible defaults for the local cache. */
    for (int i = 0; i < prof->n_channels; i++) {
        s->set_v[i] = 0.0f;
        s->set_a[i] = prof->i_max[i] * 0.1f;   /* 10% as a safety default */
        s->out_on[i] = false;
        s->state[i].set_v = s->set_v[i];
        s->state[i].set_a = s->set_a[i];
    }

    d->state = s;
    /* Use the largest per-channel limit as the GUI bound. */
    float v_lim = 0, i_lim = 0;
    for (int i = 0; i < prof->n_channels; i++) {
        if (prof->v_max[i] > v_lim) v_lim = prof->v_max[i];
        if (prof->i_max[i] > i_lim) i_lim = prof->i_max[i];
    }
    bool has_tracking = (prof->set_tracking_on != NULL && prof->set_tracking_off != NULL);
    d->caps = (psu_caps_t){
        .model_name             = prof->model_name,
        .n_channels             = prof->n_channels,
        .v_max                  = v_lim,
        .i_max                  = i_lim,
        .supports_tracking      = has_tracking,
        .supports_mppt          = false,
        .supports_ovp           = false,
        .supports_temperature   = false,
        .supports_input_voltage = false,
        .supports_runtime       = false,
        .supports_energy        = false,
    };
    d->close        = v_close;
    d->is_connected = v_is_connected;
    d->get_channel  = v_get_channel;
    d->set_voltage  = v_set_voltage;
    d->set_current  = v_set_current;
    d->set_output   = v_set_output;
    d->set_tracking = has_tracking ? v_set_tracking : NULL;
    d->get_stats    = v_get_stats;

    if (pthread_create(&s->reader, NULL, reader_main, d) != 0) {
        v_close(d);
        return NULL;
    }
    return d;
}

/* ---- per-model factory functions ---- */

static psu_driver_t *open_siglent_spd3303(const char *p, int b) { return scpi_psu_open(&k_siglent_spd3303, p, b); }
static psu_driver_t *open_keysight_e3631a(const char *p, int b) { return scpi_psu_open(&k_keysight_e3631a, p, b); }
static psu_driver_t *open_keysight_e3633a(const char *p, int b) { return scpi_psu_open(&k_keysight_e3633a, p, b); }
static psu_driver_t *open_keysight_e3634a(const char *p, int b) { return scpi_psu_open(&k_keysight_e3634a, p, b); }
static psu_driver_t *open_keysight_e3645a(const char *p, int b) { return scpi_psu_open(&k_keysight_e3645a, p, b); }

const psu_driver_factory_t siglent_spd_factory = {
    .id              = "siglent-spd",
    .display_name    = "Siglent SPD3303 (SCPI)",
    .description     = "Siglent SPD-series via SCPI; serial: or prologix: transport",
    .default_baud    = 115200,
    .n_channels_hint = 2,
    .open            = open_siglent_spd3303,
};

const psu_driver_factory_t keysight_e3631a_factory = {
    .id              = "keysight-e3631a",
    .display_name    = "Keysight/Agilent/HP E3631A (SCPI)",
    .description     = "Triple-output: +6V/5A, +25V/1A, -25V/1A. Via serial or Prologix GPIB.",
    .default_baud    = 9600,
    .n_channels_hint = 3,
    .open            = open_keysight_e3631a,
};

const psu_driver_factory_t keysight_e3633a_factory = {
    .id              = "keysight-e3633a",
    .display_name    = "Keysight/Agilent/HP E3633A (SCPI)",
    .description     = "Single output, 8V/20A or 20V/10A. Via serial or Prologix GPIB.",
    .default_baud    = 9600,
    .n_channels_hint = 1,
    .open            = open_keysight_e3633a,
};

const psu_driver_factory_t keysight_e3634a_factory = {
    .id              = "keysight-e3634a",
    .display_name    = "Keysight/Agilent/HP E3634A (SCPI)",
    .description     = "Single output, 8V/7A or 25V/4A. Via serial or Prologix GPIB.",
    .default_baud    = 9600,
    .n_channels_hint = 1,
    .open            = open_keysight_e3634a,
};

const psu_driver_factory_t keysight_e3645a_factory = {
    .id              = "keysight-e3645a",
    .display_name    = "Keysight/Agilent/HP E3645A (SCPI)",
    .description     = "Single output, 8V/5A or 20V/2.2A. Via serial or Prologix GPIB.",
    .default_baud    = 9600,
    .n_channels_hint = 1,
    .open            = open_keysight_e3645a,
};
