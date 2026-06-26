#pragma once
#include <stdbool.h>
#include <limits.h>
#include "pcw.h"
#include "input_types.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Monochrome phosphor tint applied to the resolved framebuffer.
 * Matches the four-mode selector 1984 uses, defaulting to GREEN here
 * because the PCW shipped with a green-phosphor monitor. */
typedef enum {
    MONO_OFF = 0,
    MONO_GREEN,
    MONO_AMBER,
    MONO_WHITE,
} MonoMode;

/* Host-side reinterpretation of the 1bpp roller-RAM, ported from
 * ZesarUX's pcw_video_mode. Real PCWs only had VIDEO_PCW; the others
 * regroup the same bytes into wider pixels indexed through a palette.
 * No guest software can drive these — purely a decorative toggle. */
typedef enum {
    VIDEO_PCW = 0,   /* 1 bpp, 720×256, 2 colours (uses MonoMode tint) */
    VIDEO_CGA1,      /* 2 bpp, CGA palette 0 hi: black/green/red/brown  */
    VIDEO_CGA2,      /* 2 bpp, CGA palette 1 hi: black/cyan/magenta/white */
    VIDEO_EGA,       /* 4 bpp, 180×256 quadrupled, 16-colour palette  */
} VideoMode;

/* Real PCWs shipped as separate regional models. UK / EU 8256/8512/9512
 * are PAL — 50 Hz frame, full 256 visible lines. US PcW10 / PcW256-NA /
 * PCW9512N are NTSC — 60 Hz frame, top 200 lines visible, F8 read bit 4
 * set to 1 (per Seasip §4.1). The 300 Hz interrupt timer is independent
 * of frame rate on real hardware; only frame cadence and the visible
 * window differ. */
typedef enum {
    REGION_PAL = 0,
    REGION_NTSC,
} Region;

typedef struct {
    /* [machine] */
    PcwModel model;          /* default PCW_MODEL_8256 */
    int      memory_kb;      /* 256 / 512 / 2048; default 256 */
    Region   region;         /* default REGION_PAL */

    /* [storage] */
    char     drive_a[PATH_MAX];
    char     drive_b[PATH_MAX];
    /* Last-used directories for the F9 overlay file pickers. Seeded from
     * the dirname of drive_a/drive_b at config_load when empty, then
     * updated after each successful disk/snapshot/blank dialog so the
     * next picker opens where you were last working. */
    char     last_disk_dir[PATH_MAX];
    char     last_snap_dir[PATH_MAX];

    /* [display] */
    int      scale;          /* 1..4 */
    bool     fullscreen;
    bool     fullscreen_smoothing;
    MonoMode  monochrome;    /* default MONO_GREEN */
    bool      tint_glow;     /* CRT-style: dim → near-black so the tint
                              * really pops, like a turned-up brightness
                              * knob. Applies to all phosphor tints. */
    VideoMode video_mode;    /* default VIDEO_PCW */

    /* [extensions] — model-specific add-ons. */
    bool     ext_second_drive;          /* PCW 8256 only: bolt-on drive B */
    bool     ext_sanpollo_backplane;    /* 50-pin edge-connector hub at the
                                         * back of the monitor. The hub
                                         * itself is inert — it only
                                         * enables other expansions to be
                                         * plugged in below. */
    bool     ext_serial;                /* 8251 USART. 9512 had it built in;
                                         * 8256/8512 need the SanPollo
                                         * backplane to expose it. */
    char     ext_serial_backend[8];     /* "pty" or "tcp" */
    int      ext_serial_tcp_port;       /* default 4002 */
    char     ext_serial_pty_link[PATH_MAX]; /* host-side alias symlinked to
                                             * the PTY slave; default
                                             * /tmp/1985-serial. Empty
                                             * string in config means
                                             * "use the default". */
    bool     ext_perryfi;               /* SanPollo PerryFi: AT-modem to
                                         * the host network. Plugs onto
                                         * the serial port, so it's only
                                         * available when ext_serial is
                                         * enabled. */
    bool     ext_dktronics;             /* DK'tronics PCW Sound + Joystick:
                                         * AY-3-8912 + DB9 at 0xA9-0xAB.
                                         * Needs the PCW Backplane. */
    bool     ext_multilink;             /* Multilink ring-network probe-stub
                                         * at ports 0xA6/0xA7 (Seasip §14.1).
                                         * Backplane-only accessory. Returns
                                         * the canonical "ring broken" reply
                                         * so probing software completes its
                                         * startup; not a real ring impl. */
    bool      ext_pdf_printer;          /* Host-side PDF sink for built-in
                                         * printer/Centronics output. */
    char      ext_pdf_printer_dir[PATH_MAX];
    PrintSink ext_print_sink;           /* PDF or REAL (#60). */
    bool      printer_centronics_9512;  /* 9512 only: route LST: through the
                                         * built-in Centronics (external printer)
                                         * instead of the stock daisywheel.
                                         * Ignored on 8256/8512. (#70) */

    /* [advanced] */
    bool     tinker;
    char     boot_rom_dir[PATH_MAX];    /* user-picked dir checked first
                                         * for pcw_boot.rom (overrides the
                                         * default XDG / cwd search chain).
                                         * Empty = no override. */
    bool     debug;
    bool     debug_traces;   /* master gate for all stderr noise -- default OFF */
    bool     trace_io;
    bool     trace_fdc;
    bool     trace_input;
    InputDevice input_device;
    MouseType mouse_type;
    JoystickType joystick_type;

    /* Path actually used at load/save time. */
    char     path[PATH_MAX];
} Config;

void config_defaults(Config *c);

/* If path is NULL, ~/.config/1985/1985.conf is used. */
void config_load(Config *c, const char *path);
int  config_save(const Config *c);
