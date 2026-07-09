/* perryfi.c — PerryFi/PerryNet serial-facing firmware simulations.
 * See perryfi.h. */

/* _GNU_SOURCE (glibc) trumps the strict _POSIX_C_SOURCE filter so
 * MSG_NOSIGNAL stays visible. __BSD_VISIBLE does the same on the
 * BSDs; _DEFAULT_SOURCE is kept for older glibc. */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define __BSD_VISIBLE 1
#endif

#include "perryfi.h"
#include "notify.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* MSG_NOSIGNAL is a Linux extension; on the BSDs it's not defined.
 * serial_init() ignores SIGPIPE process-wide so passing 0 here is
 * safe. Falls back AFTER <sys/socket.h> so the system header has had
 * its say first. */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#define MASK (PERRYFI_RING - 1)

enum {
    PN_OP_HELLO             = 0x01,
    PN_OP_RESET_DEVICE      = 0x02,
    PN_OP_WIFI_GET          = 0x10,
    PN_OP_WIFI_SET          = 0x11,
    PN_OP_WIFI_CONNECT      = 0x12,
    PN_OP_WIFI_DISCONNECT   = 0x13,
    PN_OP_WIFI_STATUS       = 0x14,
    PN_OP_SETTINGS_SAVE     = 0x15,
    PN_OP_DNS_RESOLVE       = 0x20,
    PN_OP_TCP_OPEN          = 0x30,
    PN_OP_TCP_CLOSE         = 0x31,
    PN_OP_TCP_SEND          = 0x32,
    PN_OP_TCP_LISTEN        = 0x33,
    PN_OP_TCP_LISTEN_CLOSE  = 0x34,
    PN_OP_TCP_RECV          = 0x35,
    PN_OP_UDP_OPEN          = 0x40,
    PN_OP_UDP_CLOSE         = 0x41,
    PN_OP_UDP_SEND          = 0x42,
    PN_OP_UART_GET          = 0x50,
    PN_OP_UART_SET          = 0x51,
    PN_OP_TIME_GET          = 0x60,
    PN_OP_PING              = 0x70,
    PN_OP_ACK               = 0x80,
    PN_OP_EVENT             = 0x81,
    PN_OP_TCP_DATA          = 0x82,
    PN_OP_UDP_DATA          = 0x83,
};

enum {
    PN_STATUS_OK             = 0x00,
    PN_STATUS_BAD_FRAME      = 0x01,
    PN_STATUS_BAD_OPCODE     = 0x02,
    PN_STATUS_BAD_LENGTH     = 0x03,
    PN_STATUS_BAD_CHANNEL    = 0x04,
    PN_STATUS_NO_SLOT        = 0x05,
    PN_STATUS_WIFI_DOWN      = 0x06,
    PN_STATUS_CONNECT_FAILED = 0x07,
    PN_STATUS_IO_ERROR       = 0x08,
    PN_STATUS_UNSUPPORTED    = 0x09,
    PN_STATUS_BAD_ARGUMENT   = 0x0B,
};

enum {
    PN_EVT_READY      = 0x01,
    PN_EVT_WIFI_UP    = 0x02,
    PN_EVT_WIFI_DOWN  = 0x03,
    PN_EVT_TCP_CLOSED = 0x11,
    PN_EVT_TCP_ERROR  = 0x12,
    PN_EVT_UDP_ERROR  = 0x20,
};

#define PN_VERSION       1
#define PN_TCP_READ_MAX  192
#define PN_UDP_READ_MAX  256

/* ---------------------------------------------------------------- */
/* Ring buffer (modem → guest)                                      */

static inline size_t rb_count(size_t h, size_t t) { return (h - t) & MASK; }
static inline size_t rb_space(size_t h, size_t t) { return MASK - rb_count(h, t); }
static inline bool   rb_empty(size_t h, size_t t) { return h == t; }

static void rx_push(Perryfi *p, u8 b) {
    if (rb_space(p->rx_head, p->rx_tail) == 0) return;
    p->rx_buf[p->rx_head & MASK] = b;
    p->rx_head = (p->rx_head + 1) & MASK;
}
bool perryfi_rx_pop(Perryfi *p, u8 *out) {
    if (rb_empty(p->rx_head, p->rx_tail)) return false;
    *out = p->rx_buf[p->rx_tail & MASK];
    p->rx_tail = (p->rx_tail + 1) & MASK;
    return true;
}
bool perryfi_rx_has(const Perryfi *p) {
    return !rb_empty(p->rx_head, p->rx_tail);
}

static void emit_str(Perryfi *p, const char *s) {
    while (*s) rx_push(p, (u8)*s++);
}

/* Hayes result-code shipping. Verbose: "\r\nOK\r\n"; numeric: "0\r\n". */
typedef enum { RC_OK, RC_CONNECT, RC_RING, RC_NO_CARRIER, RC_ERROR, RC_NO_ANSWER } Result;
static void send_result(Perryfi *p, Result r) {
    if (p->quiet) return;
    rx_push(p, '\r'); rx_push(p, '\n');
    if (!p->verbose) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", (int)r);
        emit_str(p, buf);
    } else switch (r) {
        case RC_OK:         emit_str(p, "OK"); break;
        case RC_CONNECT:    emit_str(p, "CONNECT");
                           if (p->extended_codes) emit_str(p, " 9600");
                           break;
        case RC_RING:       emit_str(p, "RING"); break;
        case RC_NO_CARRIER: emit_str(p, "NO CARRIER"); break;
        case RC_ERROR:      emit_str(p, "ERROR"); break;
        case RC_NO_ANSWER:  emit_str(p, "NO ANSWER"); break;
    }
    rx_push(p, '\r'); rx_push(p, '\n');
}

/* ---------------------------------------------------------------- */
/* TCP client                                                       */

static void tcp_close(Perryfi *p) {
#ifndef _WIN32
    if (p->tcp_fd >= 0) { close(p->tcp_fd); p->tcp_fd = -1; }
#endif
    p->remote_host[0] = 0;
    p->remote_port    = 0;
}

#ifndef _WIN32
static int tcp_dial(Perryfi *p, const char *host, int port) {
    char portstr[16]; snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        fprintf(stderr, "perryfi: getaddrinfo('%s'): %s\n", host, strerror(errno));
        return -1;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        fprintf(stderr, "perryfi: socket: %s\n", strerror(errno));
        freeaddrinfo(res); return -1;
    }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "perryfi: connect('%s:%d'): %s\n", host, port, strerror(errno));
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    p->tcp_fd = fd;
    snprintf(p->remote_host, sizeof(p->remote_host), "%s", host);
    p->remote_port = port;
    notify_post("perryfi: connected to %s:%d", host, port);
    return 0;
}
#endif

/* ---------------------------------------------------------------- */
/* AT-command parser                                                */

static char *skip_ws(char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static char  upper(char c)    { return (c >= 'a' && c <= 'z') ? (c - 32) : c; }

/* Parse and execute one AT command line (without the leading "AT" or
 * the trailing CR). `cmd` is uppercased in place. */
static void exec_at(Perryfi *p, char *cmd) {
    char *s = cmd;
    if (!*s) { send_result(p, RC_OK); return; }

    /* Dispatch on the first letter; some commands take a digit or =. */
    char c = *s++;
    switch (c) {
        case 'E': p->echo    = (*s != '0'); send_result(p, RC_OK); return;
        case 'Q': p->quiet   = (*s == '1'); send_result(p, RC_OK); return;
        case 'V': p->verbose = (*s != '0'); send_result(p, RC_OK); return;
        case 'Z':
            p->echo = true; p->quiet = false; p->verbose = true;
            send_result(p, RC_OK); return;
        case 'I':
            emit_str(p, "\r\n1985 PerryFi AT-modem\r\n");
            send_result(p, RC_OK); return;
        case 'O':
            if (p->tcp_fd >= 0) {
                p->state = PERRYFI_STATE_ONLINE;
                send_result(p, RC_CONNECT);
            } else {
                send_result(p, RC_NO_CARRIER);
            }
            return;
        case 'H':
            tcp_close(p);
            p->state = PERRYFI_STATE_CMD;
            send_result(p, RC_OK); return;
        case 'A':
            /* No inbound listener — there's nothing to answer. */
            send_result(p, RC_NO_CARRIER); return;
        case 'D': {
            /* ATD <T> <host>:<port>  — dial out. */
            s = skip_ws(s);
            if (*s == 'T' || *s == 'P') s++;        /* tone/pulse — ignore */
            s = skip_ws(s);
            char host[128] = {0}; int port = 23;
            char *colon = strrchr(s, ':');
            if (colon) {
                size_t hl = (size_t)(colon - s);
                if (hl >= sizeof(host)) hl = sizeof(host) - 1;
                memcpy(host, s, hl);
                port = atoi(colon + 1);
            } else {
                snprintf(host, sizeof(host), "%s", s);
            }
            if (!host[0] || port <= 0) { send_result(p, RC_ERROR); return; }
#ifndef _WIN32
            if (tcp_dial(p, host, port) < 0) {
                send_result(p, RC_NO_CARRIER); return;
            }
            p->state = PERRYFI_STATE_ONLINE;
            p->esc_count = 0;
            send_result(p, RC_CONNECT);
#else
            send_result(p, RC_NO_CARRIER);
#endif
            return;
        }
        case '&':
            /* &W save, &V view, &F factory — accept everything as OK. */
            send_result(p, RC_OK); return;
        case '$':
            /* Proprietary config: SSID=, PASS=, MDNS=, BAUD=, etc.
             * We don't manage any radio, so just acknowledge. */
            send_result(p, RC_OK); return;
        case 'X':
            p->extended_codes = (*s != '0');
            send_result(p, RC_OK); return;
        case 'S':
            /* S-registers (Sn=v, Sn?). Accept and reply OK. */
            send_result(p, RC_OK); return;
    }
    send_result(p, RC_ERROR);
}

/* Strip whitespace and uppercase a command, then dispatch. */
static void run_at_line(Perryfi *p, char *line) {
    /* Optional A/ repeat. */
    if (strcmp(line, "A/") == 0) {
        if (p->last_cmd[0]) { strcpy(line, p->last_cmd); }
        else { send_result(p, RC_ERROR); return; }
    }
    /* Trim leading WS and uppercase. */
    char *s = skip_ws(line);
    for (char *q = s; *q; q++) *q = upper(*q);
    if (s[0] != 'A' || s[1] != 'T') {
        send_result(p, RC_ERROR); return;
    }
    snprintf(p->last_cmd, sizeof(p->last_cmd), "%s", line);
    exec_at(p, s + 2);
}

/* Echo a byte back to the host if echo is on. */
static void maybe_echo(Perryfi *p, u8 b) {
    if (p->echo) rx_push(p, b);
}

/* ---------------------------------------------------------------- */
/* PerryNet framed socket API                                       */

static void pn_append_u16(u8 *buf, size_t *len, u16 v) {
    buf[(*len)++] = (u8)(v & 0xFF);
    buf[(*len)++] = (u8)(v >> 8);
}

static void pn_append_u32(u8 *buf, size_t *len, u32 v) {
    buf[(*len)++] = (u8)(v & 0xFF);
    buf[(*len)++] = (u8)((v >> 8) & 0xFF);
    buf[(*len)++] = (u8)((v >> 16) & 0xFF);
    buf[(*len)++] = (u8)((v >> 24) & 0xFF);
}

static u16 pn_read_u16(const u8 *p) {
    return (u16)p[0] | ((u16)p[1] << 8);
}

static u16 pn_crc16(const u8 *data, size_t len) {
    u16 crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (u16)data[i] << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021)
                                 : (u16)(crc << 1);
    }
    return crc;
}

static void pn_emit_slip_byte(Perryfi *p, u8 b) {
    if (b == 0xC0) {
        rx_push(p, 0xDB);
        rx_push(p, 0xDC);
    } else if (b == 0xDB) {
        rx_push(p, 0xDB);
        rx_push(p, 0xDD);
    } else {
        rx_push(p, b);
    }
}

static void pn_send_frame(Perryfi *p, u8 opcode, u8 seq, u8 channel,
                          const u8 *payload, size_t payload_len) {
    if (payload_len > PERRYNET_MAX_PAYLOAD) return;

    u8 body[PERRYNET_FRAME_MAX];
    size_t len = 0;
    body[len++] = PN_VERSION;
    body[len++] = opcode;
    body[len++] = seq;
    body[len++] = channel;
    pn_append_u16(body, &len, (u16)payload_len);
    if (payload_len > 0 && payload) {
        memcpy(body + len, payload, payload_len);
        len += payload_len;
    }
    u16 crc = pn_crc16(body, len);
    body[len++] = (u8)(crc & 0xFF);
    body[len++] = (u8)(crc >> 8);

    rx_push(p, 0xC0);
    for (size_t i = 0; i < len; i++)
        pn_emit_slip_byte(p, body[i]);
    rx_push(p, 0xC0);
}

static void pn_send_ack(Perryfi *p, u8 seq, u8 channel, u8 status,
                        const u8 *payload, size_t payload_len) {
    u8 ack[PERRYNET_MAX_PAYLOAD];
    size_t len = 0;
    ack[len++] = status;
    if (payload_len > sizeof(ack) - len)
        payload_len = sizeof(ack) - len;
    if (payload_len > 0 && payload) {
        memcpy(ack + len, payload, payload_len);
        len += payload_len;
    }
    pn_send_frame(p, PN_OP_ACK, seq, channel, ack, len);
}

static void pn_send_event(Perryfi *p, u8 channel, u8 event,
                          const u8 *detail, size_t detail_len) {
    u8 payload[PERRYNET_MAX_PAYLOAD];
    size_t len = 0;
    payload[len++] = event;
    if (detail_len > sizeof(payload) - len)
        detail_len = sizeof(payload) - len;
    if (detail_len > 0 && detail) {
        memcpy(payload + len, detail, detail_len);
        len += detail_len;
    }
    pn_send_frame(p, PN_OP_EVENT, 0, channel, payload, len);
}

static void pn_append_fake_net(u8 *buf, size_t *len) {
    static const u8 ip[]      = { 192, 168, 198, 5 };
    static const u8 gateway[] = { 192, 168, 198, 1 };
    static const u8 netmask[] = { 255, 255, 255, 0 };
    static const u8 dns[]     = { 8, 8, 8, 8 };
    static const u8 mac[]     = { 0x02, 0x19, 0x85, 0x00, 0x00, 0x01 };
    memcpy(buf + *len, ip, sizeof(ip));           *len += sizeof(ip);
    memcpy(buf + *len, gateway, sizeof(gateway)); *len += sizeof(gateway);
    memcpy(buf + *len, netmask, sizeof(netmask)); *len += sizeof(netmask);
    memcpy(buf + *len, dns, sizeof(dns));         *len += sizeof(dns);
    memcpy(buf + *len, mac, sizeof(mac));         *len += sizeof(mac);
}

static void pn_append_zero_net(u8 *buf, size_t *len) {
    memset(buf + *len, 0, 22);
    *len += 22;
}

static size_t pn_wifi_status_payload(const Perryfi *p, u8 *buf) {
    size_t len = 0;
    buf[len++] = p->pn_wifi_connected ? 3 : 0; /* WL_CONNECTED / idle-ish */
    buf[len++] = p->pn_wifi_connected ? 1 : 0;
    pn_append_u32(buf, &len, p->pn_wifi_connected ? (u32)(s32)-42 : 0);
    if (p->pn_wifi_connected)
        pn_append_fake_net(buf, &len);
    else
        pn_append_zero_net(buf, &len);
    return len;
}

static void pn_tcp_close(Perryfi *p, int idx) {
    if (idx < 0 || idx >= PERRYNET_MAX_TCP) return;
#ifndef _WIN32
    if (p->pn_tcp[idx].open && p->pn_tcp[idx].fd >= 0)
        close(p->pn_tcp[idx].fd);
#endif
    p->pn_tcp[idx].open = false;
    p->pn_tcp[idx].pull_rx = false;
    p->pn_tcp[idx].fd = -1;
}

static void pn_tcp_close_all(Perryfi *p) {
    for (int i = 0; i < PERRYNET_MAX_TCP; i++)
        pn_tcp_close(p, i);
}

static void pn_udp_close(Perryfi *p, int idx) {
    if (idx < 0 || idx >= PERRYNET_MAX_UDP) return;
#ifndef _WIN32
    if (p->pn_udp[idx].open && p->pn_udp[idx].fd >= 0)
        close(p->pn_udp[idx].fd);
#endif
    p->pn_udp[idx].open = false;
    p->pn_udp[idx].fd = -1;
    p->pn_udp[idx].local_port = 0;
}

static void pn_udp_close_all(Perryfi *p) {
    for (int i = 0; i < PERRYNET_MAX_UDP; i++)
        pn_udp_close(p, i);
}

static int pn_tcp_slot(const Perryfi *p, u8 channel) {
    if (channel == 0 || channel > PERRYNET_MAX_TCP) return -1;
    int idx = (int)channel - 1;
    return p->pn_tcp[idx].open ? idx : -1;
}

static int pn_udp_slot(const Perryfi *p, u8 channel) {
    if (channel == 0 || channel > PERRYNET_MAX_UDP) return -1;
    int idx = (int)channel - 1;
    return p->pn_udp[idx].open ? idx : -1;
}

static int pn_tcp_free_slot(const Perryfi *p) {
    for (int i = 0; i < PERRYNET_MAX_TCP; i++)
        if (!p->pn_tcp[i].open && !p->pn_udp[i].open) return i;
    return -1;
}

static int pn_udp_free_slot(const Perryfi *p) {
    for (int i = 0; i < PERRYNET_MAX_UDP; i++)
        if (!p->pn_tcp[i].open && !p->pn_udp[i].open) return i;
    return -1;
}

#ifndef _WIN32
static int pn_tcp_open(Perryfi *p, int idx, const char *host, u16 port,
                       u8 flags, u8 *local_ip, u16 *local_port) {
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo(host, portstr, &hints, &res);
    if (gai != 0 || !res) {
        fprintf(stderr, "perrynet: getaddrinfo('%s'): %s\n",
                host, gai_strerror(gai));
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        fprintf(stderr, "perrynet: socket: %s\n", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        fprintf(stderr, "perrynet: connect('%s:%u'): %s\n",
                host, (unsigned)port, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    if (flags & 0x01) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }

    struct sockaddr_in local;
    socklen_t local_len = sizeof(local);
    memset(&local, 0, sizeof(local));
    if (getsockname(fd, (struct sockaddr *)&local, &local_len) == 0) {
        memcpy(local_ip, &local.sin_addr.s_addr, 4);
        *local_port = ntohs(local.sin_port);
    } else {
        local_ip[0] = 192; local_ip[1] = 168;
        local_ip[2] = 198; local_ip[3] = 5;
        *local_port = 0;
    }

    p->pn_tcp[idx].fd = fd;
    p->pn_tcp[idx].open = true;
    p->pn_tcp[idx].pull_rx = (flags & 0x02) != 0;
    notify_post("perrynet: TCP %d connected to %s:%u",
                idx + 1, host, (unsigned)port);
    return 0;
}

static int pn_udp_open(Perryfi *p, int idx, u16 local_port, u16 *actual_port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "perrynet: udp socket: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(local_port);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        fprintf(stderr, "perrynet: udp bind(%u): %s\n",
                (unsigned)local_port, strerror(errno));
        close(fd);
        return -1;
    }

    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    socklen_t local_len = sizeof(local);
    memset(&local, 0, sizeof(local));
    if (getsockname(fd, (struct sockaddr *)&local, &local_len) == 0) {
        *actual_port = ntohs(local.sin_port);
    } else {
        *actual_port = local_port;
    }

    p->pn_udp[idx].fd = fd;
    p->pn_udp[idx].open = true;
    p->pn_udp[idx].local_port = *actual_port;
    notify_post("perrynet: UDP %d opened on port %u",
                idx + 1, (unsigned)*actual_port);
    return 0;
}
#endif

static void pn_reset(Perryfi *p, bool ready_event) {
    pn_tcp_close_all(p);
    pn_udp_close_all(p);
    p->pn_len = 0;
    p->pn_escaped = false;
    p->pn_drop = false;
    p->pn_wifi_connected = true;
    p->pn_pass_set = false;
    snprintf(p->pn_ssid, sizeof(p->pn_ssid), "1985");
    if (ready_event)
        pn_send_event(p, 0, PN_EVT_READY, NULL, 0);
}

static void pn_ack_wifi_up(Perryfi *p) {
    u8 payload[32];
    size_t len = pn_wifi_status_payload(p, payload);
    pn_send_event(p, 0, PN_EVT_WIFI_UP, payload, len);
}

static void pn_handle_frame(Perryfi *p, const u8 *body, size_t body_len) {
    if (body_len < 8) {
        pn_send_ack(p, 0, 0, PN_STATUS_BAD_FRAME, NULL, 0);
        return;
    }

    u8 version = body[0];
    u8 opcode  = body[1];
    u8 seq     = body[2];
    u8 channel = body[3];
    u16 payload_len = pn_read_u16(body + 4);
    size_t expected = 6u + payload_len + 2u;
    if (version != PN_VERSION || body_len != expected ||
        payload_len > PERRYNET_MAX_PAYLOAD) {
        pn_send_ack(p, seq, channel, PN_STATUS_BAD_FRAME, NULL, 0);
        return;
    }

    u16 expected_crc = pn_read_u16(body + body_len - 2);
    u16 actual_crc = pn_crc16(body, body_len - 2);
    if (expected_crc != actual_crc) {
        pn_send_ack(p, seq, channel, PN_STATUS_BAD_FRAME, NULL, 0);
        return;
    }

    const u8 *payload = body + 6;
    u8 response[PERRYNET_MAX_PAYLOAD];
    size_t response_len = 0;

    switch (opcode) {
        case PN_OP_HELLO: {
            response[response_len++] = 1; /* major */
            response[response_len++] = 0; /* minor */
            pn_append_u16(response, &response_len, PERRYNET_MAX_PAYLOAD);
            response[response_len++] = PERRYNET_MAX_TCP;
            response[response_len++] = 0; /* listeners: follow-up scope */
            pn_append_u32(response, &response_len, 0x7F); /* WiFi/DNS/TCP/UDP/UART/TIME */
            const char name[] = "1985 PerryNet";
            memcpy(response + response_len, name, sizeof(name));
            response_len += sizeof(name);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
            return;
        }
        case PN_OP_RESET_DEVICE:
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            pn_reset(p, true);
            return;
        case PN_OP_WIFI_GET: {
            size_t ssid_len = strlen(p->pn_ssid);
            if (ssid_len > 63) ssid_len = 63;
            response[response_len++] = (u8)ssid_len;
            response[response_len++] = p->pn_pass_set ? 1 : 0;
            memcpy(response + response_len, p->pn_ssid, ssid_len);
            response_len += ssid_len;
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
            return;
        }
        case PN_OP_WIFI_SET: {
            if (payload_len < 2) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_LENGTH, NULL, 0);
                return;
            }
            u8 ssid_len = payload[0];
            u8 pass_len = payload[1];
            if ((size_t)ssid_len + (size_t)pass_len + 2u != payload_len ||
                ssid_len >= sizeof(p->pn_ssid)) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_ARGUMENT, NULL, 0);
                return;
            }
            memcpy(p->pn_ssid, payload + 2, ssid_len);
            p->pn_ssid[ssid_len] = '\0';
            p->pn_pass_set = pass_len > 0;
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            return;
        }
        case PN_OP_WIFI_CONNECT:
            p->pn_wifi_connected = true;
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            pn_ack_wifi_up(p);
            return;
        case PN_OP_WIFI_DISCONNECT:
            p->pn_wifi_connected = false;
            pn_tcp_close_all(p);
            pn_udp_close_all(p);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            pn_send_event(p, 0, PN_EVT_WIFI_DOWN, NULL, 0);
            return;
        case PN_OP_WIFI_STATUS:
            response_len = pn_wifi_status_payload(p, response);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
            return;
        case PN_OP_SETTINGS_SAVE:
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            return;
        case PN_OP_DNS_RESOLVE: {
            if (payload_len == 0 || payload_len >= 128) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_ARGUMENT, NULL, 0);
                return;
            }
            if (!p->pn_wifi_connected) {
                pn_send_ack(p, seq, channel, PN_STATUS_WIFI_DOWN, NULL, 0);
                return;
            }
#ifndef _WIN32
            char host[128];
            memcpy(host, payload, payload_len);
            host[payload_len] = '\0';
            struct addrinfo hints, *res = NULL;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            int gai = getaddrinfo(host, NULL, &hints, &res);
            if (gai != 0 || !res) {
                pn_send_ack(p, seq, channel, PN_STATUS_CONNECT_FAILED, NULL, 0);
                return;
            }
            struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
            memcpy(response, &sa->sin_addr.s_addr, 4);
            freeaddrinfo(res);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, 4);
#else
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
#endif
            return;
        }
        case PN_OP_TCP_OPEN: {
            if (!p->pn_wifi_connected) {
                pn_send_ack(p, seq, channel, PN_STATUS_WIFI_DOWN, NULL, 0);
                return;
            }
            if (payload_len < 4) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_LENGTH, NULL, 0);
                return;
            }
            u8 host_len = payload[0];
            if (host_len == 0 || host_len >= 128 ||
                payload_len != (u16)(1u + host_len + 2u + 1u)) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_ARGUMENT, NULL, 0);
                return;
            }
            int slot = pn_tcp_free_slot(p);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_NO_SLOT, NULL, 0);
                return;
            }
            char host[128];
            memcpy(host, payload + 1, host_len);
            host[host_len] = '\0';
            u16 port = pn_read_u16(payload + 1 + host_len);
            u8 flags = payload[1 + host_len + 2];
            if (port == 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_ARGUMENT, NULL, 0);
                return;
            }
#ifndef _WIN32
            u8 local_ip[4] = { 192, 168, 198, 5 };
            u16 local_port = 0;
            if (pn_tcp_open(p, slot, host, port, flags, local_ip, &local_port) < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_CONNECT_FAILED, NULL, 0);
                return;
            }
            response[response_len++] = (u8)(slot + 1);
            memcpy(response + response_len, local_ip, 4);
            response_len += 4;
            pn_append_u16(response, &response_len, local_port);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
#else
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
#endif
            return;
        }
        case PN_OP_TCP_CLOSE: {
            int slot = pn_tcp_slot(p, channel);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_CHANNEL, NULL, 0);
                return;
            }
            pn_tcp_close(p, slot);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            return;
        }
        case PN_OP_TCP_SEND: {
            int slot = pn_tcp_slot(p, channel);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_CHANNEL, NULL, 0);
                return;
            }
#ifndef _WIN32
            if (payload_len > 0) {
                ssize_t n = send(p->pn_tcp[slot].fd, payload, payload_len,
                                 MSG_NOSIGNAL);
                if (n < 0 || (size_t)n != payload_len) {
                    pn_tcp_close(p, slot);
                    pn_send_ack(p, seq, channel, PN_STATUS_IO_ERROR, NULL, 0);
                    pn_send_event(p, channel, PN_EVT_TCP_ERROR,
                                  (const u8[]){ PN_STATUS_IO_ERROR }, 1);
                    return;
                }
            }
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
#else
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
#endif
            return;
        }
        case PN_OP_TCP_RECV: {
            int slot = pn_tcp_slot(p, channel);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_CHANNEL, NULL, 0);
                return;
            }
            if (payload_len != 0 && payload_len != 2) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_LENGTH, NULL, 0);
                return;
            }
#ifndef _WIN32
            size_t max_len = PN_TCP_READ_MAX;
            if (payload_len == 2)
                max_len = pn_read_u16(payload);
            if (max_len > PERRYNET_MAX_PAYLOAD - 1)
                max_len = PERRYNET_MAX_PAYLOAD - 1;
            ssize_t n = 0;
            if (max_len > 0)
                n = recv(p->pn_tcp[slot].fd, response, max_len, 0);
            if (n > 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_OK, response, (size_t)n);
                return;
            }
            if (n == 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
                pn_tcp_close(p, slot);
                pn_send_event(p, channel, PN_EVT_TCP_CLOSED, NULL, 0);
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
                return;
            }
            pn_send_ack(p, seq, channel, PN_STATUS_IO_ERROR, NULL, 0);
            pn_tcp_close(p, slot);
            pn_send_event(p, channel, PN_EVT_TCP_ERROR,
                          (const u8[]){ PN_STATUS_IO_ERROR }, 1);
#else
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
#endif
            return;
        }
        case PN_OP_UDP_OPEN: {
            if (!p->pn_wifi_connected) {
                pn_send_ack(p, seq, channel, PN_STATUS_WIFI_DOWN, NULL, 0);
                return;
            }
            if (payload_len != 2) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_LENGTH, NULL, 0);
                return;
            }
            int slot = pn_udp_free_slot(p);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_NO_SLOT, NULL, 0);
                return;
            }
#ifndef _WIN32
            u16 local_port = pn_read_u16(payload);
            u16 actual_port = 0;
            if (pn_udp_open(p, slot, local_port, &actual_port) < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_IO_ERROR, NULL, 0);
                return;
            }
            response[response_len++] = (u8)(slot + 1);
            pn_append_u16(response, &response_len, actual_port);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
#else
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
#endif
            return;
        }
        case PN_OP_UDP_CLOSE: {
            int slot = pn_udp_slot(p, channel);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_CHANNEL, NULL, 0);
                return;
            }
            pn_udp_close(p, slot);
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            return;
        }
        case PN_OP_UDP_SEND: {
            int slot = pn_udp_slot(p, channel);
            if (slot < 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_CHANNEL, NULL, 0);
                return;
            }
            if (payload_len < 6) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_LENGTH, NULL, 0);
                return;
            }
            u16 port = pn_read_u16(payload + 4);
            if (port == 0) {
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_ARGUMENT, NULL, 0);
                return;
            }
#ifndef _WIN32
            struct sockaddr_in remote;
            memset(&remote, 0, sizeof(remote));
            remote.sin_family = AF_INET;
            memcpy(&remote.sin_addr.s_addr, payload, 4);
            remote.sin_port = htons(port);
            ssize_t n = sendto(p->pn_udp[slot].fd, payload + 6,
                               (size_t)(payload_len - 6), 0,
                               (struct sockaddr *)&remote, sizeof(remote));
            if (n < 0 || (size_t)n != (size_t)(payload_len - 6)) {
                pn_send_ack(p, seq, channel, PN_STATUS_IO_ERROR, NULL, 0);
                pn_send_event(p, channel, PN_EVT_UDP_ERROR,
                              (const u8[]){ PN_STATUS_IO_ERROR }, 1);
                return;
            }
            pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
#else
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
#endif
            return;
        }
        case PN_OP_UART_GET:
            pn_append_u32(response, &response_len, 9600);
            response[response_len++] = 0x01; /* RTS/CTS enabled on real hardware */
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
            return;
        case PN_OP_UART_SET:
            if (payload_len != 5)
                pn_send_ack(p, seq, channel, PN_STATUS_BAD_LENGTH, NULL, 0);
            else
                pn_send_ack(p, seq, channel, PN_STATUS_OK, NULL, 0);
            return;
        case PN_OP_TIME_GET: {
            time_t now = time(NULL);
            response[response_len++] = now > 0 ? 1 : 0;
            pn_append_u32(response, &response_len, now > 0 ? (u32)now : 0);
            pn_append_u32(response, &response_len, (u32)SDL_GetTicks());
            pn_send_ack(p, seq, channel, PN_STATUS_OK, response, response_len);
            return;
        }
        case PN_OP_PING:
            pn_send_ack(p, seq, channel, PN_STATUS_OK, payload, payload_len);
            return;
        case PN_OP_TCP_LISTEN:
        case PN_OP_TCP_LISTEN_CLOSE:
            pn_send_ack(p, seq, channel, PN_STATUS_UNSUPPORTED, NULL, 0);
            return;
        default:
            pn_send_ack(p, seq, channel, PN_STATUS_BAD_OPCODE, NULL, 0);
            return;
    }
}

static bool perrynet_tx_push(Perryfi *p, u8 b) {
    if (b == 0xC0) {
        if (p->pn_drop) {
            pn_send_ack(p, 0, 0, PN_STATUS_BAD_FRAME, NULL, 0);
        } else if (p->pn_len > 0) {
            pn_handle_frame(p, p->pn_frame, p->pn_len);
        }
        p->pn_len = 0;
        p->pn_escaped = false;
        p->pn_drop = false;
        return true;
    }

    if (p->pn_drop) return true;

    if (p->pn_escaped) {
        if (b == 0xDC) b = 0xC0;
        else if (b == 0xDD) b = 0xDB;
        else {
            p->pn_drop = true;
            p->pn_len = 0;
            p->pn_escaped = false;
            return true;
        }
        p->pn_escaped = false;
    } else if (b == 0xDB) {
        p->pn_escaped = true;
        return true;
    }

    if (p->pn_len >= sizeof(p->pn_frame)) {
        p->pn_drop = true;
        p->pn_len = 0;
        return true;
    }
    p->pn_frame[p->pn_len++] = b;
    return true;
}

static void perrynet_poll(Perryfi *p) {
#ifndef _WIN32
    for (int i = 0; i < PERRYNET_MAX_TCP; i++) {
        if (!p->pn_tcp[i].open || p->pn_tcp[i].fd < 0) continue;
        if (p->pn_tcp[i].pull_rx) continue;
        if (rb_space(p->rx_head, p->rx_tail) < (PN_TCP_READ_MAX * 2 + 16))
            break;

        u8 payload[PN_TCP_READ_MAX];
        ssize_t n = recv(p->pn_tcp[i].fd, payload, sizeof(payload), 0);
        if (n > 0) {
            pn_send_frame(p, PN_OP_TCP_DATA, 0, (u8)(i + 1),
                          payload, (size_t)n);
            continue;
        }
        if (n == 0) {
            pn_tcp_close(p, i);
            pn_send_event(p, (u8)(i + 1), PN_EVT_TCP_CLOSED, NULL, 0);
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        pn_tcp_close(p, i);
        pn_send_event(p, (u8)(i + 1), PN_EVT_TCP_ERROR,
                      (const u8[]){ PN_STATUS_IO_ERROR }, 1);
    }

    for (int i = 0; i < PERRYNET_MAX_UDP; i++) {
        if (!p->pn_udp[i].open || p->pn_udp[i].fd < 0) continue;

        u8 payload[6 + PN_UDP_READ_MAX];
        for (;;) {
            if (rb_space(p->rx_head, p->rx_tail) < (PN_UDP_READ_MAX * 2 + 32))
                return;
            struct sockaddr_in remote;
            socklen_t remote_len = sizeof(remote);
            ssize_t n = recvfrom(p->pn_udp[i].fd, payload + 6, PN_UDP_READ_MAX, 0,
                                 (struct sockaddr *)&remote, &remote_len);
            if (n > 0) {
                u16 rport = ntohs(remote.sin_port);
                memcpy(payload, &remote.sin_addr.s_addr, 4);
                payload[4] = (u8)(rport & 0xFF);
                payload[5] = (u8)(rport >> 8);
                pn_send_frame(p, PN_OP_UDP_DATA, 0, (u8)(i + 1),
                              payload, (size_t)n + 6);
                continue;
            }
            if (n == 0) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            pn_send_event(p, (u8)(i + 1), PN_EVT_UDP_ERROR,
                          (const u8[]){ PN_STATUS_IO_ERROR }, 1);
            break;
        }
    }
#else
    (void)p;
#endif
}

/* Guest pushed one byte into the modem. */
static bool hayes_tx_push(Perryfi *p, u8 b) {
    if (!p->present) return false;

    if (p->state == PERRYFI_STATE_ONLINE) {
        /* +++ escape sequence: three '+' with ≥1 s of silence on
         * either side. Roughly modelled — full-blown guard-time is
         * unnecessary for emulator interaction. */
        Uint64 now = SDL_GetTicks();
        if (b == PERRYFI_ESC_CHAR &&
            (p->esc_count == 0 || (now - p->esc_last_ms) < 1000)) {
            p->esc_count++;
            p->esc_last_ms = now;
            if (p->esc_count >= PERRYFI_ESC_COUNT) {
                p->state = PERRYFI_STATE_CMD;
                p->esc_count = 0;
                send_result(p, RC_OK);
                return true;
            }
        } else {
            p->esc_count = 0;
        }
#ifndef _WIN32
        if (p->tcp_fd >= 0) {
            ssize_t n = send(p->tcp_fd, &b, 1, MSG_NOSIGNAL);
            if (n < 0) {
                tcp_close(p);
                p->state = PERRYFI_STATE_CMD;
                send_result(p, RC_NO_CARRIER);
            }
        }
#endif
        return true;
    }

    /* Command mode: accumulate the line and dispatch on CR. */
    if (b == '\r') {
        p->cmd_buf[p->cmd_len] = 0;
        if (p->cmd_len > 0) {
            maybe_echo(p, b);
            run_at_line(p, p->cmd_buf);
        }
        p->cmd_len = 0;
        return true;
    }
    if (b == 0x08 || b == 0x7F) {           /* BS / DEL */
        if (p->cmd_len > 0) { p->cmd_len--; maybe_echo(p, b); }
        return true;
    }
    if (p->cmd_len < PERRYFI_CMD_MAX) {
        p->cmd_buf[p->cmd_len++] = (char)b;
        maybe_echo(p, b);
    }
    return true;
}

bool perryfi_tx_push(Perryfi *p, u8 b) {
    if (!p->present) return false;
    if (p->mode == PERRYFI_MODE_PERRYNET)
        return perrynet_tx_push(p, b);
    return hayes_tx_push(p, b);
}

/* ---------------------------------------------------------------- */
/* Lifecycle + poll                                                 */

void perryfi_init(Perryfi *p, bool enable, PerryfiMode mode) {
    memset(p, 0, sizeof(*p));
    p->present        = enable;
    p->mode           = mode;
    p->state          = PERRYFI_STATE_CMD;
    p->echo           = true;
    p->verbose        = true;
    p->extended_codes = false;
    p->tcp_fd         = -1;
    for (int i = 0; i < PERRYNET_MAX_TCP; i++)
        p->pn_tcp[i].fd = -1;
    for (int i = 0; i < PERRYNET_MAX_UDP; i++)
        p->pn_udp[i].fd = -1;
    if (enable && mode == PERRYFI_MODE_PERRYNET)
        pn_reset(p, true);
}

void perryfi_shutdown(Perryfi *p) {
    tcp_close(p);
    pn_tcp_close_all(p);
    pn_udp_close_all(p);
    p->present = false;
}

static void hayes_poll(Perryfi *p) {
#ifndef _WIN32
    if (p->state == PERRYFI_STATE_ONLINE && p->tcp_fd >= 0) {
        /* Drain socket into the modem→guest queue (capped per tick). */
        for (int i = 0; i < 256; i++) {
            if (rb_space(p->rx_head, p->rx_tail) == 0) break;
            u8 b;
            ssize_t n = recv(p->tcp_fd, &b, 1, 0);
            if (n == 0) {
                tcp_close(p);
                p->state = PERRYFI_STATE_CMD;
                send_result(p, RC_NO_CARRIER);
                break;
            }
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                tcp_close(p);
                p->state = PERRYFI_STATE_CMD;
                send_result(p, RC_NO_CARRIER);
                break;
            }
            rx_push(p, b);
        }
    }
#endif
}

void perryfi_poll(Perryfi *p) {
    if (!p->present) return;
    if (p->mode == PERRYFI_MODE_PERRYNET)
        perrynet_poll(p);
    else
        hayes_poll(p);
}
