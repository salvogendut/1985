#pragma once
#include <stdbool.h>
#include <limits.h>
#include "pcw.h"

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

typedef struct {
    /* [machine] */
    PcwModel model;          /* default PCW_MODEL_8256 */
    int      memory_kb;      /* 256 / 512 / 2048; default 256 */

    /* [storage] */
    char     drive_a[PATH_MAX];
    char     drive_b[PATH_MAX];

    /* [display] */
    int      scale;          /* 1..4 */
    bool     fullscreen;
    bool     fullscreen_smoothing;
    MonoMode monochrome;     /* default MONO_GREEN */

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
    bool     ext_perryfi;               /* SanPollo PerryFi: AT-modem to
                                         * the host network. Plugs onto
                                         * the serial port, so it's only
                                         * available when ext_serial is
                                         * enabled. */
    bool     ext_dktronics;             /* DK'tronics PCW Sound + Joystick:
                                         * AY-3-8912 + DB9 at 0xA9-0xAB.
                                         * Needs the PCW Backplane. */

    /* [advanced] */
    bool     tinker;
    bool     debug;
    bool     debug_traces;   /* master gate for all stderr noise -- default OFF */
    bool     trace_io;
    bool     trace_fdc;
    bool     trace_input;

    /* Path actually used at load/save time. */
    char     path[PATH_MAX];
} Config;

void config_defaults(Config *c);

/* If path is NULL, ~/.config/1985/1985.conf is used. */
void config_load(Config *c, const char *path);
int  config_save(const Config *c);
