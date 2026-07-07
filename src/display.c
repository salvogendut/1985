#include "display.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float render_scale(Display *d) {
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_FRect rect;
    if (d->renderer &&
        SDL_GetRenderLogicalPresentation(d->renderer, &lw, &lh, &mode) &&
        lw > 0 && lh > 0 &&
        SDL_GetRenderLogicalPresentationRect(d->renderer, &rect) &&
        rect.w > 0.0f && rect.h > 0.0f) {
        float sx = rect.w / (float)lw;
        float sy = rect.h / (float)lh;
        float s = sx < sy ? sx : sy;
        if (s > 0.0f) return s;
    }
    return 1.0f;
}

static unsigned adjust_component(unsigned c, int brightness, int contrast,
                                 int gain) {
    int v = 128 + (((int)c - 128) * contrast + 50) / 100;
    v = (v * brightness + 50) / 100;
    v = (v * gain + 50) / 100;
    return (unsigned)clamp_int(v, 0, 255);
}

static const u32 *display_crt_pixels(Display *d) {
    if (!d->crt_enabled ||
        (d->crt_brightness == DISPLAY_CRT_BRIGHTNESS_DEFAULT &&
         d->crt_contrast == DISPLAY_CRT_CONTRAST_DEFAULT &&
         d->crt_red == DISPLAY_CRT_RGB_DEFAULT &&
         d->crt_green == DISPLAY_CRT_RGB_DEFAULT &&
         d->crt_blue == DISPLAY_CRT_RGB_DEFAULT))
        return d->fb;
    if (!d->crt_fb)
        return d->fb;

    /* The brightness/contrast/gain chain only depends on the 8-bit
     * input component, so bake it into three per-channel tables and
     * rebuild them when a slider moves — not 3×184,320 times a frame. */
    static u8  lut_r[256], lut_g[256], lut_b[256];
    static int lut_bri = -1, lut_con = -1, lut_red = -1,
               lut_grn = -1, lut_blu = -1;
    if (d->crt_brightness != lut_bri || d->crt_contrast != lut_con ||
        d->crt_red != lut_red || d->crt_green != lut_grn ||
        d->crt_blue != lut_blu) {
        for (int c = 0; c < 256; c++) {
            lut_r[c] = (u8)adjust_component((unsigned)c, d->crt_brightness,
                                            d->crt_contrast, d->crt_red);
            lut_g[c] = (u8)adjust_component((unsigned)c, d->crt_brightness,
                                            d->crt_contrast, d->crt_green);
            lut_b[c] = (u8)adjust_component((unsigned)c, d->crt_brightness,
                                            d->crt_contrast, d->crt_blue);
        }
        lut_bri = d->crt_brightness; lut_con = d->crt_contrast;
        lut_red = d->crt_red; lut_grn = d->crt_green; lut_blu = d->crt_blue;
    }

    int n = DISPLAY_W * DISPLAY_H;
    for (int i = 0; i < n; i++) {
        u32 px = d->fb[i];
        d->crt_fb[i] = (px & 0xFF000000u)
                     | ((u32)lut_r[(px >> 16) & 0xFF] << 16)
                     | ((u32)lut_g[(px >> 8)  & 0xFF] << 8)
                     |  (u32)lut_b[ px        & 0xFF];
    }
    return d->crt_fb;
}

static u32 mono_lit(MonoMode m) {
    switch (m) {
        case MONO_AMBER: return 0xFFBF00;
        case MONO_WHITE: return 0xFFFFFF;
        case MONO_GREEN: return 0x00FF70;
        case MONO_OFF:
        default:         return 0xFFFFFF;   /* paper white when no tint */
    }
}

static u32 mono_dim(MonoMode m, bool glow) {
    /* Glow drops the background close to pitch-black so the lit
     * pixels really pop, matching a CRT-with-the-brightness-up
     * look. Otherwise stay at 1/8 of the lit colour for a slight
     * bloom feel rather than full black. */
    u32 lit = mono_lit(m);
    int divisor = glow ? 64 : 8;
    u8 r = (u8)(((lit >> 16) & 0xFF) / divisor);
    u8 g = (u8)(((lit >> 8)  & 0xFF) / divisor);
    u8 b = (u8)(( lit        & 0xFF) / divisor);
    return ((u32)r << 16) | ((u32)g << 8) | b;
}

void display_set_monochrome(Display *d, MonoMode m) {
    d->mono = m;
    d->fg = mono_lit(m);
    d->bg = mono_dim(m, d->tint_glow);
}

void display_set_tint_glow(Display *d, bool on) {
    d->tint_glow = on;
    /* PCW video mode is the only one that uses the dim bg; the
     * colour modes force black already, so refreshing them is a
     * no-op for the bg but still cheap and keeps the code uniform. */
    if (d->video_mode == VIDEO_PCW) d->bg = mono_dim(d->mono, on);
}

/* Palettes lifted from ZesarUX's pcw_rgb24_full_table. CGA1 picks
 * the low-intensity palette-0 (black/green/red/brown), CGA2 the
 * high-intensity palette-1 (black/cyan/magenta/white); EGA uses
 * the classic 16-colour IBM palette. */
static void load_cga1_palette(u32 *p) {
    p[0] = 0x000000;
    p[1] = 0x00AA00;
    p[2] = 0xAA0000;
    p[3] = 0xAA5500;
}
static void load_cga2_palette(u32 *p) {
    p[0] = 0x000000;
    p[1] = 0x55FFFF;
    p[2] = 0xFF55FF;
    p[3] = 0xFFFFFF;
}
static void load_ega_palette(u32 *p) {
    static const u32 ega[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
    };
    for (int i = 0; i < 16; i++) p[i] = ega[i];
}

void display_set_video_mode(Display *d, VideoMode v) {
    d->video_mode = v;
    /* Background is forced black in colour modes so the saturated
     * palette entries pop against an unlit pixel. Mono modes keep
     * their CRT-bloom dim background from display_set_monochrome(). */
    switch (v) {
        case VIDEO_CGA1: load_cga1_palette(d->palette); d->bg = 0x000000; break;
        case VIDEO_CGA2: load_cga2_palette(d->palette); d->bg = 0x000000; break;
        case VIDEO_EGA:  load_ega_palette(d->palette);  d->bg = 0x000000; break;
        case VIDEO_PCW:
        default:         d->bg = mono_dim(d->mono, d->tint_glow);          break;
    }
}

void display_put_indexed(Display *d, int x, int y, int idx) {
    if (x < 0 || x >= DISPLAY_W || y < 0 || y >= DISPLAY_H) return;
    int mask = (d->video_mode == VIDEO_EGA) ? 15 : 3;
    d->fb[y * DISPLAY_W + x] = d->palette[idx & mask];
}

int display_init(Display *d, const Config *cfg) {
    memset(d, 0, sizeof(*d));
    d->crt_fb = malloc(DISPLAY_W * DISPLAY_H * sizeof(*d->crt_fb));
    if (!d->crt_fb) {
        fprintf(stderr, "display: CRT framebuffer allocation failed\n");
        return -1;
    }
    d->scale      = cfg->scale > 0 ? cfg->scale : 2;
    d->fullscreen = cfg->fullscreen;
    d->smoothing  = cfg->fullscreen_smoothing;
    d->tint_glow = cfg->tint_glow;
    d->show_status_line = cfg->show_status_line;
    d->ntsc          = (cfg->region == REGION_NTSC);
    d->visible_lines = d->ntsc ? DISPLAY_NTSC_LINES    : DISPLAY_PAL_LINES;
    d->screen_h      = d->ntsc ? DISPLAY_NTSC_SCREEN_H : DISPLAY_PAL_SCREEN_H;
    d->logical_h     = d->ntsc ? DISPLAY_NTSC_LOGICAL_H: DISPLAY_PAL_LOGICAL_H;
    display_set_monochrome(d, cfg->monochrome);
    display_set_video_mode(d, cfg->video_mode);
    display_set_crt(d, cfg->real_crt, cfg->crt_scanlines,
                    cfg->crt_brightness, cfg->crt_contrast,
                    cfg->crt_red, cfg->crt_green, cfg->crt_blue);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    int win_w = DISPLAY_LOGICAL_W * d->scale;
    int win_h = d->logical_h     * d->scale;

    SDL_WindowFlags wf = SDL_WINDOW_RESIZABLE;
    if (d->fullscreen) wf |= SDL_WINDOW_FULLSCREEN;

    /* Title reflects the active model — 1985 supports 8256/8512/9512. */
    const char *title =
        (cfg->model == PCW_MODEL_8512) ? "1985 — Amstrad PCW 8512"
      : (cfg->model == PCW_MODEL_9512) ? "1985 — Amstrad PCW 9512"
      : "1985 — Amstrad PCW 8256";
    d->win = SDL_CreateWindow(title, win_w, win_h, wf);
    if (!d->win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    d->renderer = SDL_CreateRenderer(d->win, NULL);
    if (!d->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderLogicalPresentation(d->renderer,
                                     DISPLAY_LOGICAL_W, d->logical_h,
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

void display_set_smoothing(Display *d, bool smooth) {
    d->smoothing = smooth;
    if (d->tex)
        SDL_SetTextureScaleMode(d->tex,
            smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void display_set_crt(Display *d, bool enabled, int scanlines, int brightness,
                     int contrast, int red, int green, int blue) {
    d->crt_enabled = enabled;
    d->crt_scanlines = clamp_int(scanlines, 0, 95);
    d->crt_brightness = clamp_int(brightness, 50, 100);
    d->crt_contrast = clamp_int(contrast, 50, 150);
    d->crt_red = clamp_int(red, 50, 150);
    d->crt_green = clamp_int(green, 50, 150);
    d->crt_blue = clamp_int(blue, 50, 150);
}

void display_set_region(Display *d, Region region) {
    bool ntsc = (region == REGION_NTSC);
    if (d->ntsc == ntsc) return;
    d->ntsc          = ntsc;
    d->visible_lines = ntsc ? DISPLAY_NTSC_LINES    : DISPLAY_PAL_LINES;
    d->screen_h      = ntsc ? DISPLAY_NTSC_SCREEN_H : DISPLAY_PAL_SCREEN_H;
    d->logical_h     = ntsc ? DISPLAY_NTSC_LOGICAL_H: DISPLAY_PAL_LOGICAL_H;
    if (d->renderer)
        SDL_SetRenderLogicalPresentation(d->renderer,
                                         DISPLAY_LOGICAL_W, d->logical_h,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (d->win && !d->fullscreen)
        SDL_SetWindowSize(d->win,
                          DISPLAY_LOGICAL_W * d->scale,
                          d->logical_h     * d->scale);
}

void display_quit(Display *d) {
    if (d->tex)      SDL_DestroyTexture(d->tex);
    if (d->renderer) SDL_DestroyRenderer(d->renderer);
    if (d->win)      SDL_DestroyWindow(d->win);
    free(d->crt_fb);
    SDL_Quit();
    memset(d, 0, sizeof(*d));
}

void display_clear(Display *d) {
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++) d->fb[i] = d->bg;
}

void display_fill_lit(Display *d) {
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; i++) d->fb[i] = d->fg;
}

void display_put_pixel(Display *d, int x, int y, bool lit) {
    if (x < 0 || x >= DISPLAY_W || y < 0 || y >= DISPLAY_H) return;
    d->fb[y * DISPLAY_W + x] = lit ? d->fg : d->bg;
}

void display_draw_framebuffer(Display *d) {
    SDL_UpdateTexture(d->tex, NULL, display_crt_pixels(d), DISPLAY_W * 4);
    SDL_SetRenderDrawColor(d->renderer, 0, 0, 0, 255);
    SDL_RenderClear(d->renderer);

    /* NTSC: sample only the top 200 framebuffer rows. PAL: full 256.
     * The dst rect is the shrunk image area; SDL letterboxes the
     * whole window cleanly because logical_h was set to match.
     * With the status line hidden, drop the bottom 8 guest scanlines
     * (CP/M's status row) and let the remainder fill the same image
     * area — exactly what a real tube's overscan does. */
    int vis = d->visible_lines - (d->show_status_line ? 0 : 8);
    SDL_FRect src = { 0.0f, 0.0f,
                      (float)DISPLAY_W, (float)vis };
    SDL_FRect dst = { 0.0f, 0.0f,
                      (float)DISPLAY_LOGICAL_W, (float)d->screen_h };
    SDL_RenderTexture(d->renderer, d->tex, &src, &dst);

    if (d->crt_enabled && d->crt_scanlines > 0) {
        float s = render_scale(d);
        float line_h = 1.0f / s;
        float step = 2.0f / s;
        Uint8 alpha = (Uint8)((d->crt_scanlines * 255 + 50) / 100);
        SDL_SetRenderDrawBlendMode(d->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(d->renderer, 0, 0, 0, alpha);
        for (float y = line_h; y < (float)d->screen_h; y += step) {
            SDL_FRect scan = { 0.0f, y, (float)DISPLAY_LOGICAL_W, line_h };
            SDL_RenderFillRect(d->renderer, &scan);
        }
    }

    leds_render(d->renderer, 0, d->screen_h + DISPLAY_STRIP_H,
                DISPLAY_LOGICAL_W, DISPLAY_LED_BAR_H);
}

void display_set_status_line(Display *d, bool shown) {
    d->show_status_line = shown;
}

void display_present(Display *d) {
    SDL_RenderPresent(d->renderer);
}

void display_toggle_fullscreen(Display *d) {
    d->fullscreen = !d->fullscreen;
    SDL_SetWindowFullscreen(d->win, d->fullscreen);
}

int display_save_ppm(Display *d, const char *path) {
    /* PCW framebuffer is 720x256 (W:H = 2.81:1) but the real PCW monitor
     * is 4:3, so each pixel is roughly twice as tall as wide. Stretch
     * the saved PPM to 720x540 (4:3) using nearest-neighbour line
     * replication so the screenshot has the correct aspect ratio. */
    enum { OUT_W = DISPLAY_W, OUT_H = (DISPLAY_W * 3) / 4 };  /* 720x540 */
    FILE *f = fopen(path, "wb");
    if (!f) { perror("display_save_ppm"); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", OUT_W, OUT_H);
    for (int y = 0; y < OUT_H; y++) {
        int src_y = (y * DISPLAY_H) / OUT_H;
        const u32 *row = &d->fb[src_y * DISPLAY_W];
        for (int x = 0; x < OUT_W; x++) {
            u32 px = row[x];
            u8 rgb[3] = { (u8)(px >> 16), (u8)(px >> 8), (u8)px };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    return 0;
}
