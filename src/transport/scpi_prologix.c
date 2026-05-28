/**
 * SCPI over a Prologix GPIB-USB / GPIB-Ethernet controller.
 *
 * The controller appears as a USB-serial device. Commands prefixed with
 * "++" are interpreted by the controller itself; everything else is passed
 * through to the GPIB instrument at the currently-selected primary address.
 *
 * Init sequence (per Prologix docs):
 *   ++mode 1     controller mode
 *   ++auto 1     auto-read after queries (so "VOLT?" returns the answer
 *                without an explicit ++read)
 *   ++eos 2      LF terminator on transmit
 *   ++eoi 1      assert EOI on last byte
 *   ++addr <N>   select instrument primary address
 *   ++ifc        interface clear (good practice on connect)
 *
 * The on-wire transport itself is plain serial line-mode. SCPI commands
 * leaving here are sent verbatim via serial_send_line(); query responses
 * come back as the instrument's reply line.
 */

#include "scpi.h"
#include "serial_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    serial_port_t *sp;
    int            gpib_addr;
} pro_state_t;

static pro_state_t *st(scpi_t *s) { return (pro_state_t *)s->state; }

static void pro_close(scpi_t *s) {
    if (!s) return;
    pro_state_t *t = st(s);
    if (t) {
        if (t->sp) serial_close(t->sp);
        free(t);
    }
    s->state = NULL;
}

static bool pro_send(scpi_t *s, const char *cmd) {
    return serial_send_line(st(s)->sp, cmd);
}

static bool pro_recv(scpi_t *s, char *out, size_t outlen, int timeout_ms) {
    /* With ++auto 1 the controller automatically reads after a query and
     * forwards the instrument response. */
    return serial_read_line(st(s)->sp, out, outlen, timeout_ms);
}

static bool pro_init(serial_port_t *sp, int gpib_addr) {
    char buf[64];
    /* Sleep-tolerant: send each init command and discard any stray reply. */
    static const char *common[] = {
        "++mode 1",
        "++auto 1",
        "++eos 2",
        "++eoi 1",
        NULL,
    };
    for (int i = 0; common[i]; i++) {
        if (!serial_send_line(sp, common[i])) return false;
    }
    snprintf(buf, sizeof(buf), "++addr %d", gpib_addr);
    if (!serial_send_line(sp, buf)) return false;
    if (!serial_send_line(sp, "++ifc")) return false;
    /* Drain any unsolicited input. */
    while (serial_read_line(sp, buf, sizeof(buf), 50)) { /* discard */ }
    return true;
}

scpi_t *scpi_prologix_open(const char *device, int baud, int gpib_addr) {
    if (!device || gpib_addr < 0 || gpib_addr > 30) return NULL;

    serial_port_t *sp = serial_open(device, baud);
    if (!sp) return NULL;

    if (!pro_init(sp, gpib_addr)) {
        fprintf(stderr, "prologix: failed to initialise controller on %s\n", device);
        serial_close(sp);
        return NULL;
    }

    scpi_t *s = calloc(1, sizeof(*s));
    pro_state_t *t = calloc(1, sizeof(*t));
    if (!s || !t) { free(s); free(t); serial_close(sp); return NULL; }

    pthread_mutex_init(&s->lock, NULL);
    t->sp = sp;
    t->gpib_addr = gpib_addr;
    s->state = t;
    s->close_impl = pro_close;
    s->send_impl  = pro_send;
    s->recv_impl  = pro_recv;
    return s;
}
