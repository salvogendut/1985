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
