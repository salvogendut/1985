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
 * Two paging modes, distinguished by bit 7 of the value written:
 *   PCW extended (bit 7 = 1): low 7 bits = block; one block per slot
 *                             used for both reads and writes.
 *   CPC standard (bit 7 = 0): bits 6-4 = block to READ from,
 *                             bits 3-0 = block to WRITE to.
 *                             Only blocks 0-15 (first 128 KB) reachable.
 *
 * Port 0xF4 (write) is the bank-force register: bits 4-7 select which
 * 16 KB slots should force their read bank to match the write bank when
 * a bank register is programmed in CPC-style mode.
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
    /* Per-byte "guest has written this" bitmap. Lets the video walker
     * tell apart pixels written by the firmware from un-touched RAM
     * (which would otherwise decode as visual garbage at boot). */
    u8  ram_written[MEM_SIZE / 8];
    u8  read_bank [4];                   /* block selected for reads in each slot */
    u8  write_bank[4];                   /* block selected for writes in each slot */
    u8  bank_force;                      /* port F4 bits 4-7 = force read bank == write bank */
    struct Bootstrap *bootstrap;         /* non-NULL while reset stream is active */
    struct Keyboard  *kbd;               /* live matrix overlay for block 3 reads */
} Mem;

static inline bool mem_byte_written(const Mem *m, int abs_addr) {
    int a = abs_addr & (MEM_SIZE - 1);
    return (m->ram_written[a >> 3] >> (a & 7)) & 1;
}

void mem_init(Mem *m);
void mem_reset(Mem *m);

/* Bank select via OUT (port), val for port in 0xF0..0xF3. */
void mem_bank_write(Mem *m, u8 port, u8 val);

/* Bank-force register, written via OUT (F4), val. */
void mem_set_lock(Mem *m, u8 val);

u8   mem_read (Mem *m, u16 addr);
void mem_write(Mem *m, u16 addr, u8 val);
