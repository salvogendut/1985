/* websvc.h — Web Service: the multi-session "emulator as a service" HTTP
 * server started by --web. Ported from 1984.
 *
 * Unlike the Web GUI (webgui.h, used by the F9 overlay toggle — one shared
 * PCW mirrored to every browser), each distinct browser (cookie jar) here
 * gets its own, fully isolated PCW instance:
 *
 * GET  /        auto-creates a session on first visit (Set-Cookie), or
 *               re-serves the page for an existing session's cookie
 * GET  /stream  multipart/x-mixed-replace stream of GIF frames
 * GET  /audio   streaming WAV (44.1 kHz mono s16)
 * POST /key     ?c=<SDL scancode name>&d=1|0&m=1(shift)   key down/up
 * POST /mouse   ?dx=&dy=&b=&d=                            relative mouse
 * POST /paste   body = text typed via the paste queue
 * POST /disk    ?drive=0|1&name=<file.dsk>     body = raw .dsk bytes
 * POST /session/config   body = a 1985.conf — rebuilds this session
 * POST /reset   machine reset
 *
 * Every non-"/" endpoint requires a valid session cookie; a missing or
 * stale one gets 400 (a normal browser always hits "/" first).
 *
 * Sessions always boot from config_defaults() — never the host user's
 * real ~/.config/1985/1985.conf. Capped at WS_MAX_SESSIONS concurrent
 * instances; a session with zero attached streaming clients for longer
 * than WS_IDLE_TIMEOUT_MS is destroyed to free the slot. Binds 0.0.0.0
 * with no authentication, same LAN-trust model as the Web GUI — session
 * isolation means a new browser gets a new *machine*, not new
 * *credentials*.
 *
 * Self-contained: owns its own SDL_Init, listening socket, and ~50 Hz
 * pacing loop. Does not touch webgui.c/webgui.h (the overlay-toggle path)
 * at all.
 */
#pragma once

/* Runs until Ctrl-C/SIGTERM. Returns a process exit code. */
int websvc_run(int port);
