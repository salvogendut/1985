#pragma once
#include "types.h"

/*
 * PCW reset-time bootstrap stream.
 *
 * The PCW has no boot ROM. At reset the address decoder feeds the
 * Z80 a canned 778-byte instruction stream that, when executed,
 * builds a 256-byte loader at addresses 0x0002..0x0101 and then
 * jumps to it. The very last two bytes of the canned stream are
 *
 *   D3 F8           OUT (0xF8), A    ; A = 0x00 → disable bootstrap
 *
 * From that write onwards mem_read returns RAM as normal.
 *
 * We don't try to reproduce the real ROM-less hardware's byte stream
 * here — instead we forge a minimal equivalent that:
 *   1. constructs a tiny "no system" loop at 0x0002 (it spins),
 *   2. ends with OUT (0xF8), 0 to hand control back to RAM.
 * This is enough to let the CPU start running without crashing
 * before the FDC + roller-RAM subsystems exist. When real boot is
 * implemented this struct grows the actual byte stream produced by
 * `JoyceAsic.cxx`'s buildBootstrap().
 */

typedef struct Bootstrap {
    int  pos;       /* next byte index into stream[] */
    int  len;
    bool active;
    u8   stream[1024];
} Bootstrap;

void bootstrap_init(Bootstrap *b);
void bootstrap_reset(Bootstrap *b);

bool bootstrap_active(const Bootstrap *b);

/* Returns the next byte from the canned stream. When the stream is
 * exhausted bootstrap_active() goes false and callers should fall
 * back to RAM. */
u8   bootstrap_read_byte(Bootstrap *b);

/* Called from asic.c when the Z80 writes to port 0xF8 — the canonical
 * "bootstrap done" signal. */
void bootstrap_finish(Bootstrap *b);
