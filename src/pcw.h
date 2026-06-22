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

    /* Master gate for ALL debug stderr output, populated from
     * cfg.debug_traces. When false, none of the dev traces print
     * regardless of the per-channel sub-flags below. */
    bool       debug_traces;

    /* Per-channel sub-flags. Effective only when debug_traces is true. */
    bool       trace_io;
} PCW;

void pcw_init (PCW *pcw, PcwModel model);
void pcw_reset(PCW *pcw);

/* Run one emulated frame's worth of cycles. */
void pcw_frame(PCW *pcw);
