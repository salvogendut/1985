#include "asic.h"
#include "fdc.h"
#include "bootstrap.h"
#include "fdc.h"
#include <string.h>

void asic_init(Asic *a, struct Bootstrap *boot, struct Fdc *fdc) {
    memset(a, 0, sizeof(*a));
    a->bootstrap = boot;
    a->fdc       = fdc;
    asic_reset(a);
}

void asic_reset(Asic *a) {
    a->roller_base     = 0;
    a->scroll_y        = 0;
    a->display_ctrl    = 0;
    a->display_enabled = true;   /* both gates default to enabled — the */
    a->screen_enabled  = true;   /* firmware blanks/unblanks explicitly */
    a->inverse_video   = false;
    a->flyback         = false;
    a->timer           = 0;
    a->fdc_irq_mode    = 0;   /* power-on default: FDC IRQ ignored; boot code enables it */
}

void asic_frame(Asic *a) {
    /* Simplistic: pulse flyback high for one frame in three so the
     * firmware's busy-wait loops break out. Real cadence is set by
     * the 6845 VSYNC pulse. */
    a->flyback = !a->flyback;
}

int asic_poll_fdc_irq(Asic *a) {
    bool now = a->fdc && a->fdc->irq;
    int  req = 0;
    if (now && !a->prev_fdc_irq && a->fdc_irq_mode != 0)
        req = a->fdc_irq_mode;          /* 1 = NMI, 2 = IRQ */
    a->prev_fdc_irq = now;
    return req;
}

u8 asic_read(Asic *a, u8 port) {
    switch (port) {
        case 0xF4: return a->timer;
        case 0xF8: {
            /* bit 6 = vblank/flyback, bit 5 = FDC IRQ status.
             * Matches MAME pcw.cpp:pcw_get_sys_status(). */
            u8 v = 0;
            if (a->flyback)               v |= 0x40;
            if (a->fdc && a->fdc->irq)    v |= 0x20;
            return v;
        }
        default:
            return 0xFF;
    }
}

void asic_write(Asic *a, u8 port, u8 val) {
    switch (port) {
        case 0xF4: a->timer = val; break;
        case 0xF5: a->roller_base = val; break;
        case 0xF6: a->scroll_y    = val; break;
        case 0xF7:
            a->display_ctrl   = val;
            a->screen_enabled = (val & 0x40) != 0;
            a->inverse_video  = (val & 0x80) != 0;
            break;
        case 0xF8:
            switch (val) {
                case 0x00:
                    /* End-of-bootstrap signal: subsequent fetches
                     * read from RAM. */
                    if (a->bootstrap) bootstrap_finish(a->bootstrap);
                    break;
                case 0x02: a->fdc_irq_mode = 1; break;
                case 0x03: a->fdc_irq_mode = 2; break;
                case 0x04: a->fdc_irq_mode = 0; break;
                case 0x05: fdc_set_terminal_count(a->fdc, true);  break;
                case 0x06: fdc_set_terminal_count(a->fdc, false); break;
                case 0x07: a->display_enabled = true;  break;
                case 0x08: a->display_enabled = false; break;
                case 0x09: fdc_set_motor(a->fdc, true);  break;
                case 0x0A: fdc_set_motor(a->fdc, false); break;
                default:   break;  /* beeper / unused */
            }
            break;
        default:
            break;
    }
}
