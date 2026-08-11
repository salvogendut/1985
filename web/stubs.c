/* Stubs for host-dependent modules excluded from the WASM POC build.
 *
 * Replaces SDL-dependent display, LEDs, overlay, monitor, printer,
 * serial, CPS, PerryFi, pilot, web GUI, video capture, Multilink,
 * and PCW mouse with no-op implementations that satisfy the linker.
 *
 * Only symbols actually called from the compiled core modules
 * (pcw.c, disk.c, roller.c, asic.c) are stubbed here.
 */
#include <string.h>
#include "pcw.h"
#include "display.h"
#include "notify.h"
#include "leds.h"
#include "printer.h"
#include "serial.h"
#include "cps.h"
#include "perryfi.h"
#include "multilink.h"
#include "pcwmouse.h"

int g_debug_enabled = 0;
int cpc_frame_count  = 0;
#include "display.h"
#include "notify.h"
#include "leds.h"
#include "printer.h"
#include "serial.h"
#include "cps.h"
#include "perryfi.h"
#include "multilink.h"
#include "pcwmouse.h"

/* ---- display (SDL-free subset used by roller.c + asic.c) ---- */
void display_clear(Display *d) {
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++) d->fb[i] = d->bg;
}
void display_fill_lit(Display *d) {
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++) d->fb[i] = d->fg;
}
void display_set_monochrome(Display *d, MonoMode m) { (void)d; (void)m; }
void display_set_video_mode(Display *d, VideoMode v) { (void)d; (void)v; }
void display_set_smoothing(Display *d, bool smooth) { (void)d; (void)smooth; }
void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue) {
    (void)d; (void)enabled; (void)scanlines; (void)brightness;
    (void)contrast; (void)red; (void)green; (void)blue;
}
void display_set_region(Display *d, Region region) { (void)d; (void)region; }
void display_set_status_line(Display *d, bool shown) { (void)d; (void)shown; }
void display_set_tint_glow(Display *d, bool on) { (void)d; (void)on; }
void display_put_pixel(Display *d, int x, int y, bool lit) { (void)d; (void)x; (void)y; (void)lit; }
void display_put_indexed(Display *d, int x, int y, int idx) { (void)d; (void)x; (void)y; (void)idx; }
void display_draw_framebuffer(Display *d) { (void)d; }
void display_present(Display *d) { (void)d; }
int display_init(Display *d, const Config *cfg) { (void)d; (void)cfg; return 0; }
void display_quit(Display *d) { (void)d; }
void display_toggle_fullscreen(Display *d) { (void)d; }
int display_save_ppm(Display *d, const char *path) { (void)d; (void)path; return -1; }
u32 display_hash(Display *d) { (void)d; return 0; }
void display_copy_visible(Display *d, u32 *dst) { (void)d; (void)dst; }
bool display_changed_rect(Display *d, u32 *prev, int *x, int *y, int *w, int *h) {
    (void)d; (void)prev; (void)x; (void)y; (void)w; (void)h; return false;
}
bool display_save_crop_ppm(Display *d, const char *path, int x, int y,
                           int w, int h, int scale) {
    (void)d; (void)path; (void)x; (void)y; (void)w; (void)h; (void)scale; return false;
}

/* ---- LEDs ---- */
void leds_set_enabled(LedId id, bool enabled) { (void)id; (void)enabled; }
void leds_ping(LedId id) { (void)id; }
void leds_ping_split(LedId id, bool tx) { (void)id; (void)tx; }
void leds_set_mouse_position(float x, float y, bool inside) { (void)x; (void)y; (void)inside; }

/* ---- notify (disk.c calls notify_post on load errors) ---- */
#include <stdarg.h>
void notify_post(const char *fmt, ...) { (void)fmt; }
void notify_init(void) {}
void notify_set_mode(NotifyMode mode) { (void)mode; }
void notify_tick(int dt_ms) { (void)dt_ms; }
void notify_render(struct SDL_Renderer *r, int win_w, int win_h) {
    (void)r; (void)win_w; (void)win_h;
}

/* ---- printer (pcw_init calls printer_init) ---- */
void printer_init(Printer *p) { (void)p; }
void printer_shutdown(Printer *p) { (void)p; }
void printer_set_pdf_output_dir(Printer *p, const char *dir) { (void)p; (void)dir; }
void printer_set_pdf_enabled(Printer *p, bool enabled) { (void)p; (void)enabled; }
void printer_set_sink(Printer *p, PrintSink sink) { (void)p; (void)sink; }
void printer_set_kind(Printer *p, PrinterKind kind) { (void)p; (void)kind; }
u8 printer_read(Printer *p, u8 port) { (void)p; (void)port; return 0xFF; }
void printer_write(Printer *p, u8 port, u8 val) { (void)p; (void)port; (void)val; }
void printer_write_centronics(Printer *p, u8 val) { (void)p; (void)val; }
void printer_tick(Printer *p) { (void)p; }

/* ---- serial (CPS init back-reference) ---- */
void serial_init(Serial *s, bool enable, const char *backend,
                 int tcp_port, const char *pty_link_path) {
    (void)s; (void)enable; (void)backend; (void)tcp_port; (void)pty_link_path;
}
void serial_shutdown(Serial *s) { (void)s; }
void serial_poll(Serial *s) { (void)s; }
bool serial_rx_pop(Serial *s, u8 *out) { (void)s; (void)out; return false; }
bool serial_tx_push(Serial *s, u8 b) { (void)s; (void)b; return false; }
bool serial_rx_has(const Serial *s) { (void)s; return false; }

/* ---- cps (pcw_init calls cps_init; pcw_reset calls cps_reset) ---- */
void cps_init(Cps *c, bool present, struct Serial *serial,
              struct Perryfi *perryfi, struct Printer *printer) {
    (void)c; (void)present; (void)serial; (void)perryfi; (void)printer;
}
void cps_reset(Cps *c) { (void)c; }
void cps_set_present(Cps *c, bool present) { (void)c; (void)present; }
u8 cps_read(Cps *c, u8 lo) { (void)c; (void)lo; return 0xFF; }
void cps_write(Cps *c, u8 lo, u8 val) { (void)c; (void)lo; (void)val; }

/* ---- perryfi (pcw_init calls perryfi_init) ---- */
void perryfi_init(Perryfi *p, bool enable, PerryfiMode mode) {
    (void)p; (void)enable; (void)mode;
}
void perryfi_shutdown(Perryfi *p) { (void)p; }
void perryfi_poll(Perryfi *p) { (void)p; }
bool perryfi_rx_pop(Perryfi *p, u8 *out) { (void)p; (void)out; return false; }
bool perryfi_tx_push(Perryfi *p, u8 b) { (void)p; (void)b; return false; }
bool perryfi_rx_has(const Perryfi *p) { (void)p; return false; }

/* ---- multilink (pcw_init calls multilink_init; pcw_reset calls multilink_reset) ---- */
void multilink_init(Multilink *m) { (void)m; }
void multilink_reset(Multilink *m) { (void)m; }
void multilink_set_present(Multilink *m, bool present) { (void)m; (void)present; }
u8 multilink_read(Multilink *m, u8 port) { (void)m; (void)port; return 0x00; }
void multilink_write(Multilink *m, u8 port, u8 val) { (void)m; (void)port; (void)val; }

/* ---- pcwmouse (pcw_init calls pcwmouse_init; pcw_reset calls pcwmouse_reset) ---- */
void pcwmouse_init(PcwMouse *m, bool present, MouseType type) {
    (void)m; (void)present; (void)type;
}
void pcwmouse_reset(PcwMouse *m) { (void)m; }
void pcwmouse_configure(PcwMouse *m, bool present, MouseType type) {
    (void)m; (void)present; (void)type;
}
void pcwmouse_add_motion(PcwMouse *m, float dx, float dy) { (void)m; (void)dx; (void)dy; }
void pcwmouse_set_button(PcwMouse *m, int button, bool down) { (void)m; (void)button; (void)down; }
void pcwmouse_clear_input(PcwMouse *m) { (void)m; }
bool pcwmouse_handles_port(const PcwMouse *m, u8 lo) { (void)m; (void)lo; return false; }
u8 pcwmouse_read(PcwMouse *m, u8 lo) { (void)m; (void)lo; return 0xFF; }
void pcwmouse_write(PcwMouse *m, u8 lo, u8 val) { (void)m; (void)lo; (void)val; }
void pcwmouse_overlay_kbd(PcwMouse *m, u8 *kbd_window) { (void)m; (void)kbd_window; }

/* ---- SDL audio stream stubs ---- */
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid,
                                           const SDL_AudioSpec *spec,
                                           SDL_AudioStreamCallback callback,
                                           void *userdata) {
    (void)devid; (void)spec; (void)callback; (void)userdata;
    return NULL;
}
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream) { (void)stream; return false; }
bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len) {
    (void)stream; (void)buf; (void)len; return false;
}
int SDL_GetAudioStreamQueued(SDL_AudioStream *stream) { (void)stream; return 0; }
void SDL_DestroyAudioStream(SDL_AudioStream *stream) { (void)stream; }
const char *SDL_GetError(void) { return ""; }
const char *SDL_GetBasePath(void) { return ""; }
