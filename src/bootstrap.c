#include "bootstrap.h"
#include <string.h>

/*
 * Minimal forged stream — replaced with the real 778-byte sequence
 * when roller-RAM + FDC integration lands.
 *
 *   Address (logical fetch position vs. resulting RAM image — the
 *   PCW boot sequence works by COPYING instructions into RAM, not
 *   by executing them in-stream. For the stub we just dribble out
 *   a few NOPs and then OUT (F8), 0 / HALT so the CPU stops once
 *   bootstrap signals done. F12 quits the emulator from there.)
 */
static const u8 stub_stream[] = {
    0x00,                       /* NOP */
    0x00, 0x00, 0x00, 0x00,     /* NOP × 4 */
    0xAF,                       /* XOR A      (A = 0) */
    0xD3, 0xF8,                 /* OUT (F8), A → bootstrap done */
    0x76                        /* HALT — CPU waits for IRQs */
};

void bootstrap_init(Bootstrap *b) {
    memset(b, 0, sizeof(*b));
    bootstrap_reset(b);
}

void bootstrap_reset(Bootstrap *b) {
    b->pos    = 0;
    b->len    = (int)sizeof(stub_stream);
    b->active = true;
    memcpy(b->stream, stub_stream, sizeof(stub_stream));
}

bool bootstrap_active(const Bootstrap *b) {
    return b->active;
}

u8 bootstrap_read_byte(Bootstrap *b) {
    if (b->pos >= b->len) {
        /* Past the end — pretend we keep returning NOPs until
         * something explicitly disables the bootstrap. */
        return 0x00;
    }
    return b->stream[b->pos++];
}

void bootstrap_finish(Bootstrap *b) {
    b->active = false;
}
