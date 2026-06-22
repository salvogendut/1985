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
    a->interrupt_counter = 0;
    a->fdc_irq_mode    = 0;   /* power-on default: FDC IRQ ignored; boot code enables it */
}

void asic_frame(Asic *a) {
    /* Frame boundary: nothing to do here now that flyback is driven
     * per timer tick. Kept for callers that still expect to be called
     * once per frame. */
    (void)a;
}

bool asic_timer_tick(Asic *a) {
    /* MAME pcw.cpp:141-149 + :184-191 — 300 Hz periodic tick increments
     * the interrupt counter, saturating at 0x0F, and asserts /INT.
     *
     * Use the tick to drive a coarse flyback signal too: 6 ticks per
     * 50 Hz frame, flyback HIGH only on the LAST tick (≈17 % duty —
     * still too long for a real CRT retrace but much closer to the
     * "briefly high during vertical retrace" model than the previous
     * 50 % toggle, and enough for firmware timing loops that watch
     * for the rising edge of F8 bit 6). */
    if (a->interrupt_counter < 0x0F) a->interrupt_counter++;
    a->flyback_tick = (a->flyback_tick + 1) % 6;
    a->flyback = (a->flyback_tick == 5);
    return true;
}

int asic_poll_fdc_irq(Asic *a) {
    /* Two delivery modes:
     * - IRQ mode (fdc_irq_mode == 2): LEVEL-triggered. Re-assert
     *   pending_irq every step while fdc->irq is high. Z80 accepts
     *   only when iff1 == 1. Matches MAME pcw.cpp:153-176 and the
     *   FD84/FDA8 coroutine yield pattern.
     * - NMI mode (fdc_irq_mode == 1): EDGE-triggered. NMI accept is
     *   unconditional (not gated on iff1), so a level-triggered NMI
     *   would re-fire on every instruction inside the NMI handler,
     *   producing infinite stack pushes. Only fire on the rising
     *   edge of fdc->irq. */
    bool now = a->fdc && a->fdc->irq;
    int  req = 0;
    if (a->fdc_irq_mode == 2 && now) {
        req = 2;                               /* IRQ: level */
    } else if (a->fdc_irq_mode == 1 && now && !a->prev_fdc_irq) {
        req = 1;                               /* NMI: edge */
    }
    a->prev_fdc_irq = now;
    return req;
}

static u8 sys_status(const Asic *a) {
    /* MAME pcw.cpp:196-206 + Joyce JoyceAsic.cxx:89-93: bit 5 is a LIVE
     * mirror of the FDC INTRQ line, NOT a sticky latch. The host clears
     * it implicitly by draining the FDC result phase (which causes the
     * FDC to drop its IRQ line). */
    u8 v = (a->interrupt_counter & 0x0F);
    if (a->flyback)                       v |= 0x40;
    if (a->fdc && a->fdc->irq)            v |= 0x20;
    return v;
}

u8 asic_read(Asic *a, u8 port) {
    switch (port) {
        case 0xF4: {
            /* MAME pcw.cpp:358-373 — reads the same status byte as F8
             * but additionally clears the interrupt counter. The BIOS
             * uses this to ack pending timer interrupts. */
            u8 v = sys_status(a);
            a->interrupt_counter = 0;
            return v;
        }
        case 0xF8:
            return sys_status(a);
        default:
            return 0xFF;
    }
}

void asic_write(Asic *a, u8 port, u8 val) {
    switch (port) {
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
                case 0x02:
                    /* Route FDC INTRQ to /NMI. On real hardware this
                     * is a level-triggered routing -- if INTRQ is
                     * currently high, /NMI fires immediately. Force
                     * prev_fdc_irq=false so the next asic_poll sees
                     * a rising edge if irq is high. */
                    a->fdc_irq_mode = 1;
                    a->prev_fdc_irq = false;
                    break;
                case 0x03:
                    /* Route FDC INTRQ to /INT (maskable). Level. */
                    a->fdc_irq_mode = 2;
                    a->prev_fdc_irq = false;
                    break;
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
