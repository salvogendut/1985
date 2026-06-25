#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>
#include "types.h"
#include "config.h"

/*
 * PCW logical resolution: 720 × 256 pixels (1 bit per pixel, mono).
 * We allocate a 32 bpp XRGB8888 framebuffer of that size and let SDL
 * scale to the window. Pixel aspect on the real machine is 2:1 (tall);
 * the host window is sized 2 × (720 × 256) = 1440 × 512 by default
 * (scale=2), with `scale` 1..4 multiplying both axes.
 */

#define DISPLAY_W  720
#define DISPLAY_H  256

/* Logical presentation size. PCW pixels are 2:1 (tall) so we stretch
 * vertically into a 2×-height logical area, and reserve a strip below
 * it for the drive-activity LED bar.
 *
 * The vertical height depends on region: PAL uses all 256 framebuffer
 * lines, NTSC only the top 200 (Seasip §4.1) — and we shrink the
 * presentation accordingly so the user sees a physically shorter
 * window in NTSC mode rather than a black strip beneath the image. */
#define DISPLAY_LOGICAL_W       DISPLAY_W
#define DISPLAY_PAL_LINES       256
#define DISPLAY_NTSC_LINES      200
#define DISPLAY_PAL_SCREEN_H    (DISPLAY_PAL_LINES * 2)
#define DISPLAY_NTSC_SCREEN_H   (DISPLAY_NTSC_LINES * 2)
#define DISPLAY_LED_BAR_H       22
#define DISPLAY_PAL_LOGICAL_H   (DISPLAY_PAL_SCREEN_H + DISPLAY_LED_BAR_H)
#define DISPLAY_NTSC_LOGICAL_H  (DISPLAY_NTSC_SCREEN_H + DISPLAY_LED_BAR_H)
/* Defaults match the PAL configuration. Use Display->screen_h / .logical_h
 * for the live values. */
#define DISPLAY_SCREEN_H        DISPLAY_PAL_SCREEN_H
#define DISPLAY_LOGICAL_H       DISPLAY_PAL_LOGICAL_H

typedef struct Display {
    SDL_Window   *win;
    SDL_Renderer *renderer;
    SDL_Texture  *tex;

    u32  fb[DISPLAY_W * DISPLAY_H];

    int  scale;
    bool fullscreen;
    bool smoothing;
    bool ntsc;               /* false = PAL (256 lines), true = NTSC (200) */
    int  visible_lines;      /* live: 256 (PAL) or 200 (NTSC) */
    int  screen_h;           /* live logical height of PCW image area */
    int  logical_h;          /* live logical height incl. LED bar */
    MonoMode mono;
    bool tint_glow;          /* near-black background for any tint */

    /* Resolved phosphor colours for "lit" pixel and "background". */
    u32  fg, bg;

    /* Host-side reinterpretation of the 1bpp roller-RAM. PCW = native
     * mono (use fg/bg); CGA / EGA branch in roller.c to indexed colour
     * lookups via palette[]. See VideoMode in config.h. */
    VideoMode video_mode;
    u32  palette[16];
} Display;

int  display_init(Display *d, const Config *cfg);
void display_quit(Display *d);

void display_set_monochrome(Display *d, MonoMode m);
void display_set_tint_glow(Display *d, bool on);
void display_set_video_mode(Display *d, VideoMode v);
void display_set_smoothing(Display *d, bool smooth);
/* Switch between PAL (256 lines) and NTSC (200 lines). Resizes the
 * SDL window and updates the logical presentation so the rendered
 * image area shrinks when NTSC is selected. */
void display_set_region(Display *d, Region region);

/* Plot a colour-indexed pixel (CGA/EGA modes). Index is masked to the
 * palette size in the active video mode (4 in CGA, 16 in EGA). */
void display_put_indexed(Display *d, int x, int y, int idx);

/* Clear the framebuffer to bg. */
void display_clear(Display *d);

/* Fill the framebuffer with the foreground (lit) colour — used to
 * mimic the real PCW's all-green look at power-on before the firmware
 * programs the roller table. */
void display_fill_lit(Display *d);

/* Set a single 1bpp pixel. lit=true → fg, false → bg. */
void display_put_pixel(Display *d, int x, int y, bool lit);

/* Clear the renderer and draw the framebuffer texture into it. Call
 * this first each frame, then draw any overlays on top, then present. */
void display_draw_framebuffer(Display *d);

/* Present the current renderer contents to the window. */
void display_present(Display *d);

void display_toggle_fullscreen(Display *d);

/* Write PPM (P6) of the current framebuffer to path. Returns 0/-1. */
int  display_save_ppm(Display *d, const char *path);
