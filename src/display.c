#include "display.h"
#include <stdio.h>
#include <string.h>

static u32 mono_lit(MonoMode m) {
    switch (m) {
        case MONO_AMBER: return 0xFFBF00;
        case MONO_WHITE: return 0xFFFFFF;
        case MONO_GREEN: return 0x80FF80;
        case MONO_OFF:
        default:         return 0xFFFFFF;   /* paper white when no tint */
    }
}

static u32 mono_dim(MonoMode m) {
    /* 1/8 of the lit colour — gives a slight CRT-bloom feel rather
     * than pitch black, matching the dark green of the GT65. */
    u32 lit = mono_lit(m);
    u8 r = (u8)((((lit >> 16) & 0xFF) * 1) / 8);
    u8 g = (u8)((((lit >> 8)  & 0xFF) * 1) / 8);
    u8 b = (u8)(( (lit        & 0xFF) * 1) / 8);
    return ((u32)r << 16) | ((u32)g << 8) | b;
}

void display_set_monochrome(Display *d, MonoMode m) {
    d->mono = m;
    d->fg = mono_lit(m);
    d->bg = mono_dim(m);
}

int display_init(Display *d, const Config *cfg) {
    memset(d, 0, sizeof(*d));
    d->scale      = cfg->scale > 0 ? cfg->scale : 2;
    d->fullscreen = cfg->fullscreen;
    d->smoothing  = cfg->fullscreen_smoothing;
    display_set_monochrome(d, cfg->monochrome);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    int win_w = DISPLAY_W * d->scale;
    int win_h = DISPLAY_H * d->scale;

    SDL_WindowFlags wf = SDL_WINDOW_RESIZABLE;
    if (d->fullscreen) wf |= SDL_WINDOW_FULLSCREEN;

    d->win = SDL_CreateWindow("1985 — Amstrad PCW 8256", win_w, win_h, wf);
    if (!d->win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    d->renderer = SDL_CreateRenderer(d->win, NULL);
    if (!d->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderLogicalPresentation(d->renderer, DISPLAY_W, DISPLAY_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);

    d->tex = SDL_CreateTexture(d->renderer, SDL_PIXELFORMAT_XRGB8888,
                               SDL_TEXTUREACCESS_STREAMING,
                               DISPLAY_W, DISPLAY_H);
    if (!d->tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetTextureScaleMode(d->tex,
        d->smoothing ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    display_clear(d);
    return 0;
}

void display_quit(Display *d) {
    if (d->tex)      SDL_DestroyTexture(d->tex);
    if (d->renderer) SDL_DestroyRenderer(d->renderer);
    if (d->win)      SDL_DestroyWindow(d->win);
    SDL_Quit();
    memset(d, 0, sizeof(*d));
}

void display_clear(Display *d) {
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++) d->fb[i] = d->bg;
}

void display_put_pixel(Display *d, int x, int y, bool lit) {
    if (x < 0 || x >= DISPLAY_W || y < 0 || y >= DISPLAY_H) return;
    d->fb[y * DISPLAY_W + x] = lit ? d->fg : d->bg;
}

void display_present(Display *d) {
    SDL_UpdateTexture(d->tex, NULL, d->fb, DISPLAY_W * 4);
    SDL_SetRenderDrawColor(d->renderer, 0, 0, 0, 255);
    SDL_RenderClear(d->renderer);
    SDL_RenderTexture(d->renderer, d->tex, NULL, NULL);
    SDL_RenderPresent(d->renderer);
}

void display_toggle_fullscreen(Display *d) {
    d->fullscreen = !d->fullscreen;
    SDL_SetWindowFullscreen(d->win, d->fullscreen);
}

int display_save_ppm(Display *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror("display_save_ppm"); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", DISPLAY_W, DISPLAY_H);
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++) {
        u32 px = d->fb[i];
        u8 rgb[3] = { (u8)(px >> 16), (u8)(px >> 8), (u8)px };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}
