#pragma once
#include "types.h"

/*
 * Motorola 6845 CRTC — stub.
 *
 * On the PCW, software cannot reach the 6845 register file directly;
 * it programs the screen via ASIC ports 0xF5/0xF6/0xF7 instead. The
 * 6845 is here mostly to give us a frame/HSYNC/VSYNC clock and an
 * IRQ cadence consistent with the ~50 Hz PAL display. For the
 * scaffold we simply tick once per emulated frame.
 */

typedef struct {
    int frame_count;
} Crtc;

void crtc_init(Crtc *c);
void crtc_reset(Crtc *c);

/* Advance one emulated frame. Returns true if VSYNC just fired
 * (caller schedules the maskable interrupt). */
bool crtc_frame(Crtc *c);
