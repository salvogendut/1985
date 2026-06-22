#include "mem.h"
#include "bootstrap.h"
#include "kbd.h"
#include <stdio.h>
#include <string.h>

void mem_init(Mem *m) {
    memset(m, 0, sizeof(*m));
    m->ram_blocks = 16;   /* default: bare PCW 8256 — 256 KB */
    mem_reset(m);
}

void mem_set_size_kb(Mem *m, int kb) {
    int blocks = kb / 16;
    if (blocks < 16)              blocks = 16;
    if (blocks > MEM_BLOCK_COUNT) blocks = MEM_BLOCK_COUNT;
    m->ram_blocks = blocks;
}

void mem_reset(Mem *m) {
    /* Power-on default per MAME pcw.cpp:994-998 — identity map:
     * slot i -> physical block i. CP/M+'s loader relies on this
     * default (it doesn't reprogram every bank before writing). */
    for (int i = 0; i < 4; i++) m->read_bank[i] = m->write_bank[i] = (u8)i;
    /* MAME starts the PCW with all CPC-style paging locks asserted.
     * That matters when firmware programs a bank in standard mode:
     * the read bank should follow the write bank for locked slots. */
    m->bank_force = 0xF0;
}

void mem_bank_write(Mem *m, u8 port, u8 val) {
    if (port < 0xF0 || port > 0xF3) return;
    int slot = port - 0xF0;
    if (val & 0x80) {
        /* PCW extended mode — one block, used for read and write. */
        u8 blk = val & (MEM_BLOCK_COUNT - 1);
        m->read_bank [slot] = blk;
        m->write_bank[slot] = blk;
    } else {
        /* CPC standard mode — separate read- and write-banks.
         * bits 6-4 = block to read, bits 2-0 = block to write.
         * The PCW firmware only relies on the 0-7 bank range here. */
        u8 read_bank = (val >> 4) & 0x07;
        m->write_bank[slot] =  val       & 0x07;
        /* F4 lock bit per slot, matching MAME pcw.cpp:295-318:
         * slot 0 -> b6, slot 1 -> b4, slot 2 -> b5, slot 3 -> b7
         * (systemed.net hardware.html: b7-b4 = &C000/&0000/&8000/&4000). */
        static const u8 force_bit[4] = { 6, 4, 5, 7 };
        if ((m->bank_force >> force_bit[slot]) & 1)
            read_bank = m->write_bank[slot];
        m->read_bank[slot] = read_bank;
    }
}

void mem_set_lock(Mem *m, u8 val) {
    /* Bits 4-7 force the corresponding slot's read bank to equal the
     * write bank when CPC-style bank programming is used. */
    m->bank_force = val;
}

u8 mem_read(Mem *m, u16 addr) {
    /* No overlay -- the boot ROM is now copied directly into bank 0
     * RAM by pcw_reset(). This matches ZEsarUX's model and the real
     * PCW: the boot ROM bytes coexist with RAM and get overwritten
     * by subsequent writes (which is critical -- the boot ROM at
     * 0x0102 writes the OUT (F8),A instruction to RAM[0,1] then
     * JP 0000, relying on the just-written instruction to be fetched
     * and executed). With an overlay-on-reads model, the JP 0000
     * would re-fetch the original ROM byte and infinite-loop. */
    int slot = addr >> 14;
    int block = m->read_bank[slot];
    /* On real PCW hardware the high address bits beyond the populated
     * memory wrap, so paging a "non-existent" bank actually aliases an
     * existing one. The firmware's RAM-size probe relies on that:
     * it writes a known byte to bank N, then reads back through bank
     * N % ram_blocks; matching content means the upper bits were
     * dropped and there is no real RAM up there. */
    if (block >= m->ram_blocks) block %= m->ram_blocks;
    return m->ram[block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1))];
}

void mem_write(Mem *m, u16 addr, u8 val) {
    int slot = addr >> 14;
    int block = m->write_bank[slot];
    if (block >= m->ram_blocks) block %= m->ram_blocks;
    int a = block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1));
    m->ram[a] = val;
    m->ram_written[a >> 3] |= (u8)(1u << (a & 7));
}
