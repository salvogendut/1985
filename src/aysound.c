/* aysound.c — AY-3-8912 PSG model for the DK'tronics PCW sound +
 * joystick board. Ported nearly verbatim from 1984/src/psg.c. */

#include "aysound.h"
#include <string.h>

/* AY-3-8912 amplitude table (c) Hacker KAY, scaled for s16 sum across
 * three channels: source values 0..65535 / 6 → channel sum ≤ 32767. */
static const int vol_table[16] = {
       0,   139,   202,   295,   436,   645,   899,  1470,
    1732,  2784,  3889,  4882,  6161,  7736,  9199, 10922
};

void aysound_init(AySound *a, bool present) {
    memset(a, 0, sizeof(*a));
    a->present     = present;
    a->noise_lfsr  = 1;
    a->joystick    = 0xFF;   /* DB9 idle = all bits high (active-low) */
}

void aysound_reset(AySound *a) {
    bool was_present = a->present;
    u8   js          = a->joystick;
    aysound_init(a, was_present);
    a->joystick = js;
}

void aysound_set_joystick(AySound *a, u8 active_low_bits) {
    a->joystick = active_low_bits;
}

u8 aysound_pack_dksound(bool up, bool down, bool left, bool right,
                        bool fire1, bool fire2) {
    u8 value = 0xFF;
    if (left)           value &= (u8)~(1u << 2);
    if (right)          value &= (u8)~(1u << 3);
    if (down)           value &= (u8)~(1u << 4);
    if (up)             value &= (u8)~(1u << 5);
    if (fire1 || fire2) value &= (u8)~(1u << 6);
    return value;
}

/* Port dispatch. Caller has already filtered to 0xA9..0xAB. */
u8 aysound_read(AySound *a, u8 lo) {
    if (!a->present) return 0xFF;
    if (lo == 0xA9) {
        if (a->selected == 14) return a->joystick;
        if (a->selected >= AYSOUND_NUM_REGS) return 0xFF;
        return a->reg[a->selected];
    }
    return 0xFF;
}

void aysound_write(AySound *a, u8 lo, u8 val) {
    if (!a->present) return;
    if (lo == 0xAA) {
        a->selected = val;
        return;
    }
    if (lo == 0xAB) {
        if (a->selected >= AYSOUND_NUM_REGS) return;
        a->reg[a->selected] = val;
        if (a->selected == 13) {
            a->env_step    = 0;
            a->env_counter = 0;
            a->env_hold    = false;
            a->env_dir     = (val >> 2) & 1;  /* ATTACK bit */
        }
    }
}

/* One AY clock tick — returns the mixed level for all three channels. */
static int ay_tick(AySound *a) {
    /* Tone counters — internal ÷8 prescaler then ÷N. f = clk/(16·N). */
    for (int c = 0; c < 3; c++) {
        u16 period = (u16)(((a->reg[c*2+1] & 0x0F) << 8) | a->reg[c*2]);
        if (!period) period = 1;
        if (++a->tone_counter[c] >= (u16)(period * 8)) {
            a->tone_counter[c] = 0;
            a->tone_output[c] ^= 1;
        }
    }

    /* Noise LFSR. */
    {
        u16 np = a->reg[6] & 0x1F;
        if (!np) np = 1;
        if (++a->noise_counter >= (u16)(np * 16)) {
            a->noise_counter = 0;
            u32 bit = (a->noise_lfsr ^ (a->noise_lfsr >> 3)) & 1;
            a->noise_lfsr = (a->noise_lfsr >> 1) | (bit << 16);
        }
    }
    int noise_out = (int)(a->noise_lfsr & 1);

    /* Envelope: 32 steps. AY-3-8912 datasheet ambiguity aside, MAME's
     * authoritative model (sound/ay8910.cpp:643-665, `STEP_AY = 2`)
     * gives 2 master cycles per envelope-counter step. We previously
     * used 8, making envelopes run 4× too slow. (1984/src/psg.c has
     * the same bug — fix there too if anyone cares about CPC envelopes.) */
    if (!a->env_hold) {
        u16 ep = (u16)((a->reg[12] << 8) | a->reg[11]);
        if (!ep) ep = 1;
        if (++a->env_counter >= (u32)ep * 2) {
            a->env_counter = 0;
            a->env_step++;
            if (a->env_step >= 32) {
                u8   shape = a->reg[13] & 0x0F;
                bool cont  = (shape >> 3) & 1;
                bool alt   = (shape >> 1) & 1;
                bool hold  = (shape >> 0) & 1;
                if (!cont) {
                    a->env_step = a->env_dir ? 0 : 31;
                    a->env_hold = true;
                } else if (hold) {
                    if (alt) a->env_dir = !a->env_dir;
                    a->env_step = 31;
                    a->env_hold = true;
                } else if (alt) {
                    a->env_dir = !a->env_dir;
                    a->env_step = 0;
                } else {
                    a->env_step = 0;
                }
            }
        }
    }
    u8 env_level = (u8)(a->env_dir ? (a->env_step / 2)
                                   : (u8)((31 - a->env_step) / 2));

    /* Mix three channels. */
    int mix = 0;
    int active_scale = 0;
    for (int c = 0; c < 3; c++) {
        bool tone_off  = (a->reg[7] >> c)       & 1;
        bool noise_off = (a->reg[7] >> (c + 3)) & 1;
        int tone_hi    = tone_off  ? 1 : (int)a->tone_output[c];
        int noise_hi   = noise_off ? 1 : noise_out;
        u8 vr  = a->reg[8 + c];
        u8 vol = (vr & 0x10) ? env_level : (vr & 0x0F);
        if (tone_off && noise_off) continue;
        active_scale += vol_table[vol];
        if (tone_hi && noise_hi) mix += vol_table[vol];
    }
    return (mix * 2) - active_scale;
}

void aysound_render(AySound *a, s16 *buf, int n, int clock_hz, int sample_rate) {
    if (!a->present) {
        memset(buf, 0, (size_t)n * sizeof(s16));
        return;
    }
    float clk_per_sample = (float)clock_hz / (float)sample_rate;
    for (int i = 0; i < n; i++) {
        a->clock_rem += clk_per_sample;
        int ticks = (int)a->clock_rem;
        a->clock_rem -= (float)ticks;
        if (ticks < 1) ticks = 1;
        int mix_sum = 0;
        for (int t = 0; t < ticks; t++) mix_sum += ay_tick(a);
        buf[i] = (s16)(mix_sum / ticks);
    }
    /* IIR low-pass to soften aliasing. */
    int lp = a->lp_state;
    for (int i = 0; i < n; i++) {
        lp = ((int)buf[i] + lp) >> 1;
        buf[i] = (s16)lp;
    }
    a->lp_state = lp;
}
