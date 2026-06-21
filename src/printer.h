#pragma once
#include "types.h"

/*
 * PCW dot-matrix printer (model 8256/8512/9512 ship with a 9-pin
 * head; the 9512+ ships a daisy wheel instead). Ports 0xFC and
 * 0xFD carry control and data.
 *
 * Today this is a no-op: writes are discarded, status reads always
 * report "ready". Just enough to keep CP/M+ printer probing from
 * hanging.
 */

typedef struct {
    bool connected;
    bool bail_in;
    bool paper_present;
    bool feeder_present;
    bool head_at_left;
    bool busy;
    int  busy_ticks;
    u8   cmd[2];
    int  cmd_pos;
} Printer;

void printer_init(Printer *p);
u8   printer_read (Printer *p, u8 port);
void printer_write(Printer *p, u8 port, u8 val);
