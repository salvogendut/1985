#pragma once

#include "types.h"
#include <stddef.h>

/* Browser transport used by src/perryfi.c when compiled with Emscripten.
 * The JavaScript bridge owns the WebSocket and queues network data so the
 * emulated serial device can remain entirely non-blocking. */
void perryfi_web_set_device(int enabled, int mode);
void perryfi_web_connect(void);
void perryfi_web_wifi_disconnect(void);
int  perryfi_web_connected(void);
void perryfi_web_reset_channels(void);

int perryfi_web_dns(const char *host);

int perryfi_web_tcp_open(int slot, const char *host, u16 port, u8 flags);
int perryfi_web_tcp_send(int slot, const u8 *data, size_t len);
void perryfi_web_tcp_close(int slot);
int perryfi_web_tcp_read(int slot, u8 *data, size_t max_len);

int perryfi_web_udp_open(int slot, u16 local_port);
int perryfi_web_udp_send(int slot, const u8 *address, u16 port,
                         const u8 *data, size_t len);
void perryfi_web_udp_close(int slot);
int perryfi_web_udp_read(int slot, u8 *address, u16 *port,
                         u8 *data, size_t max_len);
