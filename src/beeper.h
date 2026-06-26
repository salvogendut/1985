#pragma once
#include "types.h"

/* PCW internal beeper — a fixed-frequency square wave gated by F8
 * commands 0x0B (on) and 0x0C (off). Used by the boot ROM error tones
 * and the CP/M `BEL` (0x07) character. MAME models it at 3750 Hz
 * (BEEP(..., 3750) in pcw.cpp:1287). */

#define BEEPER_FREQ_HZ 3750

/* Tag-and-typedef form so `struct Beeper *` in asic.h's forward
 * declaration matches the `Beeper *` here without a cast. */
typedef struct Beeper {
    bool on;
    u32  phase_acc;   /* 24.8-ish fixed-point phase */
    u32  phase_step;  /* (freq << 16) / rate — set in beeper_init */
} Beeper;

void beeper_init  (Beeper *b, int audio_rate);
void beeper_reset (Beeper *b);
void beeper_set_on(Beeper *b, bool on);

/* Mix the beeper square wave into `buf` (additive). Pass the same
 * rate beeper_init was called with so the phase advances correctly. */
void beeper_render(Beeper *b, s16 *buf, int n);
