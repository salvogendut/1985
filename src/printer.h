#pragma once
#include "types.h"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

/*
 * PCW dot-matrix printer (model 8256/8512/9512 ship with a 9-pin
 * head; the 9512+ ships a daisy wheel instead). Ports 0xFC and
 * 0xFD carry control and data.
 *
 * The startup code only really needs the controller to look sane:
 * report "printer present", stay ready, and remember whether the
 * head has been reset to the left margin.
 *
 * The optional PDF backend is host-side only: disabling it must not
 * make the emulated printer disappear from the guest.
 */

typedef enum {
    PRINTER_MODE_NORMAL = 0,
    PRINTER_MODE_LINEFEED,
    PRINTER_MODE_PRINTING,
} PrinterMode;

typedef struct Printer {
    bool connected;
    bool bail_in;
    bool paper_present;
    bool feeder_present;
    bool head_at_left;
    u8   cmd[2];
    int  cmd_pos;

    PrinterMode mode;
    float x;
    float y;
    float xstep;
    float xdir;

    bool pdf_enabled;
    char pdf_output_dir[PATH_MAX];
    char pdf_path[PATH_MAX];
    cairo_surface_t *pdf_surface;
    cairo_t *pdf_cr;
    bool pdf_page_open;

    float text_x;
    float text_y;
    int   text_esc_skip;
} Printer;

void printer_init(Printer *p);
void printer_shutdown(Printer *p);
void printer_set_pdf_output_dir(Printer *p, const char *dir);
void printer_set_pdf_enabled(Printer *p, bool enabled);
u8   printer_read (Printer *p, u8 port);
void printer_write(Printer *p, u8 port, u8 val);
void printer_write_centronics(Printer *p, u8 val);
