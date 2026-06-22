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
