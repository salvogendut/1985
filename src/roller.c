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
    /* Before the roller table exists, real hardware shows a solid
     * green phosphor field. Keep that power-on look instead of
     * decoding RAM address zero as a roller table. */
    if (!a->display_enabled || !a->roller_programmed) {
        display_fill_lit(d);
        return;
    }

    /* F7 bit 6 is the firmware-controlled screen gate. The CP/M boot
     * path drops it after the stripe loader while the old roller table
     * still names loaded program/data bytes, then raises it again once
     * the real screen table and bitmap area have been initialized. */
    if (!a->screen_enabled) {
        display_clear(d);
        return;
    }

    int base    = roller_base_addr(a->roller_base) & 0x3FFFF;
    int scroll  = a->scroll_y;
    int invert  = a->inverse_video ? 1 : 0;
    /* Seasip §4.1: NTSC PCWs only display the top 200 lines; the rest
     * of the frame falls in vertical blanking. We render the rest as
     * background so the bottom strip is visibly off, not garbage. */
    int visible_rows = a->refresh_60hz ? 200 : SCREEN_ROWS;

    /* All coordinates below are in-range by construction (90 cols × 8
     * px = 720 = DISPLAY_W, 256 rows = DISPLAY_H), so write straight
     * into the framebuffer row instead of going through the
     * bounds-checked display_put_pixel() call 184,320 times a frame —
     * that call was ~45% of total emulation time under gprof. */
    const u32 fg = d->fg, bg = d->bg;

    for (int y = 0; y < SCREEN_ROWS; y++) {
        u32 *row = &d->fb[y * DISPLAY_W];
        if (y >= visible_rows) {
            for (int x = 0; x < SCREEN_COLS * 8; x++) row[x] = bg;
            continue;
        }
        int idx = (y + scroll) & 0xFF;
        int tbl_off = (base + idx * 2) & 0x3FFFF;

        /* If the firmware hasn't actually written this roller table
         * slot yet, treat the row as un-populated. Otherwise the two
         * source bytes are random RAM and decode to visual garbage. */
        bool tbl_ready = mem_byte_written(m, tbl_off)
                      && mem_byte_written(m, (tbl_off + 1) & 0x3FFFF);
        if (!tbl_ready) {
            for (int x = 0; x < SCREEN_COLS * 8; x++) row[x] = fg;
            continue;
        }

        unsigned word = (unsigned)m->ram[tbl_off]
                      | ((unsigned)m->ram[(tbl_off + 1) & 0x3FFFF] << 8);
        int row_addr = (int)(((word & 0xE000u) << 1)
                          |  ((word & 0x1FF8u) << 1)
                          |  (word & 0x0007u));

        for (int col = 0; col < SCREEN_COLS; col++) {
            int src = (row_addr + col * 8) & 0x3FFFF;
            u32 *cell = row + col * 8;
            if (!mem_byte_written(m, src)) {
                /* Pixel cell whose source byte has never been written
                 * by the guest — show phosphor green, not whatever
                 * random data happens to live in that RAM cell. */
                for (int b = 0; b < 8; b++) cell[b] = fg;
                continue;
            }
            u8 byte = m->ram[src];
            if (invert) byte ^= 0xFF;

            switch (d->video_mode) {
                case VIDEO_CGA1:
                case VIDEO_CGA2: {
                    /* 2 bpp: each pair of bits is a palette index;
                     * doubled horizontally so the line still spans 720. */
                    for (int b = 0; b < 8; b += 2) {
                        u32 px = d->palette[(byte >> (6 - b)) & 0x3];
                        cell[b]     = px;
                        cell[b + 1] = px;
                    }
                    break;
                }
                case VIDEO_EGA: {
                    /* 4 bpp: each nybble is a palette index; quadrupled
                     * horizontally so the line still spans 720. */
                    for (int b = 0; b < 8; b += 4) {
                        u32 px = d->palette[(byte >> (4 - b)) & 0xF];
                        for (int r = 0; r < 4; r++)
                            cell[b + r] = px;
                    }
                    break;
                }
                case VIDEO_PCW:
                default:
                    for (int b = 0; b < 8; b++)
                        cell[b] = (byte & (0x80u >> b)) ? fg : bg;
                    break;
            }
        }
    }
}
