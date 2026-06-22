/* cps.c — CPS8256 SIO/Centronics add-on. See cps.h.
 *
 * Z80-DART state machine + channel-A serial wiring + channel-B
 * Centronics STROBE/BUSY ported from Joyce 2.4.2 (bin/JoyceCPS.cxx). */

#include "cps.h"
#include "serial.h"
#include "perryfi.h"
#include "leds.h"

#include <stdio.h>
#include <string.h>

/* RR0 bits we synthesise from the Serial backend (channel A). */
#define RR0_RX_AVAIL       0x01
#define RR0_TX_BUF_EMPTY   0x04
#define RR0_DCD            0x08
#define RR0_RI             0x10
#define RR0_CTS            0x20
#define RR0_BREAK          0x80

/* RR1 bits for channel A. */
#define RR1_ALL_SENT       0x01

/* WR5 control bits we partially honour. */
#define WR5_RTS            0x02
#define WR5_TX_ENABLE      0x08
#define WR5_SEND_BREAK     0x10
#define WR5_DTR            0x80

/* ---------------------------------------------------------------- */
/* Z80-DART register dispatch — shared by both channels.            */

static void dart_reset(CpsDart *d) {
    d->regmode = CPS_DART_MODE_REGISTER;
    d->regno   = 0;
    d->latch   = 0;
    memset(d->reg,  0, sizeof(d->reg));
    memset(d->rreg, 0, sizeof(d->rreg));
}

/* Generic register write — bottom 3 bits of WR0 select the next
 * register to write/read; otherwise reset/interrupt commands. */
static void dart_out_reg(CpsDart *d, u8 reg, u8 value) {
    if (reg == 0) {
        switch ((value >> 3) & 7) {
            case 0:  break;                /* null code */
            case 3:  dart_reset(d); break; /* channel reset */
            default: break;                /* int-control commands: no-op */
        }
    }
    d->reg[reg & 7] = value;
}

/* Channel-agnostic write through the data/control pair. Channel-
 * specific side effects (RTS/DTR, strobe edge, send-break) are
 * applied by the callers below before delegating here. */
static void dart_out_ctrl(CpsDart *d, u8 b) {
    switch (d->regmode) {
        case CPS_DART_MODE_REGISTER:
            d->regno = (b & 7);
            if (d->regno) {
                d->regmode = CPS_DART_MODE_DATA;
                /* still record the WR0 command on this write */
                dart_out_reg(d, 0, b);
            } else {
                dart_out_reg(d, 0, b);
            }
            break;
        case CPS_DART_MODE_DATA:
            dart_out_reg(d, d->regno, b);
            d->regmode = CPS_DART_MODE_REGISTER;
            break;
    }
}

static u8 dart_in_reg(const CpsDart *d, u8 reg) {
    if (reg < 2) return d->rreg[reg];
    if (reg == 2) return d->reg[2];     /* interrupt vector mirror */
    return 0xFF;
}

static u8 dart_in_ctrl(CpsDart *d) {
    u8 v;
    switch (d->regmode) {
        case CPS_DART_MODE_REGISTER:
            v = dart_in_reg(d, 0);
            break;
        case CPS_DART_MODE_DATA:
            v = dart_in_reg(d, d->regno);
            d->regmode = CPS_DART_MODE_REGISTER;
            break;
        default:
            v = 0xFF;
            break;
    }
    return v;
}

/* ---------------------------------------------------------------- */
/* Channel A — serial.                                              */

/* Pick the live channel-A endpoint: PerryFi steals the line when its
 * `present` flag is set; otherwise the raw pty/tcp backend wins. */
static bool a_rx_has(const Cps *c) {
    if (c->perryfi && c->perryfi->present) return perryfi_rx_has(c->perryfi);
    if (c->serial  && c->serial->present)  return serial_rx_has(c->serial);
    return false;
}
static bool a_rx_pop(Cps *c, u8 *out) {
    if (c->perryfi && c->perryfi->present) return perryfi_rx_pop(c->perryfi, out);
    if (c->serial  && c->serial->present)  return serial_rx_pop(c->serial, out);
    return false;
}
static bool a_tx_push(Cps *c, u8 b) {
    if (c->perryfi && c->perryfi->present) return perryfi_tx_push(c->perryfi, b);
    if (c->serial  && c->serial->present)  return serial_tx_push(c->serial, b);
    return false;
}

static void update_rr_serial(Cps *c) {
    CpsDart *a = &c->dartA;
    /* RR0: clear the bits we drive, then re-assert based on backend. */
    a->rreg[0] &= ~(RR0_RX_AVAIL | RR0_TX_BUF_EMPTY | RR0_CTS);
    if (a_rx_has(c)) a->rreg[0] |= RR0_RX_AVAIL;
    a->rreg[0] |= RR0_TX_BUF_EMPTY;     /* we never back up */
    a->rreg[0] |= RR0_CTS;              /* assume cleared-to-send */
    a->rreg[1] = (a->rreg[1] & ~RR1_ALL_SENT) | RR1_ALL_SENT;
}

static u8 dartA_in_ctrl(Cps *c) {
    update_rr_serial(c);
    return dart_in_ctrl(&c->dartA);
}

static u8 dartA_in_data(Cps *c) {
    u8 b = 0;
    if (a_rx_pop(c, &b))
        leds_ping_split(LED_SERIAL, false);   /* RX (red) — host/modem → PCW */
    c->dartA.latch = b;
    return b;
}

static void dartA_out_data(Cps *c, u8 val) {
    c->dartA.latch = val;
    if (a_tx_push(c, val))
        leds_ping_split(LED_SERIAL, true);    /* TX (green) — PCW → host/modem */
}

static void dartA_out_ctrl(Cps *c, u8 val) {
    /* When the data-byte selector lands on WR5 / WR4 / WR3, we'd be
     * mirroring Joyce's modem-control mappings. The Serial backend
     * doesn't model RTS/DTR/parity/stop bits, so we just record the
     * register write through the generic path. */
    dart_out_ctrl(&c->dartA, val);
}

/* ---------------------------------------------------------------- */
/* Channel B — Centronics control. CTS reflects /BUSY, DTR drives
 * /STROBE; a high→low DTR transition latches the byte in 0xE8 to
 * the parallel output stream. */

static void update_rr_centronics(CpsDart *b, const Cps *c) {
    /* CTS: parallel port is "ready" unless we model a busy printer.
     * We don't model one yet, so always ready. */
    b->rreg[0] &= ~RR0_CTS;
    b->rreg[0] |= RR0_CTS;
    (void)c;
}

static u8 dartB_in_ctrl(Cps *c) {
    update_rr_centronics(&c->dartB, c);
    return dart_in_ctrl(&c->dartB);
}

static u8 dartB_in_data(Cps *c) {
    return c->dartB.latch;
}

static void dartB_out_data(Cps *c, u8 val) {
    c->dartB.latch = val;
}

static void dartB_out_ctrl(Cps *c, u8 val) {
    /* Detect a falling DTR edge on WR5 — that's how the firmware
     * latches the byte at port 0xE8 to the external parallel port. */
    u8 prev_wr5_dtr = c->dartB.reg[5] & WR5_DTR;
    dart_out_ctrl(&c->dartB, val);
    u8 curr_wr5_dtr = c->dartB.reg[5] & WR5_DTR;
    if (prev_wr5_dtr && !curr_wr5_dtr) {
        /* /STROBE fell — flush c->parallel_data to the host. We don't
         * have a Centronics sink wired yet, so just drop the byte
         * here. (Joyce calls dropStrobe() → writeChar() into a
         * printer text file.) */
        (void)c->parallel_data;
    }
    c->parallel_strobe = (curr_wr5_dtr != 0);
}

/* ---------------------------------------------------------------- */
/* 8253 PIT (counters 0 and 1 used as baud generators).             */

static void pit_write_counter(Cps *c, u8 val) {
    /* Each counter is loaded as low-byte then high-byte after a mode
     * 0x36 (counter 0, mode 3, LSB then MSB) or 0x76 (counter 1, ...)
     * write to the control port. Joyce mirrors this exactly. */
    switch (c->baud_mode) {
        case 1: case 3:
            c->baud_buf = val;
            c->baud_mode++;
            break;
        case 2: {
            c->baud_tx  = (u16)c->baud_buf | ((u16)val << 8);
            c->baud_mode = 0;
            break;
        }
        case 4: {
            c->baud_rx  = (u16)c->baud_buf | ((u16)val << 8);
            c->baud_mode = 0;
            break;
        }
        default: break;     /* idle — ignore stray writes */
    }
}

static void pit_write_ctrl(Cps *c, u8 val) {
    if      (val == 0x36) c->baud_mode = 1;   /* select counter 0 (TX) */
    else if (val == 0x76) c->baud_mode = 3;   /* select counter 1 (RX) */
    /* Other modes: we don't drive interrupts or strobed I/O. */
}

/* ---------------------------------------------------------------- */
/* Public API.                                                      */

void cps_init(Cps *c, bool present, struct Serial *serial, struct Perryfi *perryfi) {
    memset(c, 0, sizeof(*c));
    c->present = present;
    c->serial  = serial;
    c->perryfi = perryfi;
    cps_reset(c);
}

void cps_reset(Cps *c) {
    dart_reset(&c->dartA);
    dart_reset(&c->dartB);
    c->baud_tx   = 0;
    c->baud_rx   = 0;
    c->baud_mode = 0;
    c->baud_buf  = 0;
    c->parallel_data   = 0;
    c->parallel_strobe = false;
}

void cps_set_present(Cps *c, bool present) {
    c->present = present;
}

u8 cps_read(Cps *c, u8 lo) {
    if (!c->present) return 0xFF;
    switch (lo & 0x0F) {
        case 0x00: return dartA_in_data(c);
        case 0x01: return dartA_in_ctrl(c);
        case 0x02: return dartB_in_data(c);
        case 0x03: return dartB_in_ctrl(c);
        case 0x08: return c->parallel_data;   /* read-back */
        default:   return 0xFF;
    }
}

void cps_write(Cps *c, u8 lo, u8 val) {
    if (!c->present) return;
    switch (lo & 0x0F) {
        case 0x00: dartA_out_data(c, val); break;
        case 0x01: dartA_out_ctrl(c, val); break;
        case 0x02: dartB_out_data(c, val); break;
        case 0x03: dartB_out_ctrl(c, val); break;
        case 0x04: case 0x05: pit_write_counter(c, val); break;
        case 0x07: pit_write_ctrl(c, val); break;
        case 0x08: c->parallel_data = val; break;
        default:   break;
    }
}
