#include "pcwmouse.h"
#include <string.h>

#define AMX_MAX_PENDING 32767.0f

/* Host pixel deltas → AMX encoder pulses. The hardware encoder is a
 * 4-bit-per-axis delta nibble; the driver polls A0/A1 on every PCW
 * timer tick (~300 Hz) and rolls the count into the GEM cursor. Two
 * knobs:
 *
 *   AMX_SENSITIVITY  scales raw SDL pixel deltas before they hit the
 *                    accumulator. Too low and slow movement rounds
 *                    to zero between polls; too high and the cursor
 *                    flies. 0.5 keeps fine motion alive.
 *   AMX_MAX_PER_POLL caps the count drained on one poll. The hardware
 *                    nibble allows 15, but emitting 15 in one shot
 *                    makes the driver lurch then sit still ("stutter")
 *                    when the host moves continuously. A small cap
 *                    spreads the drain over many polls, which the
 *                    driver integrates into smooth motion. */
#define AMX_SENSITIVITY  0.50f
#define AMX_MAX_PER_POLL 3

/* Keymouse counters are 7-bit signed cumulative position. PCW MiSTer
 * scales raw host deltas by 1/8 before adding them (see
 * rtl/key_joystick.sv:105) — too coarse and the cursor flies, too
 * fine and slow motion drops to zero between MicroDesign polls. */
#define KEYMOUSE_SENSITIVITY 0.125f

static float clamp_pending(float v) {
    if (v >  AMX_MAX_PENDING) return  AMX_MAX_PENDING;
    if (v < -AMX_MAX_PENDING) return -AMX_MAX_PENDING;
    return v;
}

void pcwmouse_init(PcwMouse *m, bool present, MouseType type) {
    memset(m, 0, sizeof(*m));
    m->present = present;
    m->type    = type;
}

void pcwmouse_reset(PcwMouse *m) {
    bool present = m->present;
    MouseType type = m->type;
    pcwmouse_init(m, present, type);
}

void pcwmouse_configure(PcwMouse *m, bool present, MouseType type) {
    if (m->present == present && m->type == type) return;
    pcwmouse_init(m, present, type);
}

void pcwmouse_add_motion(PcwMouse *m, float dx, float dy) {
    if (!m->present) return;

    m->amx_x = clamp_pending(m->amx_x + dx * AMX_SENSITIVITY);
    m->amx_y = clamp_pending(m->amx_y + dy * AMX_SENSITIVITY);

    m->kempston_frac_x += dx;
    m->kempston_frac_y -= dy;
    int whole_x = (int)m->kempston_frac_x;
    int whole_y = (int)m->kempston_frac_y;
    if (whole_x) {
        m->kempston_x = (u8)(m->kempston_x + whole_x);
        m->kempston_frac_x -= (float)whole_x;
    }
    if (whole_y) {
        m->kempston_y = (u8)(m->kempston_y + whole_y);
        m->kempston_frac_y -= (float)whole_y;
    }

    /* Keymouse: cumulative 7-bit signed counters, wrap mod 128. SDL Y
     * goes down with positive dy; MicroDesign expects PC-style "up
     * = lower count", so we add dy as-is (positive dy = increases Y
     * counter = cursor moves down on PCW screen). */
    m->keymouse_frac_x += dx * KEYMOUSE_SENSITIVITY;
    m->keymouse_frac_y += dy * KEYMOUSE_SENSITIVITY;
    int km_dx = (int)m->keymouse_frac_x;
    int km_dy = (int)m->keymouse_frac_y;
    if (km_dx) {
        m->keymouse_x = (u8)((m->keymouse_x + km_dx) & 0x7F);
        m->keymouse_frac_x -= (float)km_dx;
    }
    if (km_dy) {
        m->keymouse_y = (u8)((m->keymouse_y + km_dy) & 0x7F);
        m->keymouse_frac_y -= (float)km_dy;
    }
}

void pcwmouse_set_button(PcwMouse *m, int button, bool down) {
    if (!m->present || button < 0 || button > 2) return;
    u8 mask = (u8)(1u << button);
    if (down) m->buttons |= mask;
    else      m->buttons &= (u8)~mask;
}

void pcwmouse_clear_input(PcwMouse *m) {
    m->amx_x = 0.0f;
    m->amx_y = 0.0f;
    m->kempston_frac_x = 0.0f;
    m->kempston_frac_y = 0.0f;
    m->keymouse_frac_x = 0.0f;
    m->keymouse_frac_y = 0.0f;
    m->buttons = 0;
}

static int consume_positive(float *pending) {
    int n = (int)*pending;
    if (n > AMX_MAX_PER_POLL) n = AMX_MAX_PER_POLL;
    *pending -= (float)n;
    return n;
}

static int consume_negative(float *pending) {
    int n = (int)-*pending;
    if (n > AMX_MAX_PER_POLL) n = AMX_MAX_PER_POLL;
    *pending += (float)n;
    return n;
}

bool pcwmouse_handles_port(const PcwMouse *m, u8 lo) {
    if (!m->present) return false;
    if (m->type == MOUSE_TYPE_AMX)
        return lo >= 0xA0 && lo <= 0xA3;
    return lo >= 0xD0 && lo <= 0xD4;
}

static u8 read_amx(PcwMouse *m, u8 lo) {
    switch (lo) {
        case 0xA0:
            if (m->amx_y <= -1.0f)
                return (u8)consume_negative(&m->amx_y);
            if (m->amx_y >= 1.0f)
                return (u8)(consume_positive(&m->amx_y) << 4);
            return 0;

        case 0xA1:
            if (m->amx_x >= 1.0f)
                return (u8)consume_positive(&m->amx_x);
            if (m->amx_x <= -1.0f)
                return (u8)(consume_negative(&m->amx_x) << 4);
            return 0;

        case 0xA2:
            /* Only the low three 8255 Port-C lines are button inputs.
             * Keep the unused upper lines low; PCW software uses this
             * non-floating value to distinguish an AMX interface from
             * an absent device returning FF. */
            return (u8)(0x07u & (u8)~m->buttons);

        case 0xA3:
            return 0;

        default:
            return 0xFF;
    }
}

static u8 read_kempston(PcwMouse *m, u8 lo) {
    switch (lo) {
        case 0xD0:
        case 0xD2:
            return m->kempston_x;
        case 0xD1:
        case 0xD3:
            return m->kempston_y;
        case 0xD4: {
            /* Spectrum-Kempston convention: bit 0 = right, bit 1 = left,
             * active-low. AMX-Art and Mini Office's PCW Kempston drivers
             * mirror this. (ZEsarUX operaciones.c:7161.) */
            u8 value = 0xFF;
            if (m->buttons & (1u << 2)) value &= (u8)~(1u << 0);
            if (m->buttons & (1u << 0)) value &= (u8)~(1u << 1);
            return value;
        }
        default:
            return 0xFF;
    }
}

u8 pcwmouse_read(PcwMouse *m, u8 lo) {
    if (!pcwmouse_handles_port(m, lo)) return 0xFF;
    return m->type == MOUSE_TYPE_AMX ? read_amx(m, lo)
                                     : read_kempston(m, lo);
}

void pcwmouse_write(PcwMouse *m, u8 lo, u8 val) {
    if (!m->present || m->type != MOUSE_TYPE_AMX) return;

    if (lo == 0xA2) {
        if (val == 0xFF) {
            m->reset_armed = true;
        } else {
            if (val == 0x00 && m->reset_armed) {
                m->amx_x = 0.0f;
                m->amx_y = 0.0f;
            }
            m->reset_armed = false;
        }
    } else if (lo == 0xA3) {
        m->control = val;
    }
}

void pcwmouse_overlay_kbd(PcwMouse *m, u8 *kbd_window) {
    if (!m->present || m->type != MOUSE_TYPE_KEYMOUSE) return;

    u8 x  = m->keymouse_x & 0x7F;
    u8 y  = m->keymouse_y & 0x7F;
    u8 mb = (m->buttons >> 1) & 1;     /* middle */
    u8 lb =  m->buttons       & 1;     /* left   */
    u8 rb = (m->buttons >> 2) & 1;     /* right  */

    /* 0x3FFB = window[0xB]: b7 middle, b6-b0 X. Replaces row 11
     * entirely (joystick-2 row is unreachable while Keymouse is on
     * — same trade-off as the real hardware). */
    kbd_window[0xB] = (u8)((mb << 7) | x);

    /* 0x3FFC = window[0xC]: keep b5-b0 (keyboard), set b7-b6 to Y[6:5]. */
    kbd_window[0xC] = (u8)((kbd_window[0xC] & 0x3F) | ((y & 0x60) << 1));

    /* 0x3FFD = window[0xD]: keep b7-b5, set b4-b0 to Y[4:0]. */
    kbd_window[0xD] = (u8)((kbd_window[0xD] & 0xE0) | (y & 0x1F));

    /* 0x3FFE = window[0xE]: keep b5-b0, set b7 left, b6 right. */
    kbd_window[0xE] = (u8)((kbd_window[0xE] & 0x3F) | (lb << 7) | (rb << 6));
}
