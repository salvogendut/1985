#pragma once
#include <stdbool.h>
#include "pcw.h"
#include "display.h"
#include "paste.h"

/*
 * pilot — host-PTY auto-pilot input for 1985.
 *
 * This is the PCW sibling of 1984's --pilot interface. It opens a PTY and
 * accepts newline-delimited commands for live UI automation:
 *
 *   move R THETA / R THETA / hold F R THETA / stop
 *   press N / release N / click N / hold-click F N
 *   target mouse|joy
 *   key-down NAME / key-up NAME / key-tap NAME F
 *   paste TEXT
 *   state / frame / hash / changed / wait ... / crop PATH X Y W H [SCALE]
 *   snapshot-save PATH / snapshot-load PATH
 *
 * PCW mapping:
 *   target mouse  -> 1985's emulated PCW mouse device
 *   target joy    -> PCW cursor-key matrix + Space as fire/click
 */

typedef enum { PILOT_MOUSE = 0, PILOT_JOY = 1 } PilotTarget;
typedef enum {
    PILOT_WAIT_NONE = 0,
    PILOT_WAIT_FRAMES,
    PILOT_WAIT_HASH_EQ,
    PILOT_WAIT_HASH_NE,
    PILOT_WAIT_CHANGE,
    PILOT_WAIT_QUIET,
} PilotWaitMode;

typedef struct {
    int          fd;
    char         slave[256];
    char         link[256];
    bool         reply_stderr;
    PilotTarget  target;

    double       mag, ang;
    double       vx, vy;
    double       acc_x, acc_y;

    bool         btn[3];
    int          click_left[3];
    int          move_left;
    int          keytap_left;
    int          keytap_scancode;
    int          scroll_pending;
    bool         joy_held;

    int          frame_count;
    u32          fb_hash;
    u32         *last_pixels;
    bool         have_last_pixels;
    int          changed_x, changed_y, changed_w, changed_h;
    int          quiet_frames;

    PilotWaitMode wait_mode;
    int           wait_target_frames;
    int           wait_start_frame;
    int           wait_timeout_frame;
    u32           wait_hash;
    int           wait_quiet_need;

    char         line[256];
    int          line_len;
} Pilot;

bool pilot_open(Pilot *p, const char *link, PilotTarget initial,
                bool reply_stderr);
bool pilot_is_open(const Pilot *p);

void pilot_tick(Pilot *p, PCW *pcw, Display *d, Paste *paste);
void pilot_post_frame(Pilot *p, PCW *pcw, Display *d, int frame_count);
