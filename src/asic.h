#pragma once
#include "types.h"

/*
 * PCW ASIC ("Anne" — the system controller).
 *
 * The ASIC is a glue chip that owns:
 *   - port 0xF4 read/write   timer/counter register
 *   - port 0xF5 write        roller-RAM base (block<<5 | offset>>9)
 *   - port 0xF6 write        Y scroll
 *   - port 0xF7 write        bit7 = inverse video, bit6 = enable display
 *   - port 0xF8 read/write   system control:
 *       read  bit 6 = frame flyback (1 during VSYNC)
 *             bit 4 = 60Hz mode link
 *       write 0x00 = end bootstrap (RAM takes over)
 *             0x02 = FDC interrupt routed to NMI
 *             0x03 = FDC interrupt routed to IRQ (maskable)
 *             0x04 = FDC interrupts ignored
 *             0x05 = assert FDC terminal count
 *             0x06 = clear FDC terminal count
 *             0x07 = enable display
 *             0x08 = disable display
 *             others = beeper toggle / unused bits
 *
 * For the scaffold pass we accept all writes and update internal
 * latches; reads return only the flyback bit, advanced once per
 * frame by asic_frame().
 */

struct Mem;
struct Bootstrap;
struct Fdc;

typedef struct Asic {
    u8  roller_base;        /* port 0xF5 */
    u8  scroll_y;           /* port 0xF6 */
    u8  display_ctrl;       /* port 0xF7 */
    bool display_enabled;
    bool inverse_video;
    bool flyback;           /* port 0xF8 read bit 6 */
    u8  timer;              /* port 0xF4 */
    int fdc_irq_mode;       /* 0=ignore, 1=NMI, 2=IRQ */

    struct Bootstrap *bootstrap;
    struct Fdc       *fdc;
} Asic;

void asic_init(Asic *a, struct Bootstrap *boot, struct Fdc *fdc);
void asic_reset(Asic *a);

/* Called once per emulated frame (50 Hz) — toggles flyback bit. */
void asic_frame(Asic *a);

u8   asic_read (Asic *a, u8 port);
void asic_write(Asic *a, u8 port, u8 val);
