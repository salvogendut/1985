#include "mem.h"
#include "bootstrap.h"
#include "kbd.h"
#include <stdio.h>
#include <string.h>

void mem_init(Mem *m) {
    memset(m, 0, sizeof(*m));
    mem_reset(m);
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
    /* Boot overlay lives in low memory; reads above the stream length
     * (notably stack pops at ~0x7E00) must fall through to RAM, or
     * RET would return 0x0000 and restart the loader. */
    if (m->bootstrap && bootstrap_active(m->bootstrap)
        && addr < (u16)m->bootstrap->len)
        return bootstrap_read(m->bootstrap, addr);

    int slot = addr >> 14;
    u8 block = m->read_bank[slot];
    return m->ram[block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1))];
}

void mem_write(Mem *m, u16 addr, u8 val) {
    int slot = addr >> 14;
    u8 block = m->write_bank[slot];
    m->ram[block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1))] = val;
}
