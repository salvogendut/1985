#pragma once
#include "types.h"

/*
 * PCW reset-time bootstrap.
 *
 * The PCW has no boot ROM. At reset the address decoder overlays a
 * canned byte sequence onto low memory so the Z80, starting at PC=0,
 * fetches our boot loader instead of RAM. The loader uses the FDC
 * to copy the first sector of drive A into RAM at 0xF000, then
 * issues OUT (0xF8), 0 to disable the overlay and JP 0xF000.
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
