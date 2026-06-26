/* PCW beeper — square wave gated by F8 cmd 0x0B/0x0C. See beeper.h. */
#include "beeper.h"
#include <string.h>

/* Output amplitude when the beeper is on. A real PCW beeper isn't very
 * loud relative to a Spectrum / DK'tronics AY tone, but it should be
 * audible alongside AY output without clipping the mixed S16 buffer.
 * ~8% of full scale is comfortable headroom (AY peaks at ~24k, AY+beep
 * stays under 32k). */
#define BEEPER_AMPLITUDE 2600

void beeper_init(Beeper *b, int audio_rate) {
    memset(b, 0, sizeof(*b));
    if (audio_rate <= 0) return;
    /* phase_step is the amount added to phase_acc per sample so that
     * the MSB toggles at BEEPER_FREQ_HZ. With phase_acc as a 32-bit
     * counter, one full cycle is 2^32; we want 2*BEEPER_FREQ_HZ MSB
     * flips per second, so phase_step = (2^32 * freq) / rate. */
    b->phase_step = (u32)(((u64)BEEPER_FREQ_HZ << 32) / (u64)audio_rate);
}

void beeper_reset(Beeper *b) {
    b->on        = false;
    b->phase_acc = 0;
}

void beeper_set_on(Beeper *b, bool on) {
    /* No phase reset on edge — matches a continuously-running oscillator
     * gated by a switch, which is what the PCW circuit does. */
    b->on = on;
}

void beeper_render(Beeper *b, s16 *buf, int n) {
    if (!b->on) {
        /* Phase still advances when muted so the wave is in the same
         * place when the beeper is re-enabled (same as the real
         * always-running oscillator). */
        b->phase_acc = (u32)(b->phase_acc + (u32)b->phase_step * (u32)n);
        return;
    }
    for (int i = 0; i < n; i++) {
        s16 v = (b->phase_acc & 0x80000000u) ? BEEPER_AMPLITUDE
                                             : -BEEPER_AMPLITUDE;
        /* Clamp the sum into int16 range — paranoid; AY's peak + our
         * amplitude is well below 32767 by design. */
        int sum = (int)buf[i] + (int)v;
        if      (sum >  32767) sum =  32767;
        else if (sum < -32768) sum = -32768;
        buf[i] = (s16)sum;
        b->phase_acc += b->phase_step;
    }
}
