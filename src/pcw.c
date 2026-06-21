#include "pcw.h"
#include <stdio.h>
#include <string.h>

/* 4 MHz Z80, 50 Hz frame → 80,000 T-states per frame. */
#define CYCLES_PER_FRAME  80000

static u8 bus_mem_read(void *ctx, u16 addr) {
    return mem_read(&((PCW *)ctx)->mem, addr);
}

static void bus_mem_write(void *ctx, u16 addr, u8 val) {
    mem_write(&((PCW *)ctx)->mem, addr, val);
}

static u8 bus_io_read(void *ctx, u16 port) {
    PCW *pcw = (PCW *)ctx;
    u8 lo = (u8)(port & 0xFF);
    if (pcw->trace_io)
        fprintf(stderr, "io_read  %04X (lo=%02X)\n", port, lo);

    /* FDC: A0 selects MSR vs Data, mirrored across 0x00..0x7F
     * (A1..A6 are don't-care, A7=0 selects the FDC window).
     * MAME pcw.cpp: map(0x000, 0x001).mirror(0x7e). */
    if (lo < 0x80) return fdc_read(&pcw->fdc, lo & 0x01);

    /* ASIC system control and video registers. */
    if (lo == 0xF4 || lo == 0xF8) return asic_read(&pcw->asic, lo);

    /* Printer status. */
    if (lo == 0xFC || lo == 0xFD) return printer_read(&pcw->printer, lo);

    return 0xFF;
}

static void bus_io_write(void *ctx, u16 port, u8 val) {
    PCW *pcw = (PCW *)ctx;
    u8 lo = (u8)(port & 0xFF);
    if (pcw->trace_io)
        fprintf(stderr, "io_write %04X=%02X\n", port, val);

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
            cycles += z80_step(&pcw->cpu, &pcw->bus);
        }

        int req = asic_poll_fdc_irq(&pcw->asic);
        if (req == 1)      z80_nmi      (&pcw->cpu);
        else if (req == 2) z80_interrupt(&pcw->cpu);

        while (cycles >= next_tick) {
            if (asic_timer_tick(&pcw->asic)) z80_interrupt(&pcw->cpu);
            next_tick += CYCLES_PER_TICK;
        }
    }

    if (crtc_frame(&pcw->crtc)) asic_frame(&pcw->asic);
}
