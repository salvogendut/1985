#pragma once
#include <stdbool.h>
#include "types.h"
#include "input_types.h"

/*
 * Host-mouse state exposed through one selected PCW mouse protocol:
 *
 * AMX, ports A0-A3:
 *   A0 vertical movement: low nibble up, high nibble down
 *   A1 horizontal movement: low nibble right, high nibble left
 *   A2 buttons in bits 0..2, active-low: left, middle, right
 *   A2 write FF then 00 resets the movement counters
 *   A3 8255 mode control
 *
 * Kempston, ports D0-D4:
 *   D0/D2 X counter, D1/D3 Y counter, D4 active-low L/R buttons
 *
 * Keymouse: keyboard-matrix overlay at 0x3FFB-0x3FFE (no I/O ports).
 *   3FFB  b7=middle button, b6-b0=signed 7-bit X position
 *   3FFC  b7-b6=Y[6:5], b5-b0=preserved keyboard bits
 *   3FFD  b4-b0=Y[4:0],  b7-b5=preserved keyboard bits
 *   3FFE  b7=left button, b6=right button, b5-b0=preserved keyboard bits
 *   Position is cumulative (wraps mod 128) with a /8 sensitivity divider
 *   matching PCW MiSTer's rtl/key_joystick.sv:105-106.
 */
typedef struct PcwMouse {
    bool      present;
    MouseType type;

    float amx_x;
    float amx_y;

    float kempston_frac_x;
    float kempston_frac_y;
    u8    kempston_x;
    u8    kempston_y;

    float keymouse_frac_x;
    float keymouse_frac_y;
    u8    keymouse_x;        /* 7-bit signed, mask 0x7F on use */
    u8    keymouse_y;

    u8   buttons;       /* active-high host state: L/M/R in bits 0..2 */
    u8   control;
    bool reset_armed;
} PcwMouse;

void pcwmouse_init       (PcwMouse *m, bool present, MouseType type);
void pcwmouse_reset      (PcwMouse *m);
void pcwmouse_configure  (PcwMouse *m, bool present, MouseType type);
void pcwmouse_add_motion (PcwMouse *m, float dx, float dy);
void pcwmouse_set_button (PcwMouse *m, int button, bool down);
void pcwmouse_clear_input(PcwMouse *m);

bool pcwmouse_handles_port(const PcwMouse *m, u8 lo);
u8   pcwmouse_read       (PcwMouse *m, u8 lo);
void pcwmouse_write      (PcwMouse *m, u8 lo, u8 val);

/* Keymouse: overlay the X/Y/button bytes onto the 16-byte keyboard
 * scan window (kbd_window[0..15] mirrors RAM 0x3FF0..0x3FFF). No-op
 * unless type == MOUSE_TYPE_KEYMOUSE. Call after kbd_scan_into_ram. */
void pcwmouse_overlay_kbd(PcwMouse *m, u8 *kbd_window);
