#include "printer.h"
#include <string.h>

void printer_init(Printer *p) {
    memset(p, 0, sizeof(*p));
}

u8 printer_read(Printer *p, u8 port) {
    (void)p;
    /* Canned MCU responses, matching joyce's combinational stub
     * (JoyceMatrix.cxx:107-131). The boot firmware polls these to
     * decide whether the printer is alive; 0xFF (our previous stub)
     * is invalid and wedges the printer-check loop.
     *
     *   0xFC error type  — 0xF8 = "no errors"
     *   0xFD status      — 0xCC = BAIL_IN | READY | FEEDER | PAPER
     *                      (8256 at power-on, bail bar in, head parked)
     */
    if (port == 0xFC) return 0xF8;
    if (port == 0xFD) return 0xCC;
    return 0xFF;
}

void printer_write(Printer *p, u8 port, u8 val) {
    (void)p; (void)port; (void)val;
}
