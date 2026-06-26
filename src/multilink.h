#pragma once
#include "types.h"

/* PCW Multilink token-ring probe stub. Multilink lives at ports
 * 0xA6/0xA7 (Seasip §14.1) — same range as the Electric Studio light
 * pen, so the two conflict on real hardware. We don't emulate the ring
 * proper; instead we return the canonical "ring broken / no Multilink"
 * 4-byte reply (00 90 99 00) so Multilink-aware software completes its
 * startup probe and falls through to the no-network path instead of
 * spinning waiting for a token.
 *
 * Reads from either port cycle through the sequence; writes are
 * accepted and dropped. Full ring emulation is L effort and not worth
 * pursuing without a software target — see audit M8 / issue #122. */

typedef struct Multilink {
    u8 idx;   /* next slot in PROBE_REPLY to return on read */
} Multilink;

void multilink_init (Multilink *m);
void multilink_reset(Multilink *m);
u8   multilink_read (Multilink *m, u8 port);
void multilink_write(Multilink *m, u8 port, u8 val);
