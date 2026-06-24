/* serial — PCW host-side serial backend (PTY or TCP).
 *
 * Modelled after 1984's USIfAC backend layer: each "port" terminates
 * either in a host pseudo-terminal or in a TCP listener on localhost,
 * with ring buffers between the host fd and the guest-facing API.
 *
 * On the PCW this will be driven by the 8251 USART (CPS8256 add-on for
 * the 8256/8512, built into the 9512) — that I/O surface is not wired
 * yet; this header exposes only the backend lifecycle and a byte-level
 * RX/TX queue for whatever model is plugged in later.
 */
#pragma once

#include "types.h"
#include <stdbool.h>
#include <stddef.h>

#define SERIAL_RING 4096   /* power of two */

typedef enum {
    SERIAL_BACKEND_PTY = 0,
    SERIAL_BACKEND_TCP = 1,
} SerialBackend;

typedef struct Serial {
    bool present;             /* mirror of cfg->ext_serial at init time */
    SerialBackend backend;

    /* PTY backend */
    int  pty_master;          /* -1 when closed */
    char pty_slave[64];       /* /dev/pts/N for the overlay */
    char pty_link[64];        /* stable host-side alias, e.g. /tmp/1985-serial */

    /* TCP backend */
    int  tcp_listen;          /* -1 when closed */
    int  tcp_client;          /* -1 when no client */
    int  tcp_port;

    /* Ring buffers — power-of-two sized, masked indices */
    u8     rx_buf[SERIAL_RING];
    size_t rx_head, rx_tail;  /* head=push (backend), tail=pop (guest) */
    u8     tx_buf[SERIAL_RING];
    size_t tx_head, tx_tail;  /* head=push (guest),   tail=pop (backend) */
} Serial;

/* `backend` is "pty" or "tcp"; `tcp_port` is ignored for PTY.
 * `pty_link_path` is the stable host-side symlink to create for PTY
 * backends (e.g. "/tmp/1985-serial"); NULL or empty disables the link. */
void serial_init    (Serial *s, bool enable, const char *backend,
                     int tcp_port, const char *pty_link_path);
void serial_shutdown(Serial *s);

/* Drain backend → RX, push TX → backend. Call once per frame. */
void serial_poll(Serial *s);

/* Byte-level FIFO used by the USART model. */
bool serial_rx_pop (Serial *s, u8 *out);
bool serial_tx_push(Serial *s, u8 b);
bool serial_rx_has (const Serial *s);
