/**
 * SCPI over direct USB serial. The instrument expects line-terminated
 * commands; serial_send_line / serial_read_line already do that for us.
 */

#include "scpi.h"
#include "serial_port.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    serial_port_t *sp;
} ser_state_t;

static ser_state_t *st(scpi_t *s) { return (ser_state_t *)s->state; }

static void ser_close(scpi_t *s) {
    if (!s) return;
    ser_state_t *t = st(s);
    if (t) {
        if (t->sp) serial_close(t->sp);
        free(t);
    }
    s->state = NULL;
}

static bool ser_send(scpi_t *s, const char *cmd) {
    return serial_send_line(st(s)->sp, cmd);
}

static bool ser_recv(scpi_t *s, char *out, size_t outlen, int timeout_ms) {
    return serial_read_line(st(s)->sp, out, outlen, timeout_ms);
}

scpi_t *scpi_serial_open(const char *device, int baud) {
    if (!device) return NULL;

    serial_port_t *sp = serial_open(device, baud);
    if (!sp) return NULL;

    scpi_t *s = calloc(1, sizeof(*s));
    ser_state_t *t = calloc(1, sizeof(*t));
    if (!s || !t) { free(s); free(t); serial_close(sp); return NULL; }

    pthread_mutex_init(&s->lock, NULL);
    t->sp = sp;
    s->state = t;
    s->close_impl = ser_close;
    s->send_impl  = ser_send;
    s->recv_impl  = ser_recv;
    return s;
}
