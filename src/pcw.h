#pragma once
#include "types.h"
#include "z80.h"
#include "mem.h"
#include "bootstrap.h"
#include "kbd.h"
#include "asic.h"
#include "fdc.h"
#include "crtc.h"
#include "printer.h"

/*
 * Top-level Amstrad PCW machine. Owns the Z80, all peripherals, and
 * the Z80Bus glue that routes memory and I/O accesses to the
 * appropriate subsystem. Mirrors 1984's CPC struct in spirit.
 */

typedef enum { PCW_MODEL_8256 = 0, PCW_MODEL_8512, PCW_MODEL_9512 } PcwModel;

typedef struct PCW {
    Z80        cpu;
    Z80Bus     bus;
    Mem        mem;
    Bootstrap  boot;
    Keyboard   kbd;
    Asic       asic;
    Fdc        fdc;
    Crtc       crtc;
    Printer    printer;

    PcwModel   model;
    int        memory_kb;

    /* Master gate for ALL debug stderr output, populated from
     * cfg.debug_traces. When false, none of the dev traces print
     * regardless of the per-channel sub-flags below. */
    bool       debug_traces;

    /* Per-channel sub-flags. Effective only when debug_traces is true. */
    bool       trace_io;
} PCW;

void pcw_init (PCW *pcw, PcwModel model, int memory_kb);
void pcw_reset(PCW *pcw);

/* Full cold boot: re-runs pcw_init with the given model and RAM size.
 * Use this when the user changes model or memory_kb from the overlay
 * — F5 (warm reset) only re-runs pcw_reset and would leave stale
 * paging / fdc / asic state from the previous configuration. */
void pcw_cold_boot(PCW *pcw, PcwModel model, int memory_kb);

/* Run one emulated frame's worth of cycles. */
void pcw_frame(PCW *pcw);
