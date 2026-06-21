#include "mem.h"
#include "bootstrap.h"
#include "kbd.h"
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
    m->lock = 0;
}

void mem_bank_write(Mem *m, u8 port, u8 val) {
    if (port < 0xF0 || port > 0xF3) return;
    int slot = port - 0xF0;
    if (val & 0x80) {
        /* PCW extended mode — one block, used for read and write. */
        u8 blk = val & 0x0F;
        m->read_bank [slot] = blk;
        m->write_bank[slot] = blk;
    } else {
        /* CPC standard mode — separate read- and write-banks.
         * bits 6-4 = block to read, bits 3-0 = block to write
         * (limited to blocks 0-15 / first 128 KB). */
        m->read_bank [slot] = (val >> 4) & 0x07;
        m->write_bank[slot] =  val       & 0x0F;
    }
}

void mem_set_lock(Mem *m, u8 val) {
    /* bit (slot+4) high → reads from that slot go to write_bank instead of read_bank. */
    m->lock = val;
}

u8 mem_read(Mem *m, u16 addr) {
    /* Boot overlay lives in low memory; reads above the stream length
     * (notably stack pops at ~0x7E00) must fall through to RAM, or
     * RET would return 0x0000 and restart the loader. */
    if (m->bootstrap && bootstrap_active(m->bootstrap)
        && addr < (u16)m->bootstrap->len)
        return bootstrap_read(m->bootstrap, addr);

    int slot = addr >> 14;
    bool locked = (m->lock >> (slot + 4)) & 1;
    u8 block = locked ? m->write_bank[slot] : m->read_bank[slot];

    /* Memory-mapped keyboard: whichever slot maps block 3 sees the
     * live matrix at offset 0x3FF0..0x3FFF instead of stored RAM. */
    if (m->kbd && block == MEM_KBD_BLOCK) {
        u16 off = addr & (MEM_BLOCK_SIZE - 1);
        if (off >= MEM_KBD_OFFSET && off < MEM_KBD_OFFSET + MEM_KBD_LEN)
            return kbd_matrix_byte(m->kbd, off - MEM_KBD_OFFSET);
    }

    return m->ram[block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1))];
}

void mem_write(Mem *m, u16 addr, u8 val) {
    int slot = addr >> 14;
    u8 block = m->write_bank[slot];
    m->ram[block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1))] = val;
}
