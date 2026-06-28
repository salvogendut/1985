#include "leds.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t dr, dg, db;   /* idle (dark) colour */
    uint8_t br, bg, bb;   /* active (bright) colour */
} LedPalette;

/* Green for both floppies — distinguishes the PCW LEDs from 1984's red
 * FDC LEDs and gives a clear bright/dim contrast. Serial uses a split
 * RX(red) / TX(green) palette; see palette_serial_{rx,tx} below. */
static const LedPalette palette[LED_COUNT] = {
    [LED_FDC_A]   = { 18, 70, 18,   80, 255,  80 },
    [LED_FDC_B]   = { 18, 70, 18,   80, 255,  80 },
    [LED_PRINTER] = { 60, 30,  0,  255, 140,  0 },
    [LED_SERIAL]  = { 0 },   /* split — entries below */
};

static const LedPalette palette_serial_rx = { 70, 18, 18,  255,  80,  80 };
static const LedPalette palette_serial_tx = { 18, 70, 18,   80, 255,  80 };

static bool   g_enabled  [LED_COUNT];
static Uint64 g_last_ms  [LED_COUNT];   /* generic, also RX half of split LEDs */
static Uint64 g_last_ms_b[LED_COUNT];   /* TX half of split LEDs */
static bool   g_mouse_inside;
static float  g_mouse_x;
static float  g_mouse_y;
static bool   g_hover_active;
static char   g_hover_label[32];
static float  g_hover_cx;
static float  g_hover_bar_y;

typedef struct {
    float led_w;
    float led_h;
    float pad;
    float line_h;
} LedLayout;

static float renderer_scale(SDL_Renderer *r) {
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_FRect rect;
    if (SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode) &&
        lw > 0 && lh > 0 &&
        SDL_GetRenderLogicalPresentationRect(r, &rect) &&
        rect.w > 0.0f && rect.h > 0.0f) {
        float sx = rect.w / (float)lw;
        float sy = rect.h / (float)lh;
        float s = sx < sy ? sx : sy;
        if (s > 0.0f) return s;
    }
    return 1.0f;
}

static LedLayout led_layout(SDL_Renderer *r) {
    float s = renderer_scale(r);
    /* 1985 uses SDL logical presentation for the PCW image. Keep the
     * activity LEDs at 1984-style physical pixel sizes instead of letting
     * them grow with the window scale. */
    LedLayout l = {
        24.0f / s,
        10.0f / s,
        8.0f / s,
        1.0f / s,
    };
    return l;
}

static const char *led_label(LedId id) {
    switch (id) {
        case LED_FDC_A:   return "Drive A";
        case LED_FDC_B:   return "Drive B";
        case LED_PRINTER: return "Printer";
        case LED_SERIAL:  return "Serial";
        case LED_COUNT:   break;
    }
    return "";
}

static void set_hover_label(const char *label, float x, float w, float bar_y) {
    snprintf(g_hover_label, sizeof(g_hover_label), "%s", label);
    g_hover_cx = x + w * 0.5f;
    g_hover_bar_y = bar_y;
    g_hover_active = true;
}

static void update_hover(SDL_Renderer *r, int x, int y, int w, int h) {
    LedLayout l = led_layout(r);
    g_hover_active = false;
    if (!g_mouse_inside)
        return;
    if (g_mouse_x < (float)x || g_mouse_x >= (float)(x + w) ||
        g_mouse_y < (float)y || g_mouse_y >= (float)(y + h))
        return;

    int n = 0;
    float total_w = 0.0f;
    for (int i = 0; i < LED_COUNT; i++) {
        if (!g_enabled[i]) continue;
        total_w += l.led_w;
        n++;
    }
    if (n == 0)
        return;
    total_w += (float)(n - 1) * l.pad;

    float cx = (float)x + ((float)w - total_w) * 0.5f;
    float cy = (float)y + ((float)h - l.led_h) * 0.5f;
    if (g_mouse_y < cy || g_mouse_y >= cy + l.led_h)
        return;

    for (int i = 0; i < LED_COUNT; i++) {
        if (!g_enabled[i]) continue;

        if (g_mouse_x >= cx && g_mouse_x < cx + l.led_w) {
            if (i == LED_SERIAL) {
                float half_w = l.led_w * 0.5f;
                bool tx = (g_mouse_x - cx) >= half_w;
                set_hover_label(tx ? "Serial TX" : "Serial RX",
                                cx + (tx ? half_w : 0.0f), half_w, (float)y);
            } else {
                set_hover_label(led_label((LedId)i), cx, l.led_w, (float)y);
            }
            return;
        }

        cx += l.led_w + l.pad;
    }
}

void leds_set_enabled(LedId id, bool enabled) {
    if ((unsigned)id < LED_COUNT) g_enabled[id] = enabled;
}

void leds_ping(LedId id) {
    if ((unsigned)id < LED_COUNT) g_last_ms[id] = SDL_GetTicks();
}

void leds_ping_split(LedId id, bool tx) {
    if ((unsigned)id >= LED_COUNT) return;
    if (tx) g_last_ms_b[id] = SDL_GetTicks();
    else    g_last_ms  [id] = SDL_GetTicks();
}

void leds_set_mouse_position(float x, float y, bool inside) {
    g_mouse_x = x;
    g_mouse_y = y;
    g_mouse_inside = inside;
    if (!inside)
        g_hover_active = false;
}

void leds_render(SDL_Renderer *r, int x, int y, int w, int h) {
    LedLayout l = led_layout(r);
    SDL_SetRenderDrawColor(r, 18, 18, 18, 255);
    SDL_FRect bg = { (float)x, (float)y, (float)w, (float)h };
    SDL_RenderFillRect(r, &bg);

    SDL_SetRenderDrawColor(r, 50, 50, 50, 255);
    SDL_FRect line = { (float)x, (float)y, (float)w, l.line_h };
    SDL_RenderFillRect(r, &line);

    int n = 0;
    for (int i = 0; i < LED_COUNT; i++) if (g_enabled[i]) n++;
    if (n == 0) {
        g_hover_active = false;
        return;
    }

    float total_w = (float)n * l.led_w + (float)(n - 1) * l.pad;
    float cx = (float)x + ((float)w - total_w) * 0.5f;
    const float cy = (float)y + ((float)h - l.led_h) * 0.5f;

    Uint64 now = SDL_GetTicks();
    for (int i = 0; i < LED_COUNT; i++) {
        if (!g_enabled[i]) continue;
        SDL_FRect led = { cx, cy, l.led_w, l.led_h };

        if (i == LED_SERIAL) {
            /* Two halves with their own palettes, single outline. */
            const float half_w = l.led_w * 0.5f;
            for (int side = 0; side < 2; side++) {
                const LedPalette *p = side == 0 ? &palette_serial_rx
                                                : &palette_serial_tx;
                Uint64 ts = side == 0 ? g_last_ms[i] : g_last_ms_b[i];
                bool active = ts != 0 && (now - ts) < LED_GLOW_MS;
                uint8_t R = active ? p->br : p->dr;
                uint8_t G = active ? p->bg : p->dg;
                uint8_t B = active ? p->bb : p->db;
                SDL_FRect half = { cx + (float)side * half_w, cy,
                                   half_w, l.led_h };
                SDL_SetRenderDrawColor(r, R, G, B, 255);
                SDL_RenderFillRect(r, &half);
            }
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderRect(r, &led);
        } else {
            const LedPalette *p = &palette[i];
            bool active = g_last_ms[i] != 0 && (now - g_last_ms[i]) < LED_GLOW_MS;
            uint8_t R = active ? p->br : p->dr;
            uint8_t G = active ? p->bg : p->dg;
            uint8_t B = active ? p->bb : p->db;

            SDL_SetRenderDrawColor(r, R, G, B, 255);
            SDL_RenderFillRect(r, &led);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderRect(r, &led);
        }

        cx += l.led_w + l.pad;
    }

    update_hover(r, x, y, w, h);
}

void leds_render_hover(SDL_Renderer *r, int window_w, int window_h) {
    if (!g_hover_active || !g_hover_label[0])
        return;

    float wx = 0.0f, wy = 0.0f;
    if (!SDL_RenderCoordinatesToWindow(r, g_hover_cx, g_hover_bar_y, &wx, &wy))
        return;

    int logical_w = 0, logical_h = 0;
    SDL_RendererLogicalPresentation mode;
    bool restore = SDL_GetRenderLogicalPresentation(r, &logical_w, &logical_h,
                                                    &mode);
    SDL_SetRenderLogicalPresentation(r, 0, 0,
                                     SDL_LOGICAL_PRESENTATION_DISABLED);

    const int font_w = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    const int font_h = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    int text_w = (int)strlen(g_hover_label) * font_w;
    float box_w = (float)(text_w + 12);
    float box_h = (float)(font_h + 8);
    float box_x = wx - box_w * 0.5f;
    float box_y = wy - box_h - 2.0f;

    if (box_x < 2.0f) box_x = 2.0f;
    if (box_x + box_w > (float)window_w - 2.0f)
        box_x = (float)window_w - box_w - 2.0f;
    if (box_y < 2.0f) box_y = 2.0f;
    if (box_y + box_h > (float)window_h - 2.0f)
        box_y = (float)window_h - box_h - 2.0f;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 220);
    SDL_FRect bg = { box_x, box_y, box_w, box_h };
    SDL_RenderFillRect(r, &bg);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
    SDL_RenderRect(r, &bg);
    SDL_RenderDebugText(r, box_x + 6.0f, box_y + 4.0f, g_hover_label);

    if (restore)
        SDL_SetRenderLogicalPresentation(r, logical_w, logical_h, mode);
}
