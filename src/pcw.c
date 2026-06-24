#include "pcw.h"
#include <stdio.h>
#include <string.h>

u32 g_after_open_count = 0;

/* 4 MHz Z80, 50 Hz frame → 80,000 T-states per frame. */
#define CYCLES_PER_FRAME  80000

static u8 bus_mem_read(void *ctx, u16 addr) {
    return mem_read(&((PCW *)ctx)->mem, addr);
}

static void dump_pc_bytes(PCW *pcw, u16 pc, const char *tag) {
    if (!pcw->debug_traces) return;
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

static u16 dbg_read16(PCW *pcw, u16 addr) {
    return mem_read(&pcw->mem, addr) | (mem_read(&pcw->mem, (u16)(addr + 1)) << 8);
}

static void bus_mem_write(void *ctx, u16 addr, u8 val) {
    PCW *pcw = (PCW *)ctx;
    /* Seed-write trace was useful for the queue-investigation phase
     * but produces ~10M lines for a 12k-frame run, drowning all other
     * traces. Gated off by default; flip the macro to re-enable. */
#ifdef PCW_SEED_TRACE
    if ((addr >= 0x1010 && addr < 0x1018)
        || (addr >= 0x0D00 && addr < 0x0D10)
        || (addr >= 0x10A0 && addr < 0x10B0)
        || (addr >= 0x6D1B && addr < 0x6D20)) {
        u8 old = mem_read(&pcw->mem, addr);
        if (old != val) {
            int slot = addr >> 14;
            fprintf(stderr,
                "seed_write pc=%04X %04X %02X->%02X slot=%d rb=%02X wb=%02X bf=%02X\n",
                pcw->cpu.pc, addr, old, val, slot,
                pcw->mem.read_bank[slot], pcw->mem.write_bank[slot],
                pcw->mem.bank_force);
        }
    }
#endif
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

    /* CPS8256 SIO/Centronics add-on: DART A/B (E0-E3), 8253 (E4-E7),
     * Centronics data (E8). Active only when the user has plugged in
     * the backplane (or on the 9512 where it's built in). */
    if (lo >= 0xE0 && lo <= 0xE8 && pcw->cps.present) {
        u8 v = cps_read(&pcw->cps, lo);
        if (pcw->trace_io)
            fprintf(stderr, "        -> %02X (cps)\n", v);
        return v;
    }

    /* DK'tronics PCW Sound + Joystick — AY-3-8912 register read at 0xA9
     * (joystick byte when reg 14 is selected). */
    if ((lo == 0xA9) && pcw->ay.present) {
        u8 v = aysound_read(&pcw->ay, lo);
        if (pcw->trace_io)
            fprintf(stderr, "        -> %02X (ay)\n", v);
        return v;
    }

    /* Expansion port range 0x080-0x0EF. MAME pcw.cpp:580-625 returns
     * specific values for a few ports the firmware probes:
     *   0x85 -> 0xFE   0x87 -> 0xFF
     *   0xE1, 0xE3 -> 0x7F   (bit 7 clear => no expansion present)
     * Other ports in this range default to 0xFF (floating bus / no
     * peripheral). When the CPS8256 isn't present, returning 0x7F on
     * E1/E3 tells the firmware's probe "no expansion here". */
    if (lo >= 0x80 && lo <= 0xEF) {
        u8 v;
        switch (lo) {
            case 0x85: v = 0xFE; break;
            case 0x87: v = 0xFF; break;
            case 0xE1: case 0xE3: v = 0x7F; break;
            default:   v = 0xFF; break;
        }
        if (pcw->trace_io)
            fprintf(stderr, "        -> %02X (expansion)\n", v);
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
    if (lo >= 0xE0 && lo <= 0xE8 && pcw->cps.present) {
        cps_write(&pcw->cps, lo, val);
        return;
    }
    if ((lo == 0xAA || lo == 0xAB) && pcw->ay.present) {
        aysound_write(&pcw->ay, lo, val);
        return;
    }
}

void pcw_init(PCW *pcw, PcwModel model, int memory_kb) {
    memset(pcw, 0, sizeof(*pcw));

    pcw->model     = model;
    pcw->memory_kb = memory_kb;

    bootstrap_init(&pcw->boot);
    mem_init(&pcw->mem);
    mem_set_size_kb(&pcw->mem, memory_kb);
    pcw->mem.bootstrap = &pcw->boot;
    pcw->mem.kbd       = &pcw->kbd;

    kbd_init    (&pcw->kbd);
    fdc_init    (&pcw->fdc);
    crtc_init   (&pcw->crtc);
    printer_init(&pcw->printer);
    asic_init   (&pcw->asic, &pcw->boot, &pcw->fdc);
    /* Backend lifecycle is unconditional; the actual pty/tcp setup
     * is wired by main/overlay based on the live config. */
    pcw->serial.pty_master = -1;
    pcw->serial.tcp_listen = -1;
    pcw->serial.tcp_client = -1;

    /* CPS8256 SIO/Centronics. Present-state is set by main/overlay
     * after init based on model + backplane. Channel A reads/writes
     * route through pcw->serial (or through pcw->perryfi when the
     * PerryFi extension is plugged in). */
    cps_init(&pcw->cps, false, &pcw->serial, &pcw->perryfi, &pcw->printer);
    perryfi_init(&pcw->perryfi, false);
    /* DK'tronics PCW Sound + Joystick — present-state set by main/
     * overlay after init based on cfg + backplane gating. */
    aysound_init(&pcw->ay, false);

    pcw->bus.mem_read  = bus_mem_read;
    pcw->bus.mem_write = bus_mem_write;
    pcw->bus.io_read   = bus_io_read;
    pcw->bus.io_write  = bus_io_write;
    pcw->bus.tick      = NULL;
    pcw->bus.ctx       = pcw;

    z80_init(&pcw->cpu);
    pcw_reset(pcw);
}

void pcw_cold_boot(PCW *pcw, PcwModel model, int memory_kb) {
    pcw_init(pcw, model, memory_kb);
}

void pcw_reset(PCW *pcw) {
    bootstrap_reset(&pcw->boot);
    mem_reset(&pcw->mem);
    fdc_reset(&pcw->fdc);
    crtc_reset(&pcw->crtc);
    asic_reset(&pcw->asic);
    z80_reset(&pcw->cpu);

    /* Copy the boot ROM bytes into BANK 0 RAM at offset 0. The real
     * PCW does this via its printer-MCU ROM at reset. Reads of low
     * memory then return RAM (which initially contains the ROM bytes),
     * and the boot ROM's self-modifying-code trick (writing D3 F8 at
     * RAM[0,1] then JP 0000 to execute the new instruction) works
     * naturally. ZEsarUX models this the same way. */
    memcpy(pcw->mem.ram, pcw->boot.stream, (size_t)pcw->boot.len);
}

/* 4 MHz / 300 Hz ≈ 13_333 cycles per timer tick. The PCW asserts /INT
 * 300 times per second from a periodic timer (MAME pcw.cpp:1297); the
 * BIOS polls the F8 interrupt counter to schedule its per-frame work. */
#define CYCLES_PER_TICK   (CYCLES_PER_FRAME / 6)

void pcw_frame(PCW *pcw) {
    /* Drain the serial backend(s) once per emulated frame — same
     * cadence 1984 uses for USIfAC. */
    serial_poll(&pcw->serial);
    perryfi_poll(&pcw->perryfi);

    /* F8 monitor controls: paused freezes the CPU; step_once lets one
     * instruction through then re-pauses. Breakpoints are checked
     * after each z80_step inside the loop below. */
    if (pcw->paused && !pcw->step_once) return;
    bool was_stepping = pcw->step_once;
    pcw->step_once = false;
    bool stop_early = false;

    int cycles = 0;
    int next_tick = CYCLES_PER_TICK;

    while (cycles < CYCLES_PER_FRAME && !stop_early) {
        if (pcw->cpu.halted) {
            /* Skip straight to the next tick — IRQ will wake the CPU. */
            cycles = next_tick;
        } else {
          /* Dev-only stderr probes, gated behind the master debug-traces
           * flag (toggled at runtime from the Advanced overlay). */
          if (pcw->debug_traces) {
            /* Foreground-PC histogram: after BDOS f=0F call, sample
             * the foreground PC (= where the CPU would be if no IRQ
             * were active). Detect "foreground" as iff1=1 AND the
             * current PC NOT in the ISR range 0x0770..0x08FF. */
            {
                static u32 hist[64] = {0};
                static u16 hist_pcs[64] = {0};
                static int hist_n = 0;
                static u32 sample_count = 0;
                extern u32 g_after_open_count;
                if (g_after_open_count > 0 && pcw->cpu.iff1
                    && (pcw->cpu.pc < 0x0770 || pcw->cpu.pc >= 0x0900)) {
                    sample_count++;
                    if ((sample_count & 0xFF) == 0) {
                        u16 p = pcw->cpu.pc;
                        int found = -1;
                        for (int i = 0; i < hist_n; i++) if (hist_pcs[i] == p) { found = i; break; }
                        if (found < 0 && hist_n < 64) { found = hist_n++; hist_pcs[found] = p; hist[found] = 0; }
                        if (found >= 0) hist[found]++;
                        if ((sample_count & 0xFFFF) == 0) {
                            fprintf(stderr, "fg_hist (samples=%u):", sample_count);
                            for (int i = 0; i < hist_n; i++) fprintf(stderr, " %04X:%u", hist_pcs[i], hist[i]);
                            fputc('\n', stderr);
                        }
                    }
                }
            }
            {
                static u32 trace_51a8 = 0;
                static u32 trace_5180 = 0;
                static u32 trace_4d44 = 0;
                extern u32 g_after_open_count;
                if (g_after_open_count > 0) {
                    static bool trace_hot_dumped = false;
                    static u32 trace_hot = 0;
                    if (pcw->cpu.pc >= 0x5170 && pcw->cpu.pc <= 0x51A8 && trace_hot < 96) {
                        if (!trace_hot_dumped) {
                            trace_hot_dumped = true;
                            fprintf(stderr, "fg_hot_bytes banks=%02X/%02X/%02X/%02X @5170:",
                                pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                            for (u16 a = 0x5170; a < 0x51B0; a++)
                                fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
                            fputc('\n', stderr);
                        }
                        trace_hot++;
                        fprintf(stderr,
                            "fg_hot#%u pc=%04X op=%02X AF=%04X HL=%04X DE=%04X BC=%04X SP=%04X "
                            "6C49=%04X 6C4B=%04X 6C55=%04X 6C68=%04X 6C39=%02X 6C63=%02X banks=%02X/%02X/%02X/%02X\n",
                            trace_hot, pcw->cpu.pc, mem_read(&pcw->mem, pcw->cpu.pc),
                            pcw->cpu.af, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc, pcw->cpu.sp,
                            dbg_read16(pcw, 0x6C49), dbg_read16(pcw, 0x6C4B),
                            dbg_read16(pcw, 0x6C55), dbg_read16(pcw, 0x6C68),
                            mem_read(&pcw->mem, 0x6C39), mem_read(&pcw->mem, 0x6C63),
                            pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                            pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                    }
                    {
                        static bool trace_bb_dumped = false;
                        static u32 trace_bb = 0;
                        if (pcw->cpu.pc >= 0xBB80 && pcw->cpu.pc <= 0xBBD0 && trace_bb < 96) {
                            if (!trace_bb_dumped) {
                                trace_bb_dumped = true;
                                fprintf(stderr, "seldsk_target_bytes banks=%02X/%02X/%02X/%02X @BB80:",
                                    pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                    pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                                for (u16 a = 0xBB80; a < 0xBBD0; a++)
                                    fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
                                fputc('\n', stderr);
                            }
                            trace_bb++;
                            fprintf(stderr,
                                "seldsk_target#%u pc=%04X op=%02X AF=%04X HL=%04X DE=%04X BC=%04X SP=%04X "
                                "FE70=%02X FE73=%04X FE77=%04X banks=%02X/%02X/%02X/%02X\n",
                                trace_bb, pcw->cpu.pc, mem_read(&pcw->mem, pcw->cpu.pc),
                                pcw->cpu.af, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc, pcw->cpu.sp,
                                mem_read(&pcw->mem, 0xFE70), dbg_read16(pcw, 0xFE73),
                                dbg_read16(pcw, 0xFE77),
                                pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                        }
                    }
                    {
                        static bool trace_bc_dumped = false;
                        static u32 trace_bc = 0;
                        if (pcw->cpu.pc >= 0xBC00 && pcw->cpu.pc <= 0xBC80 && trace_bc < 128) {
                            if (!trace_bc_dumped) {
                                trace_bc_dumped = true;
                                fprintf(stderr, "seldsk_bc_bytes banks=%02X/%02X/%02X/%02X @BC00:",
                                    pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                    pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                                for (u16 a = 0xBC00; a < 0xBC80; a++)
                                    fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
                                fputc('\n', stderr);
                            }
                            trace_bc++;
                            fprintf(stderr,
                                "seldsk_bc#%u pc=%04X op=%02X AF=%04X HL=%04X DE=%04X BC=%04X IX=%04X IY=%04X SP=%04X "
                                "BE10=%02X BE17=%02X banks=%02X/%02X/%02X/%02X\n",
                                trace_bc, pcw->cpu.pc, mem_read(&pcw->mem, pcw->cpu.pc),
                                pcw->cpu.af, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc,
                                pcw->cpu.ix, pcw->cpu.iy, pcw->cpu.sp,
                                mem_read(&pcw->mem, 0xBE10), mem_read(&pcw->mem, 0xBE17),
                                pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                        }
                    }
                    {
                        static bool trace_low_dumped = false;
                        static u32 trace_low = 0;
                        if (pcw->cpu.pc >= 0x0080 && pcw->cpu.pc <= 0x00B0 && trace_low < 128) {
                            if (!trace_low_dumped) {
                                trace_low_dumped = true;
                                fprintf(stderr, "low_helper_bytes banks=%02X/%02X/%02X/%02X @0080:",
                                    pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                    pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                                for (u16 a = 0x0080; a < 0x00C0; a++)
                                    fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
                                fputc('\n', stderr);
                            }
                            trace_low++;
                            fprintf(stderr,
                                "low_helper#%u pc=%04X op=%02X AF=%04X HL=%04X DE=%04X BC=%04X IX=%04X IY=%04X SP=%04X "
                                "banks=%02X/%02X/%02X/%02X fdc_phase=%d irq=%d tc=%d\n",
                                trace_low, pcw->cpu.pc, mem_read(&pcw->mem, pcw->cpu.pc),
                                pcw->cpu.af, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc,
                                pcw->cpu.ix, pcw->cpu.iy, pcw->cpu.sp,
                                pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                pcw->mem.read_bank[2], pcw->mem.read_bank[3],
                                pcw->fdc.phase, pcw->fdc.irq, pcw->fdc.tc);
                        }
                    }
                    {
                        static bool trace_4a_dumped = false;
                        static u32 trace_4a = 0;
                        if (pcw->cpu.pc >= 0x4A60 && pcw->cpu.pc <= 0x4AD0 && trace_4a < 160) {
                            if (!trace_4a_dumped) {
                                trace_4a_dumped = true;
                                fprintf(stderr, "routine_4a_bytes banks=%02X/%02X/%02X/%02X @4A60:",
                                    pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                    pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                                for (u16 a = 0x4A60; a < 0x4AE0; a++)
                                    fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
                                fputc('\n', stderr);
                            }
                            trace_4a++;
                            fprintf(stderr,
                                "routine_4a#%u pc=%04X op=%02X AF=%04X HL=%04X DE=%04X BC=%04X IX=%04X IY=%04X SP=%04X "
                                "banks=%02X/%02X/%02X/%02X\n",
                                trace_4a, pcw->cpu.pc, mem_read(&pcw->mem, pcw->cpu.pc),
                                pcw->cpu.af, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc,
                                pcw->cpu.ix, pcw->cpu.iy, pcw->cpu.sp,
                                pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                        }
                    }
                    {
                        static bool trace_4a20_dumped = false;
                        static u32 trace_4a20 = 0;
                        if (pcw->cpu.pc >= 0x49D0 && pcw->cpu.pc <= 0x4A60 && trace_4a20 < 192) {
                            if (!trace_4a20_dumped) {
                                trace_4a20_dumped = true;
                                fprintf(stderr, "routine_4a20_bytes banks=%02X/%02X/%02X/%02X @49D0:",
                                    pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                    pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                                for (u16 a = 0x49D0; a < 0x4A60; a++)
                                    fprintf(stderr, " %02X", mem_read(&pcw->mem, a));
                                fputc('\n', stderr);
                            }
                            trace_4a20++;
                            fprintf(stderr,
                                "routine_4a20#%u pc=%04X op=%02X AF=%04X HL=%04X DE=%04X BC=%04X IX=%04X IY=%04X SP=%04X "
                                "banks=%02X/%02X/%02X/%02X\n",
                                trace_4a20, pcw->cpu.pc, mem_read(&pcw->mem, pcw->cpu.pc),
                                pcw->cpu.af, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc,
                                pcw->cpu.ix, pcw->cpu.iy, pcw->cpu.sp,
                                pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                                pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                        }
                    }
                    if (pcw->cpu.pc == 0x51A8 && trace_51a8 < 32) {
                        trace_51a8++;
                        fprintf(stderr,
                            "b8_setup#%u pc=51A8 HL=%04X DE=%04X BC=%04X SP=%04X "
                            "6C49=%04X 6C4B=%04X 6C55=%04X 6C68=%04X 6C39=%02X 6C63=%02X banks=%02X/%02X/%02X/%02X\n",
                            trace_51a8, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc, pcw->cpu.sp,
                            dbg_read16(pcw, 0x6C49), dbg_read16(pcw, 0x6C4B),
                            dbg_read16(pcw, 0x6C55), dbg_read16(pcw, 0x6C68),
                            mem_read(&pcw->mem, 0x6C39), mem_read(&pcw->mem, 0x6C63),
                            pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                            pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                    }
                    if (pcw->cpu.pc == 0x5180 && trace_5180 < 64) {
                        trace_5180++;
                        fprintf(stderr,
                            "b8_loop#%u pc=5180 HL=%04X DE=%04X BC=%04X A=%02X SP=%04X "
                            "6C49=%04X 6C4B=%04X 6C55=%04X 6C68=%04X\n",
                            trace_5180, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc,
                            (u8)(pcw->cpu.af >> 8), pcw->cpu.sp,
                            dbg_read16(pcw, 0x6C49), dbg_read16(pcw, 0x6C4B),
                            dbg_read16(pcw, 0x6C55), dbg_read16(pcw, 0x6C68));
                    }
                    if (pcw->cpu.pc == 0x4D44 && trace_4d44 < 64) {
                        trace_4d44++;
                        fprintf(stderr,
                            "b8_coro#%u pc=4D44 SP=%04X 6C49=%04X 6C4B=%04X HL=%04X DE=%04X BC=%04X\n",
                            trace_4d44, pcw->cpu.sp,
                            dbg_read16(pcw, 0x6C49), dbg_read16(pcw, 0x6C4B),
                            pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc);
                    }
                }
            }
            /* BIOS jumpblock trace: CP/M+ BIOS jumpblock lives in the
             * high common area. Catch CALL/JP instructions whose
             * immediate destination is in the FCxx..FFxx range. */
            {
                u8 op = mem_read(&pcw->mem, pcw->cpu.pc);
                if (op == 0xCD || op == 0xC3 || op == 0xC4 || op == 0xCC
                    || op == 0xD4 || op == 0xDC || op == 0xE4 || op == 0xEC
                    || op == 0xF4 || op == 0xFC) {
                    u16 tgt = mem_read(&pcw->mem, (u16)(pcw->cpu.pc+1))
                            | (mem_read(&pcw->mem, (u16)(pcw->cpu.pc+2)) << 8);
                    if (tgt >= 0xFC00) {
                        static u16 last_pc = 0; static u16 last_tgt = 0;
                        if (pcw->cpu.pc != last_pc || tgt != last_tgt) {
                            fprintf(stderr, "bios call op=%02X pc=%04X -> %04X (C=%02X HL=%04X DE=%04X BC=%04X)\n",
                                    op, pcw->cpu.pc, tgt,
                                    pcw->cpu.c, pcw->cpu.hl, pcw->cpu.de, pcw->cpu.bc);
                            last_pc = pcw->cpu.pc; last_tgt = tgt;
                        }
                    }
                }
            }
            /* BDOS-call trace: CP/M+ apps enter BDOS via CALL 5.
             * At PC=5, C = function, DE = parameter. Return address
             * is on the stack (top word). Also catch returns. */
            static u16 g_bdos_pending_ret = 0xFFFF;
            static u8  g_bdos_pending_fn  = 0xFF;
            static u32 g_bdos_call_count  = 0;
            if (pcw->cpu.pc == g_bdos_pending_ret && g_bdos_pending_ret != 0xFFFF) {
                fprintf(stderr, "bdos return fn=%02X -> PC=%04X HL=%04X A=%02X (BC=%04X DE=%04X)\n",
                        g_bdos_pending_fn, pcw->cpu.pc,
                        pcw->cpu.hl, (u8)(pcw->cpu.af >> 8),
                        pcw->cpu.bc, pcw->cpu.de);
                g_bdos_pending_ret = 0xFFFF;
            }
            if (pcw->cpu.pc == 0x0005) {
                static u32 last = 0;
                static u8  last_c = 0xFF; static u16 last_de = 0;
                u8  c = pcw->cpu.c;
                u16 de = pcw->cpu.de;
                u16 sp = pcw->cpu.sp;
                u16 ret = mem_read(&pcw->mem, sp)
                        | (mem_read(&pcw->mem, (u16)(sp+1)) << 8);
                g_bdos_pending_ret = ret;
                g_bdos_pending_fn  = c;
                g_bdos_call_count++;
                /* De-dupe immediate repeats so polled functions don't drown the log. */
                if (c == 0x0F) g_after_open_count++;
                if (++last == 1 || c != last_c || de != last_de) {
                    fprintf(stderr, "bdos call#%u f=%02X (C=%02X) DE=%04X ret=%04X bank0=%02X bank1=%02X bank2=%02X bank3=%02X\n",
                            g_bdos_call_count, c, c, de, ret,
                            pcw->mem.read_bank[0], pcw->mem.read_bank[1],
                            pcw->mem.read_bank[2], pcw->mem.read_bank[3]);
                    /* For OPEN/MAKE/SEARCH FILE calls, dump the FCB (12 bytes
                     * starting at DE: drive + 8-char name + 3-char ext). */
                    if (c == 0x0F || c == 0x16 || c == 0x11 || c == 0x12 || c == 0x13) {
                        fprintf(stderr, "  fcb@%04X drv=%02X name='", de, mem_read(&pcw->mem, de));
                        for (int i = 1; i <= 11; i++) {
                            u8 b = mem_read(&pcw->mem, (u16)(de+i)) & 0x7F;
                            fputc(b < 0x20 || b > 0x7E ? '.' : b, stderr);
                            if (i == 8) fputc('.', stderr);
                        }
                        fputc('\'', stderr); fputc('\n', stderr);
                    }
                    last_c = c; last_de = de;
                }
            }
          }  /* end pcw->debug_traces gate */
            cycles += z80_step(&pcw->cpu, &pcw->bus);

            /* Monitor breakpoints: stop the frame as soon as the PC
             * lands on any armed breakpoint. */
            for (int b = 0; b < PCW_MAX_BREAKPOINTS; b++) {
                if (pcw->bp_enabled[b] && pcw->cpu.pc == pcw->breakpoints[b]) {
                    pcw->paused = true;
                    stop_early  = true;
                    break;
                }
            }
            if (stop_early || was_stepping) break;
        }

        /* Re-assert IRQ only while iff1=1. If the ISR is mid-flight
         * (iff1=0 after accept), holding the line high would re-fire
         * IRQ as soon as a single instruction inside the ISR happens
         * to EI, before the FDC result phase has been drained. NMI is
         * not gated since NMI accept is unconditional. */
        int req = asic_poll_fdc_irq(&pcw->asic);
        if (req == 1)      z80_nmi      (&pcw->cpu);
        else if (req == 2 && pcw->cpu.iff1) z80_interrupt(&pcw->cpu);

        while (cycles >= next_tick) {
            kbd_tick(&pcw->kbd);
            /* Joyce-style periodic kbd-MCU scan: refresh the matrix bytes
             * in physical RAM block 3 at offset 0x3FF0..0x3FFF. */
            kbd_scan_into_ram(&pcw->kbd,
                &pcw->mem.ram[MEM_KBD_BLOCK * MEM_BLOCK_SIZE + MEM_KBD_OFFSET]);
            if (asic_timer_tick(&pcw->asic)) {
                /* CP/M checks FDC before timer when sources overlap. Keep
                 * counting timer ticks in F4, but do not queue a competing
                 * one-shot /INT while the level-held FDC source is active. */
                bool fdc_busy = pcw->fdc.phase != FDC_PHASE_IDLE
                             || pcw->fdc.irq
                             || pcw->fdc.irq_arm_ticks > 0;
                if (!fdc_busy) z80_interrupt(&pcw->cpu);
            }
            next_tick += CYCLES_PER_TICK;
        }
    }

    if (crtc_frame(&pcw->crtc)) asic_frame(&pcw->asic);
}
