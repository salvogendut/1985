#include "printer.h"
#include <string.h>

void printer_init(Printer *p) {
    memset(p, 0, sizeof(*p));
    p->connected = true;
    p->bail_in = true;
    p->paper_present = true;
    p->feeder_present = true;
    p->head_at_left = true;
}

u8 printer_read(Printer *p, u8 port) {
    if (!p->connected) {
        if (port == 0xFC) return 0x01;
        return 0x21;    /* bail in + no printer */
    }

    if (port == 0xFC) return 0xF8;     /* no controller error */

    /* Return just 0x40 (READY) like ZEsarUX. The richer Joyce-style
     * 0xCC (READY|FEEDER|PAPER|BAIL) causes 1-01 to hang in our
     * emulator -- something about how BDOS interprets the bail/paper
     * bits triggers a path we don't fully model. Until we model the
     * printer MCU more accurately, the minimal 0x40 unblocks the
     * disks that work in ZEsarUX. */
    return 0x40;
    (void)p;
}

void printer_write(Printer *p, u8 port, u8 val) {
    (void)port;

    if (!p->connected) return;

    p->cmd[p->cmd_pos++] = val;
    if (p->cmd_pos < 2) return;

    u8 cmd0 = p->cmd[0];
    u8 cmd1 = p->cmd[1];
    p->cmd_pos = 0;

    switch (cmd0) {
        case 0x00:
            /* Init sequence during boot. */
            break;
        case 0xA4:
            /* Line feed. */
            break;
        case 0xA8: case 0xA9: case 0xAA: case 0xAB:
            /* Print head motion and/or print line. */
            p->head_at_left = false;
            break;
        case 0xAC:
            /* Line feed. */
            break;
        case 0xB8:
            if (cmd1 == 0)
                p->head_at_left = true;
            break;
        case 0xC0:
            /* End of command sequence. */
            break;
        default:
            /* Unknown commands complete quickly. */
            break;
    }
}
