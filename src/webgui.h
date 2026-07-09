/* webgui.h — embedded Web GUI: an HTTP server that serves the emulator
 * screen and controls to a browser on the LAN. Ported from 1984.
 *
 * GET  /        self-contained HTML page (video + keyboard/mouse/paste)
 * GET  /stream  multipart/x-mixed-replace stream of GIF frames
 * GET  /audio   streaming WAV (44.1 kHz mono s16)
 * GET  /status  {"mouse":bool} — machine facts for the page
 * POST /key     ?c=<SDL scancode name>&d=1|0[&m=1]   key down/up (+shift mod)
 * POST /mouse   ?dx=&dy= | ?b=0..2&d=1|0             PCW mouse motion/buttons
 * POST /paste   body = text typed via the paste queue
 * POST /reset   warm reset
 *
 * Single-threaded and non-blocking: webgui_poll() runs once per frame
 * before pcw_frame() (input lands on the next emulated frame),
 * webgui_frame() once per frame after it (encodes and pushes video).
 * Binds 0.0.0.0 with no authentication — LAN-trust only.
 */
#pragma once
#include <stdbool.h>
#include "pcw.h"
#include "display.h"
#include "paste.h"

/* Capture machine pointers once; opens no socket. The display is a
 * separate object from the PCW machine struct in this codebase. */
void webgui_init(PCW *pcw, Display *disp, Paste *paste);

/* Log server lifecycle and client activity to stderr (listen URLs,
 * viewer connect/disconnect). Enabled by --web for journald/log
 * capture in headless service use; off by default (toasts only). */
void webgui_set_log(bool on);

/* Start listening on 0.0.0.0:port. Posts a notify toast either way;
 * returns false if the socket could not be opened. */
bool webgui_start(int port);

/* Close the listener and all clients, release any web-held mouse
 * buttons. Safe to call when not running. */
void webgui_stop(void);

bool webgui_active(void);
int  webgui_port(void);       /* port currently listening on, 0 if inactive */

void webgui_poll(void);       /* per frame BEFORE pcw_frame */
void webgui_audio(const s16 *samples, int frames);
void webgui_frame(void);      /* per frame AFTER pcw_frame */
