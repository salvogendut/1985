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
