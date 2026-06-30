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
#include "serial.h"
#include "cps.h"
#include "perryfi.h"
#include "aysound.h"
#include "beeper.h"
#include "multilink.h"
#include "pcwmouse.h"

/*
 * Top-level Amstrad PCW machine. Owns the Z80, all peripherals, and
 * the Z80Bus glue that routes memory and I/O accesses to the
 * appropriate subsystem. Mirrors 1984's CPC struct in spirit.
 */

typedef enum { PCW_MODEL_8256 = 0, PCW_MODEL_8512, PCW_MODEL_9512 } PcwModel;

/* Live host-joystick state. main.c refreshes it from the gamepad each
 * frame; pcw.c bus_io_read packs it to the wire format dictated by
 * `type` when 0x9F (Kempston) or 0xE0 (Cascade) is polled. DKsound
 * still goes through aysound.c via the AY's port-A register. */
typedef struct {
    JoystickType type;
    bool up, down, left, right, fire1, fire2;
} Joystick;

#define PCW_MAX_BREAKPOINTS 16

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
    Serial     serial;
    Cps        cps;
    Perryfi    perryfi;
    AySound    ay;
    Beeper     beeper;            /* PCW built-in 3.75 kHz beeper (F8 cmd 0x0B/0x0C) */
    Multilink  multilink;         /* Multilink probe-stub on ports 0xA6/0xA7 */
    PcwMouse   mouse;
    Joystick   joystick;

    PcwModel   model;
    int        memory_kb;

    /* Doubles the cycle budget per emulated frame so the Z80 runs at
     * an effective 8 MHz vs the stock 4 MHz. The 300 Hz interrupt
     * tick is wall-clock based on the real machine, so its cycle
     * interval doubles in lock-step — guests still see /INT 300×/s. */
    bool       turbo;

    /* Master gate for ALL debug stderr output, populated from
     * cfg.debug_traces. When false, none of the dev traces print
     * regardless of the per-channel sub-flags below. */
    bool       debug_traces;

    /* Per-channel sub-flags. Effective only when debug_traces is true. */
    bool       trace_io;

    /* F8 memory-monitor controls — mirrors 1984's CPC monitor state. */
    bool       paused;
    bool       step_once;
    u16        breakpoints[PCW_MAX_BREAKPOINTS];
    bool       bp_enabled [PCW_MAX_BREAKPOINTS];
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
