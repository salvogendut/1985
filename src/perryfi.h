/* perryfi — software models for PerryFi-class serial WiFi devices.
 *
 * The real PerryFi (by SanPollo) is a Wemos D1 mini (ESP8266) board
 * that plugs onto the CPS8256-style serial interface and exposes a
 * Hayes AT command set; CP/M terminal programs see it as a 9600 bps
 * modem and use ATDT to dial Telnet/Telephony hosts over WiFi.
 *
 * 1985 doesn't model the radio. In Hayes mode the AT interpreter
 * lives inside the emulator and forwards ATDT host:port to a host-side
 * TCP socket. In PerryNet mode the same serial line speaks the framed
 * socket API from ../PerryNet: SLIP, CRC-16, DNS, and TCP commands.
 *
 * Wiring: when ext_perryfi is on, the CPS8256 channel-A data port
 * reads/writes route through Perryfi instead of the raw pty/tcp
 * Serial backend.
 */
#pragma once

#include "types.h"
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#define PERRYFI_RING       4096   /* power of two */
#define PERRYFI_CMD_MAX     256
#define PERRYFI_ESC_CHAR    '+'
#define PERRYFI_ESC_COUNT     3
#define PERRYNET_MAX_PAYLOAD 512
#define PERRYNET_FRAME_MAX   (6 + PERRYNET_MAX_PAYLOAD + 2)
#define PERRYNET_MAX_CHANNELS  4
#define PERRYNET_MAX_TCP       PERRYNET_MAX_CHANNELS
#define PERRYNET_MAX_UDP       PERRYNET_MAX_CHANNELS

typedef enum {
    PERRYFI_MODE_HAYES = 0,       /* PerryFiFW-style Hayes AT modem */
    PERRYFI_MODE_PERRYNET,        /* PerryNet framed socket API */
} PerryfiMode;

typedef enum {
    PERRYFI_STATE_CMD     = 0,    /* AT-command mode */
    PERRYFI_STATE_ONLINE  = 1,    /* TCP-bridge mode */
} PerryfiState;

typedef struct {
    bool open;
    bool pull_rx;
    int  fd;
} PerryfiTcpChannel;

typedef struct {
    bool open;
    int  fd;
    u16  local_port;
} PerryfiUdpChannel;

typedef struct Perryfi {
    bool present;                 /* mirror of cfg->ext_perryfi at init time */
    PerryfiMode mode;

    /* Hayes AT-modem state. */
    PerryfiState state;

    /* AT line buffer (host → modem) */
    char  cmd_buf[PERRYFI_CMD_MAX + 1];
    int   cmd_len;
    char  last_cmd[PERRYFI_CMD_MAX + 1];   /* A/ repeat */

    /* Modem-side settings — kept in RAM only (no AT&W persistence). */
    bool  echo;
    bool  quiet;
    bool  verbose;
    bool  extended_codes;

    /* TCP client */
    int   tcp_fd;                 /* -1 when disconnected */
    char  remote_host[128];
    int   remote_port;

    /* +++ escape detection (only meaningful in ONLINE state) */
    int   esc_count;
    Uint64 esc_last_ms;

    /* Modem → guest queue (result strings, TCP bytes). */
    u8     rx_buf[PERRYFI_RING];
    size_t rx_head, rx_tail;

    /* PerryNet SLIP decoder and socket state. */
    u8     pn_frame[PERRYNET_FRAME_MAX];
    size_t pn_len;
    bool   pn_escaped;
    bool   pn_drop;
    bool   pn_wifi_connected;
    bool   pn_pass_set;
    char   pn_ssid[64];
    PerryfiTcpChannel pn_tcp[PERRYNET_MAX_TCP];
    PerryfiUdpChannel pn_udp[PERRYNET_MAX_UDP];
} Perryfi;

void perryfi_init    (Perryfi *p, bool enable, PerryfiMode mode);
void perryfi_shutdown(Perryfi *p);

/* Called once per frame: pump the +++ escape timer and drain the TCP
 * socket into the modem→guest RX queue. */
void perryfi_poll(Perryfi *p);

/* Guest-facing byte API (matches Serial's). */
bool perryfi_rx_pop (Perryfi *p, u8 *out);
bool perryfi_tx_push(Perryfi *p, u8 b);
bool perryfi_rx_has (const Perryfi *p);
