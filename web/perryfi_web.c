#include "perryfi_web.h"

#include <SDL3/SDL.h>
#include <emscripten.h>

EM_JS(void, perryfi_web_set_device, (int enabled, int mode), {
    const bridge = globalThis.JS1985PerryfiBridge;
    if (bridge) bridge.setDevice(Boolean(enabled), mode);
});

EM_JS(void, perryfi_web_connect, (void), {
    const bridge = globalThis.JS1985PerryfiBridge;
    if (bridge) bridge.connect();
});

EM_JS(void, perryfi_web_wifi_disconnect, (void), {
    const bridge = globalThis.JS1985PerryfiBridge;
    if (bridge) bridge.wifiDisconnect();
});

EM_JS(int, perryfi_web_connected, (void), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge && bridge.isConnected() ? 1 : 0;
});

EM_JS(void, perryfi_web_reset_channels, (void), {
    const bridge = globalThis.JS1985PerryfiBridge;
    if (bridge) bridge.resetChannels();
});

EM_JS(int, perryfi_web_dns, (const char *host), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge && bridge.dns(UTF8ToString(host)) ? 1 : 0;
});

EM_JS(int, perryfi_web_tcp_open,
      (int slot, const char *host, u16 port, u8 flags), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge && bridge.tcpOpen(slot, UTF8ToString(host), port, flags) ? 1 : 0;
});

EM_JS(int, perryfi_web_tcp_send,
      (int slot, const u8 *data, size_t len), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge && bridge.tcpSend(slot, HEAPU8.subarray(data, data + len)) ? 1 : 0;
});

EM_JS(void, perryfi_web_tcp_close, (int slot), {
    const bridge = globalThis.JS1985PerryfiBridge;
    if (bridge) bridge.tcpClose(slot);
});

EM_JS(int, perryfi_web_tcp_read,
      (int slot, u8 *data, size_t max_len), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge ? bridge.tcpRead(slot, HEAPU8, data, max_len) : -2;
});

EM_JS(int, perryfi_web_udp_open, (int slot, u16 local_port), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge && bridge.udpOpen(slot, local_port) ? 1 : 0;
});

EM_JS(int, perryfi_web_udp_send,
      (int slot, const u8 *address, u16 port, const u8 *data, size_t len), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge && bridge.udpSend(
      slot,
      HEAPU8.subarray(address, address + 4),
      port,
      HEAPU8.subarray(data, data + len)
    ) ? 1 : 0;
});

EM_JS(void, perryfi_web_udp_close, (int slot), {
    const bridge = globalThis.JS1985PerryfiBridge;
    if (bridge) bridge.udpClose(slot);
});

EM_JS(int, perryfi_web_udp_read,
      (int slot, u8 *address, u16 *port, u8 *data, size_t max_len), {
    const bridge = globalThis.JS1985PerryfiBridge;
    return bridge
      ? bridge.udpRead(slot, HEAPU8, address, port, data, max_len)
      : -2;
});

Uint64 SDL_GetTicks(void) {
    return (Uint64)emscripten_get_now();
}
