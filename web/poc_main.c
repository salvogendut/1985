/* Minimal WASM proof-of-concept main for the 1985 (PCW) core.
 *
 * Exposes a tiny C API to the browser glue:
 *   - poc_init():     boot a plain PCW 8256 from the embedded boot ROM
 *   - poc_step():     run one emulated frame (50 Hz PAL)
 *   - poc_pixels():   pointer to the 720x256 framebuffer (u32 0x00RRGGBB)
 *   - poc_key():      SDL_Scancode key down/up
 *   - poc_load_disk():  mount a .dsk into drive A from the virtual FS
 *   - poc_load_disk_b():mount a .dsk into drive B
 *   - poc_eject_disk(): eject drive A
 *   - poc_eject_disk_b(): eject drive B
 *   - poc_autorun():  queue paste command after a frame-counted boot delay
 *   - poc_set_dksound(): connect or disconnect the DK'tronics sound board
 *   - poc_set_perryfi(): connect PerryFi in Hayes or PerryNet mode
 *   - poc_disk_activity(): bit mask for active drive A/B indicators
 *   - poc_audio_*():  ring-buffer access for beeper + AY audio (mono s16)
 *
 * No SDL runtime is used — the browser reads the framebuffer and the audio
 * ring buffer directly from WASM memory.
 */
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include "pcw.h"
#include "kbd.h"
#include "disk.h"
#include "paste.h"
#include "display.h"
#include "roller.h"

static PCW g_pcw;
static Display g_display;
static Paste g_paste;
static int g_autorun_frames;
static char g_autorun_command[256];
static bool g_dksound_enabled;
static bool g_perryfi_enabled;
static PerryfiMode g_perryfi_mode = PERRYFI_MODE_HAYES;

/* ---- audio ring buffer (mono s16, 4 seconds @ 44.1 kHz) ---- */
#define AUDIO_RING_SAMPLES (44100 * 4)
static s16 g_audio_ring[AUDIO_RING_SAMPLES];
static int g_audio_w = 0;
static int g_audio_r = 0;

EMSCRIPTEN_KEEPALIVE void poc_audio_reset(void) { g_audio_w = 0; g_audio_r = 0; }
EMSCRIPTEN_KEEPALIVE int  poc_audio_avail(void) {
    return (g_audio_w - g_audio_r + AUDIO_RING_SAMPLES) % AUDIO_RING_SAMPLES;
}
EMSCRIPTEN_KEEPALIVE int  poc_audio_read_pos(void) { return g_audio_r; }
EMSCRIPTEN_KEEPALIVE short *poc_audio_buffer(void) { return g_audio_ring; }
EMSCRIPTEN_KEEPALIVE void poc_audio_advance(int n) {
    g_audio_r = (g_audio_r + n) % AUDIO_RING_SAMPLES;
}

/* ---- emulator lifecycle ---- */
static void poc_cancel_paste(void) {
    paste_free(&g_paste);
    paste_init(&g_paste);
    g_autorun_frames = 0;
    g_autorun_command[0] = '\0';
}

static int poc_init_model_impl(int model);

EMSCRIPTEN_KEEPALIVE int poc_init(void) { return poc_init_model_impl(0); }

EMSCRIPTEN_KEEPALIVE int poc_init_model(int model) { return poc_init_model_impl(model); }

static int poc_init_model_impl(int model) {
    poc_cancel_paste();
    memset(&g_display, 0, sizeof(g_display));
    g_display.fg = 0xFF72E39A;
    g_display.bg = 0xFF000000;
    g_display.mono = MONO_GREEN;
    g_display.visible_lines = DISPLAY_PAL_LINES;
    g_display.screen_h = DISPLAY_PAL_SCREEN_H;
    g_display.logical_h = DISPLAY_PAL_LOGICAL_H;
    g_display.show_status_line = true;

    PcwModel m;
    int memory_kb;
    switch (model) {
        case 0: m = PCW_MODEL_8256; memory_kb = 256; break;
        case 1: m = PCW_MODEL_8512; memory_kb = 512; break;
        case 2: m = PCW_MODEL_9512; memory_kb = 512; break;
        default: return -1;
    }
    pcw_init(&g_pcw, m, memory_kb);
    aysound_init(&g_pcw.ay, g_dksound_enabled);
    perryfi_init(&g_pcw.perryfi, g_perryfi_enabled, g_perryfi_mode);
    cps_set_present(&g_pcw.cps, g_perryfi_enabled);
    beeper_set_audio_rate(&g_pcw.beeper, PCW_AUDIO_SAMPLE_RATE);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_reset(void) {
    poc_cancel_paste();
    pcw_reset(&g_pcw);
    g_audio_w = 0;
    g_audio_r = 0;
}

EMSCRIPTEN_KEEPALIVE int poc_step(void) {
    if (g_autorun_frames > 0 && --g_autorun_frames == 0)
        paste_text(&g_paste, g_autorun_command);
    paste_tick(&g_paste, &g_pcw.kbd);
    pcw_frame(&g_pcw);
    roller_render(&g_pcw.mem, &g_pcw.asic, &g_display);

    s16 audio_buf[PCW_AUDIO_SAMPLES_FRAME];
    pcw_render_audio_frame(&g_pcw, audio_buf);
    for (int i = 0; i < PCW_AUDIO_SAMPLES_FRAME; i++) {
        int w = (g_audio_w + 1) % AUDIO_RING_SAMPLES;
        if (w == g_audio_r) break;
        g_audio_ring[g_audio_w] = audio_buf[i];
        g_audio_w = w;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE unsigned int *poc_pixels(void) {
    return g_display.fb;
}

EMSCRIPTEN_KEEPALIVE void poc_key(int scancode, int pressed) {
    kbd_sdl_key(&g_pcw.kbd, (SDL_Scancode)scancode, pressed != 0);
}

EMSCRIPTEN_KEEPALIVE int poc_load_disk(const char *path) {
    disk_eject(&g_pcw.fdc.drive[0]);
    if (disk_load(&g_pcw.fdc.drive[0], path) != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int poc_load_disk_b(const char *path) {
    disk_eject(&g_pcw.fdc.drive[1]);
    if (disk_load(&g_pcw.fdc.drive[1], path) != 0)
        return -1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE void poc_eject_disk(void) {
    disk_eject(&g_pcw.fdc.drive[0]);
}

EMSCRIPTEN_KEEPALIVE void poc_eject_disk_b(void) {
    disk_eject(&g_pcw.fdc.drive[1]);
}

EMSCRIPTEN_KEEPALIVE int poc_autorun(const char *command, int delay_frames) {
    if (!command || !command[0])
        return -1;
    size_t len = strlen(command);
    if (len > 240)
        return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)command[i];
        if (c < 0x20 || c == 0x7f || c == '"')
            return -1;
    }
    poc_cancel_paste();
    int written = snprintf(g_autorun_command, sizeof(g_autorun_command),
                           "%s", command);
    if (written < 0 || written >= (int)sizeof(g_autorun_command)) {
        g_autorun_command[0] = '\0';
        return -1;
    }
    g_autorun_frames = delay_frames > 0 ? delay_frames : 120;
    return 0;
}

/* DKsound joystick via AY port A. Bits (active-low): L=2 R=3 D=4 U=5 F=6.
 * Columns: 0=UP 1=DOWN 2=LEFT 3=RIGHT 4=FIRE1 5=FIRE2 (same layout as 1984). */
EMSCRIPTEN_KEEPALIVE void poc_joy(int col, int pressed) {
    if (col < 0 || col > 5) return;
    /* Build the active-low joystick byte from the current state. */
    static u8 joy_state = 0xFF;   /* all released = 0xFF (all bits high) */
    u8 mask;
    switch (col) {
        case 0: mask = (1 << 5); break;  /* Up    = bit 5 */
        case 1: mask = (1 << 4); break;  /* Down  = bit 4 */
        case 2: mask = (1 << 2); break;  /* Left  = bit 2 */
        case 3: mask = (1 << 3); break;  /* Right = bit 3 */
        case 4: mask = (1 << 6); break;  /* Fire1 = bit 6 */
        case 5: mask = (1 << 6); break;  /* Fire2 = bit 6 (same fire pin) */
        default: return;
    }
    if (pressed) joy_state &= ~mask;
    else         joy_state |=  mask;
    aysound_set_joystick(&g_pcw.ay, joy_state);
}

EMSCRIPTEN_KEEPALIVE int poc_set_dksound(int enabled) {
    bool present = enabled != 0;
    g_dksound_enabled = present;
    if (g_pcw.ay.present != present)
        aysound_init(&g_pcw.ay, present);
    return g_pcw.ay.present ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_dksound_enabled(void) {
    return g_pcw.ay.present ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_set_perryfi(int enabled, int mode) {
    g_perryfi_enabled = enabled != 0;
    g_perryfi_mode = mode == (int)PERRYFI_MODE_PERRYNET
        ? PERRYFI_MODE_PERRYNET : PERRYFI_MODE_HAYES;
    perryfi_shutdown(&g_pcw.perryfi);
    perryfi_init(&g_pcw.perryfi, g_perryfi_enabled, g_perryfi_mode);
    cps_set_present(&g_pcw.cps, g_perryfi_enabled);
    return g_pcw.perryfi.present ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_perryfi_enabled(void) {
    return g_pcw.perryfi.present ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int poc_perryfi_mode(void) {
    return (int)g_pcw.perryfi.mode;
}

EMSCRIPTEN_KEEPALIVE void poc_perryfi_dns_result(int status,
                                                 int a, int b, int c, int d) {
    const u8 address[4] = { (u8)a, (u8)b, (u8)c, (u8)d };
    perryfi_web_dns_result(&g_pcw.perryfi, (u8)status, address);
}

EMSCRIPTEN_KEEPALIVE void poc_perryfi_tcp_open_result(int slot, int status,
                                                       int a, int b, int c, int d,
                                                       int port) {
    const u8 address[4] = { (u8)a, (u8)b, (u8)c, (u8)d };
    perryfi_web_tcp_open_result(&g_pcw.perryfi, slot, (u8)status,
                                address, (u16)port);
}

EMSCRIPTEN_KEEPALIVE void poc_perryfi_udp_open_result(int slot, int status,
                                                       int port) {
    perryfi_web_udp_open_result(&g_pcw.perryfi, slot, (u8)status, (u16)port);
}

/* Test hooks exercise the actual serial firmware protocol without needing
 * to script guest DART register traffic. */
EMSCRIPTEN_KEEPALIVE int poc_perryfi_serial_write(const u8 *data, int len) {
    if (!data || len < 0) return -1;
    for (int i = 0; i < len; i++)
        if (!perryfi_tx_push(&g_pcw.perryfi, data[i])) return i;
    return len;
}

EMSCRIPTEN_KEEPALIVE int poc_perryfi_serial_read(u8 *data, int max_len) {
    if (!data || max_len < 0) return -1;
    int count = 0;
    while (count < max_len && perryfi_rx_pop(&g_pcw.perryfi, &data[count]))
        count++;
    return count;
}

EMSCRIPTEN_KEEPALIVE int poc_disk_motor(void) { return g_pcw.fdc.motor_on ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int poc_disk_activity(void) {
    if (!g_pcw.fdc.motor_on) return 0;
    return 1 << g_pcw.fdc.cur_unit;
}
EMSCRIPTEN_KEEPALIVE int poc_width(void)  { return DISPLAY_W; }
EMSCRIPTEN_KEEPALIVE int poc_height(void) { return DISPLAY_H; }
