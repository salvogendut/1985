#pragma once
#include "types.h"

/*
 * PCW dot-matrix printer (model 8256/8512/9512 ship with a 9-pin
 * head; the 9512+ ships a daisy wheel instead). Ports 0xFC and
 * 0xFD carry control and data.
 *
 * The startup code only really needs the controller to look sane:
 * report "printer present", stay ready, and remember whether the
 * head has been reset to the left margin.
 */

typedef struct {
    bool connected;
    bool bail_in;
    bool paper_present;
    bool feeder_present;
    bool head_at_left;
    u8   cmd[2];
    int  cmd_pos;
} Printer;

void printer_init(Printer *p);
u8   printer_read (Printer *p, u8 port);
void printer_write(Printer *p, u8 port, u8 val);
