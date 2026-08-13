# Javascript 1985

Build the browser edition with Emscripten and serve the publish directory:

    make -C web
    python3 -m http.server 8080 --directory web/dist

## Interface themes

PCW8256 is the default interface theme. It uses the grey monitor and keyboard
shells, recessed green display, drive fascia and product labels associated with
the original PCW 8256. Retro CRT, Sapporo and Sapporo Dark remain available
from the theme menu. The selection is saved in the browser; it can also be set
with `?theme=pcw8256` (theme names and labels are accepted case-insensitively).

The PCW8256 theme includes a show/hide on-screen keyboard. Its keys feed the
same PCW matrix as the physical keyboard. SHIFT, EXTRA and ALT latch for the
next non-modifier key, allowing the paired PCW function keys to be selected.
The packaged page also includes the 1985 application icon as part of the
monitor fascia.

## Expansion bay

The AUX key opens a left-side expansion bay. DK'sound and PerryFi can be
connected and disconnected while the emulator is running. PerryFi offers the
same AT Hayes and PerryNet TCP/IP device models as native 1985. Expansion
state, PerryFi mode, and relay endpoint are saved in the browser and survive
warm resets and machine changes.

Browsers cannot open arbitrary TCP or UDP sockets. The WASM PerryFi therefore
uses the restricted WebSocket relay in `web/relay`; the emulated CPS8256,
Hayes parser, and PerryNet SLIP/CRC firmware model still run inside WASM.

For local use, install and start the relay:

```bash
npm --prefix web/relay ci
PERRYFI_ORIGINS=http://localhost:8080 npm --prefix web/relay start
```

Then set **AUX > PerryFi > Relay endpoint** to
`ws://127.0.0.1:1985/perryfi`. It can also be supplied without changing the
saved value:

```text
http://localhost:8080/?perryfiRelay=ws%3A%2F%2F127.0.0.1%3A1985%2Fperryfi
```

For an HTTPS deployment, reverse-proxy `/perryfi` to the relay and set
`PERRYFI_ORIGINS` to the exact public page origin. The default browser endpoint
is then the same-origin `wss://.../perryfi`. A separately hosted relay can be
entered in AUX, but it must use `wss://`; a static GitHub Pages deployment
cannot provide the relay process itself.

Relay configuration is supplied through environment variables:

| Variable | Default | Purpose |
|----------|---------|---------|
| `PERRYFI_RELAY_HOST` | `127.0.0.1` | Listen address |
| `PERRYFI_RELAY_PORT` | `1985` | Listen port |
| `PERRYFI_RELAY_PATH` | `/perryfi` | WebSocket path |
| `PERRYFI_ORIGINS` | local origins only | Comma-separated browser origins |
| `PERRYFI_TOKEN` | unset | Optional token, supplied as `?token=...` on the endpoint |
| `PERRYFI_TCP_PORTS` | `23,70,80,443,2323` | Permitted outbound TCP ports |
| `PERRYFI_UDP_PORTS` | `123` | Permitted outbound UDP ports |

The relay resolves hostnames itself and connects to the approved numeric
address, rejects private, loopback, link-local, multicast, and documentation
ranges, limits each browser to four channels, and bounds payloads, queued
bytes, connection time, and idle time. UDP uses ephemeral local ports and only
forwards replies from destinations previously contacted by that channel. Keep
those restrictions in place when publishing it on the Internet.

## Server-hosted media

Media URLs are resolved relative to the page URL. `disk` mounts Drive A and
`diskb` mounts Drive B. Both can be supplied together:

    http://localhost:8080/?disk=media/system.dsk&diskb=media/data.dsk

When Drive A is supplied, both images are mounted before the PCW resets and
boots automatically from Drive A. Drive B is secondary media and is never used
as the boot disk. An optional `autorun` command can be injected after the same
boot delay used by native 1985:

    http://localhost:8080/?disk=media/system.dsk&diskb=media/data.dsk&autorun=GB

Parameter values containing spaces or other reserved characters must be URL
encoded. Media must be available over HTTP or HTTPS. Cross-origin servers must
permit the request with CORS headers.

The fetched image lives in the browser's in-memory filesystem. Guest writes
are not uploaded to the server.
