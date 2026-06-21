#include "pcw.h"
#include <stdio.h>
#include <string.h>

/* 4 MHz Z80, 50 Hz frame → 80,000 T-states per frame. */
#define CYCLES_PER_FRAME  80000

static u8 bus_mem_read(void *ctx, u16 addr) {
    return mem_read(&((PCW *)ctx)->mem, addr);
}

static void dump_pc_bytes(PCW *pcw, u16 pc, const char *tag) {
    static bool dumped_077b = false;
    static bool dumped_5d76 = false;
    static bool dumped_08a0 = false;
    static bool dumped_08ab = false;
    bool *flag = NULL;

    if (pc == 0x077B) flag = &dumped_077b;
    else if (pc == 0x5D76) flag = &dumped_5d76;
    else if (pc == 0x08A0) flag = &dumped_08a0;
    else if (pc == 0x08AB) flag = &dumped_08ab;
    else return;
    if (*flag) return;
    *flag = true;

    fprintf(stderr, "%s pc=%04X bytes:", tag, pc);
    for (int i = -16; i < 48; i++) {
        u16 a = (u16)(pc + i);
        fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
    }
    fputc('\n', stderr);
}

static void bus_mem_write(void *ctx, u16 addr, u8 val) {
    PCW *pcw = (PCW *)ctx;
    /* Trace ALL writes (zero and nonzero) to the BIOS scheduler queue
     * regions, with the per-slot read/write bank at the moment of the
     * write. If write_bank != read_bank, the seed lands in a physical
     * block the dispatcher never reads back from. */
    if ((addr >= 0x1010 && addr < 0x1018)
        || (addr >= 0x0D00 && addr < 0x0D10)
        || (addr >= 0x10A0 && addr < 0x10B0)
        || (addr >= 0x6D1B && addr < 0x6D20)) {
        u8 old = mem_read(&pcw->mem, addr);
        if (old != val) {
            int slot = addr >> 14;
            fprintf(stderr,
                "seed_write pc=%04X %04X %02X->%02X slot=%d rb=%02X wb=%02X bf=%02X\n",
                pcw->cpu.pc, addr, old, val,
                slot,
                pcw->mem.read_bank[slot],
                pcw->mem.write_bank[slot],
                pcw->mem.bank_force);
        }
    }
    mem_write(&pcw->mem, addr, val);
}

static u8 bus_io_read(void *ctx, u16 port) {
    PCW *pcw = (PCW *)ctx;
    u8 lo = (u8)(port & 0xFF);
    if (pcw->trace_io)
        fprintf(stderr, "io_read  pc=%04X op=%02X port=%04X (lo=%02X)\n",
                pcw->cpu.pc, pcw->cpu.last_op, port, lo);
    dump_pc_bytes(pcw, pcw->cpu.pc, "pcdump");

    /* FDC: A0 selects MSR vs Data, mirrored across 0x00..0x7F
     * (A1..A6 are don't-care, A7=0 selects the FDC window).
     * MAME pcw.cpp: map(0x000, 0x001).mirror(0x7e). */
    if (lo < 0x80) {
        u8 v = fdc_read(&pcw->fdc, lo & 0x01);
        if (pcw->trace_io)
            fprintf(stderr, "        -> %02X\n", v);
        return v;
    }

    /* ASIC system control and video registers. */
    if (lo == 0xF4 || lo == 0xF8) {
        u8 v = asic_read(&pcw->asic, lo);
        if (pcw->trace_io)
            fprintf(stderr, "        -> %02X\n", v);
        return v;
    }

    /* Printer status. */
    if (lo == 0xFC || lo == 0xFD) {
        u8 v = printer_read(&pcw->printer, lo);
        if (pcw->trace_io)
            fprintf(stderr, "        -> %02X\n", v);
        return v;
    }

    if (pcw->trace_io)
        fprintf(stderr, "        -> FF\n");
    return 0xFF;
}

static void bus_io_write(void *ctx, u16 port, u8 val) {
    PCW *pcw = (PCW *)ctx;
    u8 lo = (u8)(port & 0xFF);
    if (pcw->trace_io)
        fprintf(stderr, "io_write pc=%04X op=%02X port=%04X=%02X\n",
                pcw->cpu.pc, pcw->cpu.last_op, port, val);
    dump_pc_bytes(pcw, pcw->cpu.pc, "pcdump");

    if (lo < 0x80) { fdc_write(&pcw->fdc, lo & 0x01, val); return; }

    if (lo >= 0xF0 && lo <= 0xF3) {
        mem_bank_write(&pcw->mem, lo, val);
        return;
    }
    if (lo == 0xF4) {
        /* F4 write = per-slot read/write lock register, owned by mem.c.
         * (F4 read still goes to asic.c for the interrupt-counter clear.) */
        mem_set_lock(&pcw->mem, val);
        return;
    }
    if (lo >= 0xF5 && lo <= 0xF8) {
        asic_write(&pcw->asic, lo, val);
        return;
    }
    if (lo == 0xFC || lo == 0xFD) {
        printer_write(&pcw->printer, lo, val);
        return;
    }
}

void pcw_init(PCW *pcw, PcwModel model) {
    memset(pcw, 0, sizeof(*pcw));

    pcw->model = model;

    bootstrap_init(&pcw->boot);
    mem_init(&pcw->mem);
    pcw->mem.bootstrap = &pcw->boot;
    pcw->mem.kbd       = &pcw->kbd;

    kbd_init    (&pcw->kbd);
    fdc_init    (&pcw->fdc);
    crtc_init   (&pcw->crtc);
    printer_init(&pcw->printer);
    asic_init   (&pcw->asic, &pcw->boot, &pcw->fdc);

    pcw->bus.mem_read  = bus_mem_read;
    pcw->bus.mem_write = bus_mem_write;
    pcw->bus.io_read   = bus_io_read;
    pcw->bus.io_write  = bus_io_write;
    pcw->bus.tick      = NULL;
    pcw->bus.ctx       = pcw;

    z80_init(&pcw->cpu);
    pcw_reset(pcw);
}

void pcw_reset(PCW *pcw) {
    bootstrap_reset(&pcw->boot);
    mem_reset(&pcw->mem);
    fdc_reset(&pcw->fdc);
    crtc_reset(&pcw->crtc);
    asic_reset(&pcw->asic);
    z80_reset(&pcw->cpu);

    /* On the real machine PC starts at 0x0000 and the bootstrap
     * stream is what the fetcher sees. z80_reset() already zeroes
     * PC; nothing else to do here. */
}

/* 4 MHz / 300 Hz ≈ 13_333 cycles per timer tick. The PCW asserts /INT
 * 300 times per second from a periodic timer (MAME pcw.cpp:1297); the
 * BIOS polls the F8 interrupt counter to schedule its per-frame work. */
#define CYCLES_PER_TICK   (CYCLES_PER_FRAME / 6)

void pcw_frame(PCW *pcw) {
    int cycles = 0;
    int next_tick = CYCLES_PER_TICK;

    while (cycles < CYCLES_PER_FRAME) {
        if (pcw->cpu.halted) {
            /* Skip straight to the next tick — IRQ will wake the CPU. */
            cycles = next_tick;
        } else {
            /* BDOS-call trace: CP/M+ apps enter BDOS via CALL 5.
             * At PC=5, C = function, DE = parameter. Return address
             * is on the stack (top word). */
            if (pcw->cpu.pc == 0x0005) {
                static u32 last = 0;
                static u8  last_c = 0xFF; static u16 last_de = 0;
                u8  c = pcw->cpu.c;
                u16 de = pcw->cpu.de;
                u16 sp = pcw->cpu.sp;
                u16 ret = mem_read(&pcw->mem, sp)
                        | (mem_read(&pcw->mem, (u16)(sp+1)) << 8);
                /* De-dupe immediate repeats so polled functions don't drown the log. */
                if (++last == 1 || c != last_c || de != last_de) {
                    fprintf(stderr, "bdos f=%02X (C=%02X) DE=%04X ret=%04X bank0=%02X bank1=%02X bank2=%02X bank3=%02X\n",
                            c, c, de, ret,
                            pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                            pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                    last_c = c; last_de = de;
                }
            }
            cycles += z80_step(&pcw->cpu, &pcw->bus);
        }

        int req = asic_poll_fdc_irq(&pcw->asic);
        if (req == 1)      z80_nmi      (&pcw->cpu);
        else if (req == 2) z80_interrupt(&pcw->cpu);

        while (cycles >= next_tick) {
            kbd_tick(&pcw->kbd);
            /* Joyce-style periodic kbd-MCU scan: refresh the matrix bytes
             * in physical RAM block 3 at offset 0x3FF0..0x3FFF. */
            kbd_scan_into_ram(&pcw->kbd,
                &pcw->mem.ram[MEM_KBD_BLOCK * MEM_BLOCK_SIZE + MEM_KBD_OFFSET]);
            if (asic_timer_tick(&pcw->asic) && pcw->cpu.iff1)
                z80_interrupt(&pcw->cpu);
            next_tick += CYCLES_PER_TICK;
        }
    }

    if (crtc_frame(&pcw->crtc)) asic_frame(&pcw->asic);
}
