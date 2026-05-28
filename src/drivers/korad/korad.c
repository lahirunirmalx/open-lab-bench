/**
 * Korad KA-series wire protocol — used by Korad and a long list of Chinese
 * clones. Quirks vs SCPI / Modbus:
 *
 *  - Commands have NO terminator. We write raw bytes.
 *  - Responses also have no terminator; we read until idle (~120 ms).
 *  - A short inter-command delay (~50 ms) keeps cheap clones happy.
 *  - "IDN?" returns a fixed-length string with no separators
 *    (e.g. "KORADKA3005PV2.0").
 *
 * Command set (single output):
 *   *IDN?            identify
 *   VSET1:<v.vv>     set voltage  (2 decimals)
 *   ISET1:<a.aaa>    set current  (3 decimals)
 *   VSET1?           query voltage setpoint
 *   ISET1?           query current setpoint
 *   VOUT1?           query actual output voltage
 *   IOUT1?           query actual output current
 *   OUT1 / OUT0      output enable / disable
 *   STATUS?          1-byte status: bit0=CV(1)/CC(0), bit6=output-on
 */

#include "korad.h"

#include "serial_port.h"

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#define V_MAX            30.0f
#define I_MAX             5.0f
#define POLL_MS          250
#define IDLE_TIMEOUT_MS  120     /* "no more bytes for X ms" → end of response */
#define POST_CMD_DELAY_MS 50

typedef struct {
    serial_port_t  *sp;
    pthread_mutex_t io_lock;     /* serialise raw read/write cycles */

    pthread_t reader;
    volatile bool running;

    pthread_mutex_t state_lock;
    psu_channel_state_t state;

    float set_v_cache;
    float set_a_cache;
    bool  out_on_cache;

    volatile bool     connected;
    volatile uint32_t rx_count;
    volatile uint32_t err_count;
} korad_state_t;

static korad_state_t *st_of(psu_driver_t *d) { return (korad_state_t *)d->state; }

static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000u + tv.tv_usec / 1000u;
}

static void delay_ms(int ms) {
    if (ms <= 0) return;
    usleep((useconds_t)ms * 1000);
}

/* Write the exact bytes of `cmd` (no terminator). */
static bool korad_write_raw(korad_state_t *s, const char *cmd) {
    int fd = serial_get_fd(s->sp);
    if (fd < 0) return false;
    size_t n = strlen(cmd);
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, cmd + off, n - off);
        if (w < 0) {
            s->err_count++;
            return false;
        }
        off += (size_t)w;
    }
    return true;
}

/* Read until IDLE_TIMEOUT_MS of silence. Returns bytes captured. */
static int korad_read_idle(korad_state_t *s, char *out, size_t outlen) {
    int fd = serial_get_fd(s->sp);
    if (fd < 0 || !out || outlen == 0) return -1;
    size_t got = 0;
    while (got + 1 < outlen) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(fd, &rs);
        struct timeval tv = { .tv_sec = 0, .tv_usec = IDLE_TIMEOUT_MS * 1000 };
        int r = select(fd + 1, &rs, NULL, NULL, &tv);
        if (r <= 0) break;            /* timeout or error → done */
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        size_t take = (size_t)n;
        if (take > outlen - 1 - got) take = outlen - 1 - got;
        memcpy(out + got, buf, take);
        got += take;
    }
    out[got] = '\0';
    return (int)got;
}

static bool korad_query(korad_state_t *s, const char *cmd,
                        char *out, size_t outlen) {
    pthread_mutex_lock(&s->io_lock);
    bool ok = korad_write_raw(s, cmd);
    if (ok) {
        int n = korad_read_idle(s, out, outlen);
        ok = (n > 0);
        if (ok) s->rx_count++; else s->err_count++;
    }
    delay_ms(POST_CMD_DELAY_MS);
    pthread_mutex_unlock(&s->io_lock);
    return ok;
}

static bool korad_command(korad_state_t *s, const char *cmd) {
    pthread_mutex_lock(&s->io_lock);
    bool ok = korad_write_raw(s, cmd);
    delay_ms(POST_CMD_DELAY_MS);
    pthread_mutex_unlock(&s->io_lock);
    return ok;
}

/* ---- reader thread ---- */

static void *reader_main(void *arg) {
    psu_driver_t   *d = (psu_driver_t *)arg;
    korad_state_t *s = st_of(d);

    char idn[64];
    if (korad_query(s, "*IDN?", idn, sizeof(idn))) {
        fprintf(stderr, "korad: identified as: %s\n", idn);
        s->connected = true;
    } else {
        fprintf(stderr, "korad: *IDN? timed out — continuing anyway\n");
    }

    while (s->running) {
        char buf[64];
        float v = NAN, a = NAN;
        bool ok_v = korad_query(s, "VOUT1?", buf, sizeof(buf));
        if (ok_v) v = (float)atof(buf);
        bool ok_a = korad_query(s, "IOUT1?", buf, sizeof(buf));
        if (ok_a) a = (float)atof(buf);

        /* STATUS byte: bit0 = CV (1) / CC (0), bit6 = output enabled. */
        bool cv = true, on = s->out_on_cache;
        if (korad_query(s, "STATUS?", buf, sizeof(buf)) && strlen(buf) >= 1) {
            unsigned char st = (unsigned char)buf[0];
            cv = (st & 0x01) != 0;
            on = (st & 0x40) != 0;
            s->out_on_cache = on;
        }

        pthread_mutex_lock(&s->state_lock);
        psu_channel_state_t *cs = &s->state;
        if (ok_v) cs->out_v = v;
        if (ok_a) cs->out_a = a;
        cs->out_p   = (ok_v && ok_a) ? v * a : 0;
        cs->set_v   = s->set_v_cache;
        cs->set_a   = s->set_a_cache;
        cs->out_on  = on;
        cs->cv_mode = cv;
        cs->valid   = ok_v && ok_a;
        if (cs->valid) {
            cs->timestamp_ms = now_ms();
            s->connected = true;
        }
        pthread_mutex_unlock(&s->state_lock);

        for (int i = 0; i < POLL_MS / 20 && s->running; i++)
            delay_ms(20);
    }
    return NULL;
}

/* ---- vtable ---- */

static void v_close(psu_driver_t *self) {
    if (!self) return;
    korad_state_t *s = st_of(self);
    if (s) {
        s->running = false;
        pthread_join(s->reader, NULL);
        if (s->sp) serial_close(s->sp);
        pthread_mutex_destroy(&s->io_lock);
        pthread_mutex_destroy(&s->state_lock);
        free(s);
    }
    free(self);
}

static bool v_is_connected(psu_driver_t *self) { return st_of(self)->connected; }

static void v_get_channel(psu_driver_t *self, int ch, psu_channel_state_t *out) {
    if (ch != 1) { memset(out, 0, sizeof(*out)); return; }
    korad_state_t *s = st_of(self);
    pthread_mutex_lock(&s->state_lock);
    *out = s->state;
    pthread_mutex_unlock(&s->state_lock);
}

static bool v_set_voltage(psu_driver_t *self, int ch, float v) {
    if (ch != 1 || v < 0 || v > V_MAX) return false;
    korad_state_t *s = st_of(self);
    pthread_mutex_lock(&s->state_lock); s->set_v_cache = v; pthread_mutex_unlock(&s->state_lock);
    char buf[32];
    snprintf(buf, sizeof(buf), "VSET1:%.2f", v);
    return korad_command(s, buf);
}

static bool v_set_current(psu_driver_t *self, int ch, float a) {
    if (ch != 1 || a < 0 || a > I_MAX) return false;
    korad_state_t *s = st_of(self);
    pthread_mutex_lock(&s->state_lock); s->set_a_cache = a; pthread_mutex_unlock(&s->state_lock);
    char buf[32];
    snprintf(buf, sizeof(buf), "ISET1:%.3f", a);
    return korad_command(s, buf);
}

static bool v_set_output(psu_driver_t *self, int ch, bool on) {
    if (ch != 1) return false;
    korad_state_t *s = st_of(self);
    s->out_on_cache = on;
    return korad_command(s, on ? "OUT1" : "OUT0");
}

static void v_get_stats(psu_driver_t *self, uint32_t *rx, uint32_t *err) {
    korad_state_t *s = st_of(self);
    if (rx)  *rx  = s->rx_count;
    if (err) *err = s->err_count;
}

/* ---- factory ---- */

static psu_driver_t *korad_open(const char *device, int baud) {
    if (!device) return NULL;
    if (baud <= 0) baud = 9600;     /* Korad default */

    serial_port_t *sp = serial_open(device, baud);
    if (!sp) return NULL;

    korad_state_t *s = calloc(1, sizeof(*s));
    psu_driver_t  *d = calloc(1, sizeof(*d));
    if (!s || !d) { free(s); free(d); serial_close(sp); return NULL; }

    s->sp = sp;
    s->running = true;
    s->set_v_cache = 0.0f;
    s->set_a_cache = 0.1f;
    pthread_mutex_init(&s->io_lock, NULL);
    pthread_mutex_init(&s->state_lock, NULL);

    d->state = s;
    d->caps = (psu_caps_t){
        .model_name             = "Korad / TENMA / Velleman / Hanmatek (KA-protocol)",
        .n_channels             = 1,
        .v_max                  = V_MAX,
        .i_max                  = I_MAX,
        .supports_tracking      = false,
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
    d->set_tracking = NULL;
    d->get_stats    = v_get_stats;

    if (pthread_create(&s->reader, NULL, reader_main, d) != 0) {
        v_close(d);
        return NULL;
    }
    return d;
}

const psu_driver_factory_t korad_ka_factory = {
    .id              = "korad-ka",
    .display_name    = "Korad KA-series (+ Chinese clones)",
    .description     = "Korad/TENMA/Velleman/Hanmatek single-output PSUs over USB serial",
    .default_baud    = 9600,
    .n_channels_hint = 1,
    .open            = korad_open,
};
