#pragma once
#include "types.h"

/*
 * PCW reset-time bootstrap.
 *
 * On real hardware the printer-controller MCU (an 8041AH) shifts 275
 * bytes of mask-programmed boot code into low RAM through the parallel
 * port. We model that by loading the same 275-byte image from
 * `roms/pcw_boot.rom` (with an embedded fallback in bootstrap.c) and
 * overlaying it onto low memory so the Z80, starting at PC=0, fetches
 * the loader instead of RAM. The loader uses the FDC to copy the first
 * sector of drive A into RAM at 0xF000, then issues OUT (0xF8), 0 to
 * disable the overlay and JP 0xF000.
 *
 * Reads are address-indexed (not sequential) so the loader can use
 * loops and subroutines: while active, mem_read(addr) returns
 * stream[addr] for addr < len, and falls through to RAM otherwise.
 *
 * Once OUT (0xF8), 0 fires (asic.c calls bootstrap_finish) the
 * overlay is removed and all subsequent fetches go straight to RAM.
 */

typedef struct Bootstrap {
    int  len;
    bool active;
    u8   stream[1024];
    /* Human-readable label of where the ROM bytes came from. Set in
     * bootstrap_reset(): either the path of an on-disk file picked up
     * by the search-path walk in load_rom_file(), or "embedded" when
     * the fallback shipped with the binary was used. Read by the
     * overlay's Advanced tab purely for display. */
    char source[512];
} Bootstrap;

void bootstrap_init(Bootstrap *b);
void bootstrap_reset(Bootstrap *b);

bool bootstrap_active(const Bootstrap *b);

/* Address-indexed fetch. Returns stream[addr] for addr in [0, len),
 * 0x00 otherwise. Callers should check bootstrap_active() first. */
u8   bootstrap_read(const Bootstrap *b, u16 addr);

/* Called from asic.c when the Z80 writes to port 0xF8 — the canonical
 * "bootstrap done" signal. */
void bootstrap_finish(Bootstrap *b);
