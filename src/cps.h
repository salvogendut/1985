/* cps — Amstrad CPS8256 SIO / Centronics add-on.
 *
 * The CPS8256 is an Amstrad-branded expansion that plugs into the PCW
 * 8256/8512 50-pin edge connector (i.e. through the SanPollo backplane
 * in 1985's model) and the 9512 had its surface built into the main
 * board. It exposes:
 *
 *   0xE0 / 0xE1   Z80-DART channel A data / control  (serial)
 *   0xE2 / 0xE3   Z80-DART channel B data / control  (Centronics: DTR
 *                                                     is /STROBE, CTS
 *                                                     is /BUSY)
 *   0xE4 .. 0xE7  Intel 8253 PIT — used as baud-rate generator
 *   0xE8          Centronics data latch
 *
 * CP/M+ probes these ports during boot; when it sees a responding DART
 * it advertises "SIO/Centronics add-on" in the banner.
 *
 * Behaviour ported from Joyce 2.4.2 (bin/JoyceCPS.cxx): Z80-DART
 * register-select state machine, RR0/RR1 status bits, the 8253
 * latched-write protocol, and channel B's STROBE/BUSY semantics.
 */
#pragma once

#include "types.h"
#include <stdbool.h>

struct Serial;
struct Perryfi;
struct Printer;

typedef enum { CPS_DART_MODE_REGISTER = 0, CPS_DART_MODE_DATA = 1 } CpsDartMode;

typedef struct {
    /* Write registers (WR0..WR7) and read registers (RR0..RR2). */
    u8  reg[8];
    u8  rreg[3];
    CpsDartMode regmode;
    u8  regno;
    u8  latch;   /* last data byte */
} CpsDart;

typedef struct Cps {
    bool present;            /* mirror of cfg→model + ext gating */

    CpsDart dartA;           /* serial */
    CpsDart dartB;           /* Centronics control */

    /* 8253 PIT — TX/RX divisors latched via the two-byte WR protocol on
     * counters 0/1. We don't actually time the bit shifting; the kept
     * values are reported back so probing firmware sees a stable chip. */
    u16 baud_tx;
    u16 baud_rx;
    u8  baud_mode;           /* 0 idle, 1/2 tx pair, 3/4 rx pair */
    u8  baud_buf;            /* low byte staged for the next high-byte write */

    /* Centronics data latch (port 0xE8) and the most recent /STROBE
     * edge state from DART B's DTR. */
    u8   parallel_data;
    bool parallel_strobe;    /* current DTR/STROBE level */

    /* Backends — both borrowed. Channel A's data port routes through
     * `perryfi` when its `present` flag is set, otherwise through the
     * raw `serial` (pty/tcp) backend. */
    struct Serial  *serial;
    struct Perryfi *perryfi;
} Cps;

/* Borrowed pointers — lifetimes managed by the caller. Pass NULL to
 * leave the dart-A side unwired (smoke tests). */
void cps_init(Cps *c, bool present, struct Serial *serial, struct Perryfi *perryfi);
void cps_reset(Cps *c);
void cps_set_present(Cps *c, bool present);

/* Port I/O for the 0xE0..0xE8 range. `lo` is the low byte of the
 * 16-bit I/O address. Reads of an unmapped port in this range return
 * 0xFF; writes are ignored. */
u8   cps_read (Cps *c, u8 lo);
void cps_write(Cps *c, u8 lo, u8 val);
