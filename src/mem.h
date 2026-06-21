#pragma once
#include "types.h"

/*
 * Amstrad PCW memory subsystem
 *
 * 256 KB RAM organised as 16 × 16 KB blocks.
 * 4 paging registers (one per Z80 slot, ports 0xF0–0xF3) each select
 * which of the 16 blocks appears in the corresponding 16 KB window:
 *   port 0xF0 → 0x0000–0x3FFF
 *   port 0xF1 → 0x4000–0x7FFF
 *   port 0xF2 → 0x8000–0xBFFF
 *   port 0xF3 → 0xC000–0xFFFF
 *
 * Value written is 0x80 | block_number. Bit 7 must be set; bits 6..4
 * have further meaning on later PCWs (CPC compatibility / RAM size)
 * which we ignore for the 8256 stub.
 *
 * At reset, before the boot program has finished, mem_read() returns
 * bytes from the bootstrap stream (see bootstrap.c) regardless of
 * address — once the bootstrap signals completion (write to port
 * 0xF8) RAM takes over.
 *
 * Keyboard scan rows live at addresses 0x3FF0..0x3FFF in the
 * unbanked CPC sense; PCW firmware reads them via whichever slot is
 * currently mapping RAM block 3. mem_read() detects this and returns
 * the live keyboard matrix instead of stored RAM contents.
 */

#define MEM_BLOCK_SIZE   0x4000          /* 16 KB */
#define MEM_BLOCK_COUNT  16              /* 256 KB total */
#define MEM_SIZE         (MEM_BLOCK_SIZE * MEM_BLOCK_COUNT)

#define MEM_KBD_BLOCK    3               /* block holding keyboard scan rows */
#define MEM_KBD_OFFSET   0x3FF0
#define MEM_KBD_LEN      16

struct Bootstrap;   /* forward */
struct Keyboard;    /* forward */

typedef struct Mem {
    u8  ram[MEM_SIZE];
    u8  bank[4];                         /* block index per Z80 slot */
    struct Bootstrap *bootstrap;         /* non-NULL while reset stream is active */
    struct Keyboard  *kbd;               /* live matrix overlay for block 3 reads */
} Mem;

void mem_init(Mem *m);
void mem_reset(Mem *m);

/* Bank select via OUT (port), val for port in 0xF0..0xF3. */
void mem_bank_write(Mem *m, u8 port, u8 val);

u8   mem_read (Mem *m, u16 addr);
void mem_write(Mem *m, u16 addr, u8 val);
