# PerryFi Networking in the WASM Build

## The problem

Native 1985 can give the emulated PerryFi device direct access to host TCP and
UDP sockets. A browser cannot do that. Browser JavaScript can use HTTP and
WebSocket APIs, but it cannot open a raw TCP connection to a Telnet server or
send an NTP datagram directly to UDP port 123.

Replacing PerryFi with a browser-specific network API would have made the web
edition behave differently from the native emulator. Guest software would no
longer be talking to the same CPS8256 serial hardware, Hayes command parser, or
PerryNet SLIP/CRC protocol implementation.

The solution is to keep the emulated device intact and replace only its host
socket boundary.

## Architecture

```text
 PCW application (TELNET.APP, NETTEST.APP, TIMESYNC.APP, ...)
                              |
                   emulated CPS8256 DART
                              |
              src/perryfi.c compiled into WASM
                 /                         \
        Hayes AT firmware            PerryNet firmware
                 \                         /
                  web/perryfi_web.c (EM_JS)
                              |
                  web/perryfi-bridge.js
                              |
             binary WebSocket /perryfi protocol
                              |
                  web/relay/server.js
                    /        |        \
                  DNS       TCP       UDP
```

The important boundary is below the firmware simulation. From the PCW's point
of view, nothing browser-specific exists:

1. The guest still accesses the CPS8256 ports.
2. The CPS8256 still exposes its emulated Z80-DART serial channel.
3. PerryFi still receives and emits serial bytes.
4. Hayes commands and PerryNet frames are still parsed by `src/perryfi.c`.
5. Only operations that would normally call `getaddrinfo()`, `connect()`,
   `send()`, `recv()`, or `sendto()` cross into JavaScript.

This gives the WASM build the same guest-visible device behavior while using a
transport browsers are allowed to open.

## Reusing the real emulation

Before this work, the web build linked no-op CPS and PerryFi stubs. The WASM
Makefile now compiles both `src/cps.c` and `src/perryfi.c`. Native builds retain
their POSIX socket path, while `__EMSCRIPTEN__` selects the browser transport.

This matters for more than code reuse. It preserves:

- CPS8256 port decoding and DART register behavior;
- the Hayes command state machine, result codes, and online data mode;
- PerryNet SLIP framing, escaping, CRC validation, opcodes, channels, events,
  and status codes;
- the normal PCW polling cadence and serial receive queue;
- compatibility with the same GEOS applications used on real hardware.

The web-specific C surface is deliberately small. `web/perryfi_web.c` uses
Emscripten `EM_JS` functions to call the browser bridge for DNS, TCP, and UDP.
It also supplies `SDL_GetTicks()` from `emscripten_get_now()` so the shared
firmware timing code does not need a second implementation.

## Adapting asynchronous browser I/O

The native socket code can complete some operations synchronously. Browser
networking is event-driven, and waiting synchronously inside WASM would freeze
the emulator and prevent WebSocket events from being delivered.

The bridge handles that mismatch in two directions.

### Requests from WASM

An open or DNS function only reports whether the request was accepted for
delivery. The firmware records the operation as pending, including the
PerryNet sequence and channel that must eventually receive the result.

The bridge assigns its own 16-bit request ID and sends a WebSocket frame. When
the result arrives, JavaScript calls one of the exported completion functions:

- `poc_perryfi_dns_result()`
- `poc_perryfi_tcp_open_result()`
- `poc_perryfi_udp_open_result()`

Those functions re-enter the shared C model, clear the pending state, and
generate the normal PerryNet ACK or Hayes result code for the guest.

### Data returning to WASM

TCP bytes and UDP datagrams arriving from the relay are placed in bounded
JavaScript queues. Once per emulated frame, `perryfi_poll()` asks the bridge for
available data and moves it into the existing PerryFi serial path. No browser
callback runs the emulated CPU, and no emulation callback blocks the browser
event loop.

This queue-and-poll arrangement is also why large Telnet banners do not need a
special browser implementation. They flow through the same firmware receive
path as native socket data, in bounded pieces the guest can consume.

## Supporting both PerryFi models

The transport bridge is shared, but the guest-facing behavior remains mode
specific.

### AT Hayes

The Hayes parser stays in C. `ATDhost:port` asks the bridge to open TCP channel
zero. A successful asynchronous result produces `CONNECT`; received TCP bytes
enter the modem RX queue, and guest bytes in online mode are sent through the
same channel. Disconnects become the normal modem result.

### PerryNet TCP/IP

PerryNet continues to expose its framed serial API. Guest channels 1 through 4
map to bridge channels 1 through 4. DNS, TCP open/send/receive/close, and UDP
open/send/receive/close are translated at the host boundary, while framing,
CRC handling, pull versus streamed TCP receive behavior, and guest ACK/event
generation remain in C.

PerryNet's `TIME_GET` operation does not need the relay. It uses the browser
host clock through Emscripten's C runtime, plus the monotonic tick shim.

In the WASM build, the emulated Wi-Fi-up state means that the relay connection
is ready. It does not claim to emulate an ESP8266 radio or a real SSID.

## The relay protocol

`web/perryfi-relay-protocol.js` is shared by the browser and Node relay. Every
message is a binary WebSocket frame with an eight-byte header:

| Offset | Size | Meaning |
|-------:|-----:|---------|
| 0 | 1 | Magic byte, `0x85` |
| 1 | 1 | Protocol version, currently `1` |
| 2 | 1 | Message type |
| 3 | 1 | Channel |
| 4 | 2 | Request ID, little endian |
| 6 | 2 | Payload length, little endian |

The message set is intentionally narrow: handshake, DNS, TCP open/send/close,
UDP open/send/close, operation results, received data, and close events. Status
values match PerryNet where practical, which keeps error translation simple.

The WebSocket protocol is not exposed to the PCW. It is a host transport
between the browser bridge and relay, not a third PerryFi firmware mode.

## Why a separate relay is necessary

A WebSocket server cannot make an ordinary Telnet or NTP server speak
WebSocket. The Node relay terminates the browser-safe WebSocket connection and
performs the permitted raw DNS, TCP, or UDP operation on the server side.

For local development the page and relay may run separately:

```bash
npm --prefix web/relay ci
PERRYFI_ORIGINS=http://localhost:8080 npm --prefix web/relay start
python3 -m http.server 8080 --directory web/dist
```

For an HTTPS deployment, the preferred arrangement is to reverse-proxy
`/perryfi` to the relay. The browser then uses the same-origin
`wss://host/perryfi` endpoint. A static GitHub Pages deployment cannot provide
the relay process by itself.

## Security boundary

The relay is deliberately not an unrestricted browser-to-network proxy. Its
defaults constrain both where a connection may go and how many resources a
client may consume:

- browser origins are checked exactly; without configuration, only loopback
  origins are accepted;
- an optional token can be required during the WebSocket handshake;
- outbound TCP and UDP ports use explicit allowlists;
- private, loopback, link-local, multicast, reserved, and documentation IPv4
  ranges are rejected by default;
- hostnames are resolved by the relay and the approved numeric address is used
  for the connection, preventing a second DNS lookup from bypassing the address
  policy;
- each client is limited to four channels and a small number of concurrent DNS
  requests;
- frame sizes, queues, socket buffers, UDP peers, connection time, idle time,
  and WebSocket backpressure are bounded;
- UDP sockets use ephemeral local ports, and replies are forwarded only from
  destinations that the same channel previously contacted;
- handshake deadlines and WebSocket heartbeats remove abandoned clients.

These checks belong in the relay. Browser UI validation is useful feedback,
but it is not a security boundary.

## Failure behavior

PerryFi is optional and disabled by default. If no relay is available, the
emulator still boots and runs normally; the AUX panel reports the relay as
offline and PerryNet reports Wi-Fi down or connection failure to the guest.

The browser bridge reconnects with bounded exponential backoff. Individual
requests have deadlines. If an open request times out, its relay channel is
closed before the failure is returned, so a guest retry does not leak a slot.
Queue overflow or excessive WebSocket backpressure closes the affected channel
instead of allowing unbounded memory growth.

Changing the device mode or turning PerryFi off closes channels and resets
pending work. Warm resets and PCW model changes preserve the user's AUX
selection, while the emulated device itself is reconstructed in the selected
mode.

## AUX integration

The AUX expansion panel provides:

- PerryFi board power;
- an `AT Hayes` / `TCP/IP` segmented mode selector;
- a relay endpoint field;
- a live disabled, connecting, online, offline, or error indicator.

Power, mode, and endpoint are stored in `localStorage`. A
`?perryfiRelay=...` query parameter can override the endpoint for a page load
without replacing the saved value. HTTPS pages reject insecure `ws://`
endpoints.

## Verification strategy

The implementation is tested at each boundary:

- `test_perryfi_protocol.js` verifies binary framing and decoding;
- `test_perryfi_bridge.js` verifies request correlation, queues, callbacks,
  channel mapping, and timeout cleanup with a fake WebSocket;
- `relay/test/relay.test.js` uses real local WebSocket, TCP, and UDP sockets to
  verify forwarding, origin rejection, fixed-port rejection, and filtering of
  unsolicited UDP datagrams;
- `test_wasm_expansions.js` verifies PerryFi state across resets and PCW model
  changes;
- `test_wasm_perryfi.js` drives the actual compiled PerryFi serial byte API. It
  sends real PerryNet SLIP/CRC frames through DNS, TCP, and UDP, then switches
  the same WASM module to Hayes mode and tests `AT` plus a TCP dial.

The last test bypasses scripted DART register traffic only to make the test
deterministic; it still exercises the real PerryFi firmware simulation and the
entire browser-relay transport.

## File map

| File | Responsibility |
|------|----------------|
| `src/cps.c` | Guest-visible CPS8256 and DART hardware |
| `src/perryfi.c` | Shared Hayes and PerryNet firmware simulations |
| `web/perryfi_web.c` | Narrow C-to-JavaScript Emscripten boundary |
| `web/perryfi-bridge.js` | Async requests, channel state, queues, and reconnects |
| `web/perryfi-relay-protocol.js` | Shared binary WebSocket protocol |
| `web/relay/server.js` | Restricted DNS/TCP/UDP relay |
| `web/app.js` | AUX state, persistence, and WASM control |

## Current limits

- The relay and PerryNet browser path are IPv4-only.
- TCP listener operations are not implemented.
- A separately deployed relay is required for real Internet access.
- The relay's default port allowlists intentionally cover the current Telnet,
  Gopher, HTTP/HTTPS, and NTP use cases rather than arbitrary destinations.

The design keeps those limitations outside the emulated machine. New host
transport capabilities can be added without inventing another guest API or
forking the PerryFi firmware behavior between native and WASM builds.
