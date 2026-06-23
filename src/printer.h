#pragma once
#include "types.h"
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Opaque-pointer forward declarations so consumers don't need <cairo.h>
 * to include this header. When built without Cairo the fields below
 * just hold NULL. */
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

/* Where the finalised PDF lands when the printer extension is on.
 * PDF leaves it in pdf_output_dir for the user; REAL pipes the
 * file to the host's default printer via `lp` (Linux/macOS).
 * Windows falls back to PDF behaviour for now. */
typedef enum {
    PRINT_SINK_PDF = 0,
    PRINT_SINK_REAL,
} PrintSink;

/* Built-in printer hardware shape. 8256 / 8512 shipped with a 9-pin
 * dot-matrix; 9512 shipped with a daisywheel (chars only, no
 * graphics). Derived from the PCW model at boot — not user-cyclable
 * because real hardware can't be swapped. */
typedef enum {
    PRINTER_KIND_DOT_MATRIX = 0,
    PRINTER_KIND_DAISYWHEEL,
} PrinterKind;

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
    PrintSink sink;
    PrinterKind kind;
    bool pdf_ephemeral;             /* true: file is in /tmp, delete after spooling */
    char pdf_output_dir[PATH_MAX];
    char pdf_path[PATH_MAX];
    cairo_surface_t *pdf_surface;
    cairo_t *pdf_cr;
    bool pdf_page_open;

    float text_x;
    float text_y;
    int   text_esc_skip;

    /* Idle-flush counter (frames). Print activity resets this to a
     * positive value; printer_tick decrements each frame and closes
     * the current PDF when it hits zero — without that, Cairo only
     * writes the trailer on shutdown and viewers see an empty file. */
    int   idle_countdown;
} Printer;

void printer_init(Printer *p);
void printer_shutdown(Printer *p);
void printer_set_pdf_output_dir(Printer *p, const char *dir);
void printer_set_pdf_enabled(Printer *p, bool enabled);
void printer_set_sink(Printer *p, PrintSink sink);
void printer_set_kind(Printer *p, PrinterKind kind);
u8   printer_read (Printer *p, u8 port);
void printer_write(Printer *p, u8 port, u8 val);
void printer_write_centronics(Printer *p, u8 val);
void printer_tick (Printer *p);   /* call once per frame at 50 Hz */
