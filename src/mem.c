#include "mem.h"
#include "bootstrap.h"
#include "kbd.h"
#include <string.h>

void mem_init(Mem *m) {
    memset(m, 0, sizeof(*m));
    mem_reset(m);
}

void mem_reset(Mem *m) {
    /* On reset all four slots map block 0. The PCW firmware
     * reprograms them immediately. */
    for (int i = 0; i < 4; i++) m->bank[i] = 0;
}

void mem_bank_write(Mem *m, u8 port, u8 val) {
    if (port < 0xF0 || port > 0xF3) return;
    /* Bit 7 must be set on a real bank-select write; ignore otherwise. */
    if (!(val & 0x80)) return;
    m->bank[port - 0xF0] = val & 0x0F;   /* 16 blocks; ignore CPC-compat bits */
}

u8 mem_read(Mem *m, u16 addr) {
    /* Bootstrap window — every instruction fetch up to bootstrap end
     * comes from the canned stream rather than RAM. */
    /* Boot overlay lives in low memory; reads above the stream length
     * (notably stack pops at ~0x7E00) must fall through to RAM, or
     * RET would return 0x0000 and restart the loader. */
    if (m->bootstrap && bootstrap_active(m->bootstrap)
        && addr < (u16)m->bootstrap->len)
        return bootstrap_read(m->bootstrap, addr);

    int slot = addr >> 14;
    u8 block = m->bank[slot];

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
    u8 block = m->bank[slot];
    m->ram[block * MEM_BLOCK_SIZE + (addr & (MEM_BLOCK_SIZE - 1))] = val;
}
