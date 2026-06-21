#include "roller.h"
#include "display.h"
#include "mem.h"
#include "asic.h"

/*
 * PCW roller-RAM video walker.
 *
 * The 256 visible scanlines are described by a 512-byte table at the
 * absolute RAM offset latched in ASIC port 0xF5. Each table entry is
 * a 16-bit little-endian "roller word" naming the source row in main
 * RAM for that scanline:
 *
 *   row_addr = ((word & 0xE000) << 1)
 *            | ((word & 0x1FF8) << 1)
 *            |  (word & 0x0007)
 *
 * (per MAME pcw_v.cpp:142.) Within a row the 90 byte columns are
 * NOT contiguous — they step by 8 (character-cell interleave), so
 * byte[col] = ram[row_addr + col*8].
 *
 * Each byte produces 8 pixels MSB-first. This milestone does the
 * naive walk only — scroll, invert, and display-enable land next.
 */

#define SCREEN_ROWS    256
#define SCREEN_COLS    90

static int roller_base_addr(u8 v) {
    /* MAME pcw.cpp:401 — F5 byte decodes to an absolute RAM offset. */
    return (((v >> 5) & 7) << 14) | ((v & 0x1F) << 9);
}

void roller_render(struct Mem *m, struct Asic *a, struct Display *d) {
    /* Display-enable gate: F8 cmds 7/8 only. F7 bit 6 turns out to be
     * something else on this firmware (Loco/CP/M+ writes F7=0 early
     * and never re-asserts bit 6, but expects the display to stay on),
     * so we don't gate on it. */
    if (!a->display_enabled) {
        display_clear(d);
        return;
    }

    int base    = roller_base_addr(a->roller_base) & 0x3FFFF;
    int scroll  = a->scroll_y;
    int invert  = a->inverse_video ? 1 : 0;

    for (int y = 0; y < SCREEN_ROWS; y++) {
        int idx = (y + scroll) & 0xFF;
        int tbl_off = (base + idx * 2) & 0x3FFFF;
        unsigned word = (unsigned)m->ram[tbl_off]
                      | ((unsigned)m->ram[(tbl_off + 1) & 0x3FFFF] << 8);
        int row_addr = (int)(((word & 0xE000u) << 1)
                          |  ((word & 0x1FF8u) << 1)
                          |  (word & 0x0007u));

        for (int col = 0; col < SCREEN_COLS; col++) {
            u8 byte = m->ram[(row_addr + col * 8) & 0x3FFFF];
            int xbase = col * 8;
            for (int b = 0; b < 8; b++)
                display_put_pixel(d, xbase + b, y,
                                  (((byte >> (7 - b)) & 1) ^ invert) != 0);
        }
    }
}
