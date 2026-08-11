# Javascript 1985

Build the browser edition with Emscripten and serve the publish directory:

    make -C web
    python3 -m http.server 8080 --directory web/dist

## Server-hosted media

Media URLs are resolved relative to the page URL. A disk can be mounted in
drive A at startup:

    http://localhost:8080/?disk=media/thisdisk.dsk

Add `autorun` to reset the PCW after mounting and inject a command after the
same boot delay used by native 1985:

    http://localhost:8080/?disk=media/thisdisk.dsk&autorun=cpm

Parameter values containing spaces or other reserved characters must be URL
encoded. Media must be available over HTTP or HTTPS. Cross-origin servers must
permit the request with CORS headers.

The fetched image lives in the browser's in-memory filesystem. Guest writes
are not uploaded to the server.
