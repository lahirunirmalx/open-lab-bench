/**
 * SCPI client — thread-safe send/query against a SCPI-speaking instrument.
 *
 * The wire transport is selected by the port spec string passed to
 * scpi_open():
 *
 *   "serial:/dev/ttyUSB0"           direct SCPI-over-serial
 *   "serial:/dev/ttyUSB0:9600"      explicit baud override
 *   "prologix:/dev/ttyUSB0:5"       Prologix GPIB-USB adapter, GPIB addr 5
 *   "prologix:/dev/ttyUSB0:5:9600"  with explicit Prologix-port baud
 *   "<naked path>"                  shorthand for "serial:<path>"
 *
 * Drivers (e.g. Siglent SPD) talk only to scpi_t — they don't know whether
 * they're going over USB-serial directly or through a GPIB controller.
 */

#ifndef TRANSPORT_SCPI_H
#define TRANSPORT_SCPI_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct scpi scpi_t;

struct scpi {
    void           *state;          /* transport-private */
    pthread_mutex_t lock;

    /* Vtable filled by the chosen transport during scpi_open(). */
    void (*close_impl)(scpi_t *);
    bool (*send_impl) (scpi_t *, const char *cmd);
    bool (*recv_impl) (scpi_t *, char *out, size_t outlen, int timeout_ms);
};

/**
 * Open a SCPI connection described by port_spec. default_baud is used when
 * the spec doesn't carry an explicit baud rate.
 *
 * Returns NULL on failure.
 */
scpi_t *scpi_open(const char *port_spec, int default_baud);

void scpi_close(scpi_t *s);

/* Send a command line (e.g. "OUTP CH1,ON"). No '?' expected. Thread-safe. */
bool scpi_send(scpi_t *s, const char *cmd);

/*
 * Send a query (e.g. "MEAS:VOLT? CH1") and capture the response line.
 * Returns false on transport error or timeout. Thread-safe.
 */
bool scpi_query(scpi_t *s, const char *cmd, char *out, size_t outlen,
                int timeout_ms);

/* ---- factory hooks (called by scpi_open) — not for direct use --- */

/* Open a direct-serial SCPI transport. */
scpi_t *scpi_serial_open(const char *device, int baud);

/* Open a Prologix-GPIB-over-USB SCPI transport on the given serial device,
 * talking to the instrument at GPIB primary address `gpib_addr`. */
scpi_t *scpi_prologix_open(const char *device, int baud, int gpib_addr);

#endif
