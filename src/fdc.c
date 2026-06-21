#include "fdc.h"
#include <string.h>

#define MSR_RQM   0x80   /* request for master — host can transfer */
#define MSR_DIO   0x40   /* 1 = FDC → CPU */
#define MSR_NDM   0x20   /* non-DMA mode */
#define MSR_BUSY  0x10   /* command in progress */

void fdc_init(Fdc *f) {
    memset(f, 0, sizeof(*f));
    disk_init(&f->drive[0]);
    disk_init(&f->drive[1]);
}

void fdc_reset(Fdc *f) {
    f->tc = false;
}

u8 fdc_read(Fdc *f, u8 port) {
    (void)f;
    switch (port) {
        case 0x00:
            /* Main status register: report "idle, ready to accept a
             * command". When real command dispatch lands this swings
             * with BUSY/NDM/DIO. */
            return MSR_RQM;
        case 0x01:
            /* Data register read with no active command — undefined
             * on real silicon; return 0xFF (idle bus). */
            return 0xFF;
        default:
            return 0xFF;
    }
}

void fdc_write(Fdc *f, u8 port, u8 val) {
    (void)f; (void)port; (void)val;
    /* Command byte stream is silently discarded for the scaffold. */
}

void fdc_set_terminal_count(Fdc *f, bool on) {
    f->tc = on;
}
