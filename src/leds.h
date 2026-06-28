#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

/* Drive-activity LED bar rendered below the PCW screen.
 *
 * PCW floppy drives are green LEDs in this emulator (the real PCW used
 * a single red LED above the drive, but green reads better against the
 * dark bar and distinguishes 1985 from 1984's red FDC LEDs).
 *
 * Activity is signalled by leds_ping() from the device emulation; the LED
 * glows bright for LED_GLOW_MS milliseconds, then fades to its idle colour.
 */

#define LED_BAR_H     22
#define LED_GLOW_MS   120

typedef enum {
    LED_FDC_A = 0,
    LED_FDC_B,
    LED_PRINTER,    /* Orange. Pulses while bytes arrive at FCh/FDh. */
    LED_SERIAL,     /* Split LED: left half = RX (red), right half = TX (green). */
    LED_COUNT
} LedId;

/* Configure which LEDs to display. Call after reading config. */
void leds_set_enabled(LedId id, bool enabled);

/* Signal one frame of activity for the given LED. */
void leds_ping(LedId id);

/* Signal RX (tx=false) or TX (tx=true) activity on a split LED. */
void leds_ping_split(LedId id, bool tx);

/* Track mouse hover over the LED bar in renderer/logical coordinates.
 * Pass inside=false on window leave or mouse capture to clear the label. */
void leds_set_mouse_position(float x, float y, bool inside);

/* Render the LED bar across (x,y,w,h). */
void leds_render(SDL_Renderer *r, int x, int y, int w, int h);

/* Render the active hover label, if any. Call after bottom status UI so
 * the tooltip is not covered. */
void leds_render_hover(SDL_Renderer *r, int window_w, int window_h);
