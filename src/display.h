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

typedef struct Display {
    SDL_Window   *win;
    SDL_Renderer *renderer;
    SDL_Texture  *tex;

    u32  fb[DISPLAY_W * DISPLAY_H];

    int  scale;
    bool fullscreen;
    bool smoothing;
    MonoMode mono;

    /* Resolved phosphor colours for "lit" pixel and "background". */
    u32  fg, bg;
} Display;

int  display_init(Display *d, const Config *cfg);
void display_quit(Display *d);

void display_set_monochrome(Display *d, MonoMode m);

/* Clear the framebuffer to bg. */
void display_clear(Display *d);

/* Set a single 1bpp pixel. lit=true → fg, false → bg. */
void display_put_pixel(Display *d, int x, int y, bool lit);

/* Upload the framebuffer to the texture and present. */
void display_present(Display *d);

void display_toggle_fullscreen(Display *d);

/* Write PPM (P6) of the current framebuffer to path. Returns 0/-1. */
int  display_save_ppm(Display *d, const char *path);
