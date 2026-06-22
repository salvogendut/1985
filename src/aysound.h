#pragma once
#include "types.h"
#include <stdbool.h>

/*
 * AY-3-8912 Programmable Sound Generator — DK'tronics PCW Sound + Joystick.
 *
 * The DK'tronics add-on (and SanPollo's modern clone) plugs onto the
 * PCW backplane and exposes the AY at:
 *
 *   0xA9 (read)   data read from selected register
 *   0xAA (write)  register select
 *   0xAB (write)  data write to selected register
 *
 * The AY's Port A (register 14) is wired to the DB9 joystick connector
 * — Atari-style active-low bits 0..5 (Up / Down / Left / Right / Fire1
 * / Fire2). When the guest selects R14 and reads 0xA9, our `joystick`
 * byte goes out. (Same trick as the CPC's PSG, where Port A reads the
 * keyboard matrix — this module is ported nearly verbatim from
 * 1984/src/psg.*.)
 *
 * Hardware note: an AMX-style PCW mouse lives at 0xA0..0xA3 (8255 PPI,
 * a different chip on the same DB9 socket family). The two extensions
 * are independent — they don't share ports on real hardware.
 *
 * Port reference: https://wiki.readerrorb.ro/doku.php?id=tech:amstrad:pcw:ioports
 */

#define AYSOUND_NUM_REGS 16

typedef struct AySound {
    bool present;

    u8   reg[AYSOUND_NUM_REGS];
    u8   selected;

    /* Per-channel tone counters */
    u16 tone_period[3];
    u16 tone_counter[3];
    u8  tone_output[3];

    /* Noise */
    u16 noise_period;
    u16 noise_counter;
    u32 noise_lfsr;

    /* Envelope */
    u16  env_period;
    u32  env_counter;
    u8   env_step;        /* 0..31 */
    bool env_hold;
    bool env_dir;

    /* Fractional clock accumulator across samples */
    float clock_rem;
    /* One-pole IIR low-pass state */
    s32 lp_state;

    /* Port A externals: Atari-style active-low joystick byte. */
    u8 joystick;
} AySound;

void aysound_init    (AySound *a, bool present);
void aysound_reset   (AySound *a);
void aysound_set_joystick(AySound *a, u8 active_low_bits);

/* Bus dispatch — `lo` is the I/O port low byte (0xA0..0xA3 in range). */
u8   aysound_read (AySound *a, u8 lo);
void aysound_write(AySound *a, u8 lo, u8 val);

/* Render `n` mono S16 samples at sample_rate Hz, ticking the AY at
 * clock_hz. Mirrors 1984/src/psg.c — see comments there. */
void aysound_render(AySound *a, s16 *buf, int n, int clock_hz, int sample_rate);
