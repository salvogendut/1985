/* pilot.c — host-PTY "auto-pilot" input device for 1985. See pilot.h. */

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define __BSD_VISIBLE 1
#endif

#include "pilot.h"
#include "kbd.h"
#include "notify.h"
#include "snapshot.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GEOBENCH on PCW reads the keyboard cursor keys and Space as the pointer.
 * target joy drives that path directly, which is also what a user does by hand. */
#define PCW_KEY_RIGHT_ROW 0
#define PCW_KEY_RIGHT_BIT 6
#define PCW_KEY_UP_ROW    1
#define PCW_KEY_UP_BIT    6
#define PCW_KEY_LEFT_ROW  1
#define PCW_KEY_LEFT_BIT  7
#define PCW_KEY_SPACE_ROW 5
#define PCW_KEY_SPACE_BIT 7
#define PCW_KEY_DOWN_ROW  10
#define PCW_KEY_DOWN_BIT  6
#define PCW_KEY_FIRE2_ROW 10
#define PCW_KEY_FIRE2_BIT 5

#define CLICK_HOLD_FRAMES 4
#define JOY_DEADZONE      0.5

#ifndef _WIN32

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static void set_vector(Pilot *p, double mag, double ang_deg)
{
    double r;
    p->mag = mag;
    p->ang = ang_deg;
    r = ang_deg * (M_PI / 180.0);
    p->vx =  mag * cos(r);
    p->vy = -mag * sin(r);
}

static void release_all_buttons(Pilot *p)
{
    int b;
    for (b = 0; b < 3; b++) {
        p->btn[b] = false;
        p->click_left[b] = 0;
    }
}

static void joy_release(PCW *pcw)
{
    kbd_release(&pcw->kbd, PCW_KEY_UP_ROW,    PCW_KEY_UP_BIT);
    kbd_release(&pcw->kbd, PCW_KEY_DOWN_ROW,  PCW_KEY_DOWN_BIT);
    kbd_release(&pcw->kbd, PCW_KEY_LEFT_ROW,  PCW_KEY_LEFT_BIT);
    kbd_release(&pcw->kbd, PCW_KEY_RIGHT_ROW, PCW_KEY_RIGHT_BIT);
    kbd_release(&pcw->kbd, PCW_KEY_SPACE_ROW, PCW_KEY_SPACE_BIT);
    kbd_release(&pcw->kbd, PCW_KEY_FIRE2_ROW, PCW_KEY_FIRE2_BIT);
}

static void release_keytap(Pilot *p, PCW *pcw)
{
    if (p->keytap_left > 0 && p->keytap_scancode >= 0)
        (void)kbd_sdl_key(&pcw->kbd, (SDL_Scancode)p->keytap_scancode, false);
    p->keytap_left = 0;
    p->keytap_scancode = -1;
}

static int parse_timeout(char *const *tok, int n, int idx, int frame_now)
{
    if (idx < n) {
        int frames = atoi(tok[idx]);
        if (frames >= 0) return frame_now + frames;
    }
    return -1;
}

static void start_wait(Pilot *p, PilotWaitMode mode, int frame_now,
                       int timeout_frame)
{
    p->wait_mode = mode;
    p->wait_start_frame = frame_now;
    p->wait_timeout_frame = timeout_frame;
}

static void pilot_reply(Pilot *p, const char *fmt, ...)
{
    char buf[256], line[256];
    va_list ap;
    int n;

    if (p->fd < 0) return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    snprintf(line, sizeof(line), "%s", buf);
    if (n >= (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 2;
    buf[n++] = '\n';
    (void)!write(p->fd, buf, (size_t)n);
    if (p->reply_stderr) {
        fprintf(stderr, "1985: pilot reply: %s\n", line);
        fflush(stderr);
    }
}

bool pilot_open(Pilot *p, const char *link, PilotTarget initial,
                bool reply_stderr)
{
    int fd, sfd;
    const char *name;

    memset(p, 0, sizeof(*p));
    p->fd = -1;
    p->target = initial;
    p->reply_stderr = reply_stderr;
    p->wait_timeout_frame = -1;
    p->keytap_scancode = -1;
    p->last_pixels = calloc((size_t)DISPLAY_W * DISPLAY_H, sizeof(u32));

    fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("pilot: posix_openpt");
        return false;
    }
    if (grantpt(fd) < 0 || unlockpt(fd) < 0) {
        perror("pilot: grantpt/unlockpt");
        close(fd);
        return false;
    }
    name = ptsname(fd);
    if (!name) {
        close(fd);
        return false;
    }
    snprintf(p->slave, sizeof(p->slave), "%s", name);

    sfd = open(name, O_RDWR | O_NOCTTY);
    if (sfd >= 0) {
        struct termios tio;
        if (tcgetattr(sfd, &tio) == 0) {
            cfmakeraw(&tio);
            tio.c_cflag |= CS8 | CREAD | CLOCAL;
            tcsetattr(sfd, TCSANOW, &tio);
        }
        close(sfd);
    }

    p->fd = fd;
    if (link && link[0]) {
        unlink(link);
        if (symlink(p->slave, link) == 0) {
            snprintf(p->link, sizeof(p->link), "%s", link);
        } else {
            fprintf(stderr, "pilot: symlink(%s -> %s) failed: %s\n",
                    link, p->slave, strerror(errno));
        }
    }

    if (p->link[0])
        notify_post("Pilot: PTY ready at %s (alias %s)", p->slave, p->link);
    else
        notify_post("Pilot: PTY ready at %s", p->slave);
    fprintf(stderr, "1985: pilot PTY: %s (target=%s)\n",
            p->slave, initial == PILOT_JOY ? "joystick" : "mouse");
    return true;
}

bool pilot_is_open(const Pilot *p)
{
    return p->fd >= 0;
}

static SDL_Scancode scancode_from_token(const char *name)
{
    SDL_Scancode sc;
    char buf[64];
    size_t i, j = 0;

    if (!name || !name[0]) return SDL_SCANCODE_UNKNOWN;
    sc = SDL_GetScancodeFromName(name);
    if (sc != SDL_SCANCODE_UNKNOWN) return sc;
    for (i = 0; name[i] && j + 1 < sizeof(buf); i++)
        buf[j++] = (name[i] == '_') ? ' ' : name[i];
    buf[j] = '\0';
    return SDL_GetScancodeFromName(buf);
}

static void reply_state(Pilot *p, PCW *pcw)
{
    pilot_reply(p,
                "state target=%s mag=%.3f ang=%.3f move_left=%d "
                "buttons=%d%d%d click_left=%d,%d,%d joy_held=%d "
                "frame=%d hash=%08X changed=%d,%d,%d,%d quiet=%d "
                "pcw_mouse=%d mouse_type=%d",
                p->target == PILOT_JOY ? "joy" : "mouse",
                p->mag, p->ang, p->move_left,
                p->btn[0] ? 1 : 0, p->btn[1] ? 1 : 0, p->btn[2] ? 1 : 0,
                p->click_left[0], p->click_left[1], p->click_left[2],
                p->joy_held ? 1 : 0, p->frame_count, (unsigned)p->fb_hash,
                p->changed_x, p->changed_y, p->changed_w, p->changed_h,
                p->quiet_frames, pcw->mouse.present ? 1 : 0,
                (int)pcw->mouse.type);
}

static void reply_changed(Pilot *p)
{
    int count = (p->changed_w > 0 && p->changed_h > 0) ? 1 : 0;
    pilot_reply(p, "changed count=%d rect=%d,%d,%d,%d quiet=%d",
                count, p->changed_x, p->changed_y, p->changed_w,
                p->changed_h, p->quiet_frames);
}

static void finish_wait(Pilot *p, const char *reason)
{
    pilot_reply(p,
                "ok wait reason=%s frame=%d hash=%08X changed=%d,%d,%d,%d quiet=%d",
                reason, p->frame_count, (unsigned)p->fb_hash,
                p->changed_x, p->changed_y, p->changed_w, p->changed_h,
                p->quiet_frames);
    p->wait_mode = PILOT_WAIT_NONE;
    p->wait_timeout_frame = -1;
}

static void fail_wait(Pilot *p, const char *reason)
{
    pilot_reply(p, "err wait reason=%s frame=%d hash=%08X",
                reason, p->frame_count, (unsigned)p->fb_hash);
    p->wait_mode = PILOT_WAIT_NONE;
    p->wait_timeout_frame = -1;
}

static void exec_line(Pilot *p, PCW *pcw, Display *d, Paste *paste, char *line)
{
    char *hash = strchr(line, '#');
    char raw[256];
    int frame_now = p->frame_count;
    char *tok[8], *save, *cmd;
    int n = 0;

    if (hash) *hash = '\0';
    snprintf(raw, sizeof(raw), "%s", line);
    for (char *c = line; *c; c++) if (*c == ',') *c = ' ';

    save = NULL;
    for (char *t = strtok_r(line, " \t\r\n", &save);
         t && n < 8; t = strtok_r(NULL, " \t\r\n", &save))
        tok[n++] = t;
    if (!n) return;

    cmd = tok[0];
    for (char *c = cmd; *c; c++) *c = (char)tolower((unsigned char)*c);

    if (isdigit((unsigned char)cmd[0]) ||
        ((cmd[0] == '-' || cmd[0] == '+' || cmd[0] == '.') && cmd[1])) {
        if (n >= 2) {
            set_vector(p, atof(tok[0]), atof(tok[1]));
            p->move_left = 0;
            pilot_reply(p, "ok move mag=%.3f ang=%.3f", p->mag, p->ang);
        } else {
            pilot_reply(p, "err move needs R THETA");
        }
        return;
    }

    if (!strcmp(cmd, "move") || !strcmp(cmd, "m") || !strcmp(cmd, "v")) {
        if (n >= 3) {
            set_vector(p, atof(tok[1]), atof(tok[2]));
            p->move_left = 0;
            pilot_reply(p, "ok move mag=%.3f ang=%.3f", p->mag, p->ang);
        } else pilot_reply(p, "err move needs R THETA");
    } else if (!strcmp(cmd, "hold")) {
        if (n >= 4) {
            p->move_left = atoi(tok[1]);
            if (p->move_left < 0) p->move_left = 0;
            set_vector(p, atof(tok[2]), atof(tok[3]));
            pilot_reply(p, "ok hold frames=%d mag=%.3f ang=%.3f",
                        p->move_left, p->mag, p->ang);
        } else pilot_reply(p, "err hold needs FRAMES R THETA");
    } else if (!strcmp(cmd, "stop") || !strcmp(cmd, "s") ||
               !strcmp(cmd, "halt") || !strcmp(cmd, "x")) {
        set_vector(p, 0.0, p->ang);
        p->move_left = 0;
        pilot_reply(p, "ok stop");
    } else if (!strcmp(cmd, "press") || !strcmp(cmd, "p")) {
        if (n >= 2) {
            int b = atoi(tok[1]) - 1;
            if (b >= 0 && b < 3) {
                p->btn[b] = true;
                p->click_left[b] = 0;
                pilot_reply(p, "ok press button=%d", b + 1);
            } else pilot_reply(p, "err invalid button");
        } else pilot_reply(p, "err press needs BUTTON");
    } else if (!strcmp(cmd, "release") || !strcmp(cmd, "u")) {
        if (n >= 2) {
            int b = atoi(tok[1]) - 1;
            if (b >= 0 && b < 3) {
                p->btn[b] = false;
                p->click_left[b] = 0;
                pilot_reply(p, "ok release button=%d", b + 1);
            } else pilot_reply(p, "err invalid button");
        } else pilot_reply(p, "err release needs BUTTON");
    } else if (!strcmp(cmd, "click") || !strcmp(cmd, "c")) {
        if (n >= 2) {
            int b = atoi(tok[1]) - 1;
            if (b >= 0 && b < 3) {
                p->btn[b] = true;
                p->click_left[b] = CLICK_HOLD_FRAMES;
                pilot_reply(p, "ok click button=%d frames=%d",
                            b + 1, CLICK_HOLD_FRAMES);
            } else pilot_reply(p, "err invalid button");
        } else pilot_reply(p, "err click needs BUTTON");
    } else if (!strcmp(cmd, "hold-click") || !strcmp(cmd, "holdclick")) {
        if (n >= 3) {
            int frames = atoi(tok[1]);
            int b = atoi(tok[2]) - 1;
            if (frames < 0) frames = 0;
            if (b >= 0 && b < 3) {
                p->btn[b] = true;
                p->click_left[b] = frames;
                pilot_reply(p, "ok hold-click button=%d frames=%d",
                            b + 1, frames);
            } else pilot_reply(p, "err invalid button");
        } else pilot_reply(p, "err hold-click needs FRAMES BUTTON");
    } else if (!strcmp(cmd, "scroll")) {
        if (n >= 2) {
            p->scroll_pending += atoi(tok[1]);
            pilot_reply(p, "ok scroll pending=%d", p->scroll_pending);
        } else pilot_reply(p, "err scroll needs DELTA");
    } else if (!strcmp(cmd, "target") || !strcmp(cmd, "t")) {
        if (n >= 2) {
            char k = (char)tolower((unsigned char)tok[1][0]);
            p->target = (k == 'j') ? PILOT_JOY : PILOT_MOUSE;
            pilot_reply(p, "ok target=%s",
                        p->target == PILOT_JOY ? "joy" : "mouse");
        } else pilot_reply(p, "err target needs mouse|joy");
    } else if (!strcmp(cmd, "reset")) {
        set_vector(p, 0.0, 0.0);
        p->move_left = 0;
        release_all_buttons(p);
        release_keytap(p, pcw);
        joy_release(pcw);
        p->joy_held = false;
        pcwmouse_clear_input(&pcw->mouse);
        pilot_reply(p, "ok reset");
    } else if (!strcmp(cmd, "state")) {
        reply_state(p, pcw);
    } else if (!strcmp(cmd, "frame")) {
        pilot_reply(p, "frame value=%d", p->frame_count);
    } else if (!strcmp(cmd, "hash")) {
        pilot_reply(p, "hash value=%08X", (unsigned)p->fb_hash);
    } else if (!strcmp(cmd, "changed")) {
        reply_changed(p);
    } else if (!strcmp(cmd, "key-down")) {
        if (n >= 2) {
            SDL_Scancode sc = scancode_from_token(tok[1]);
            if (sc != SDL_SCANCODE_UNKNOWN && kbd_sdl_key(&pcw->kbd, sc, true))
                pilot_reply(p, "ok key-down name=%s", tok[1]);
            else
                pilot_reply(p, "err key-down unknown=%s", tok[1]);
        } else pilot_reply(p, "err key-down needs NAME");
    } else if (!strcmp(cmd, "key-up")) {
        if (n >= 2) {
            SDL_Scancode sc = scancode_from_token(tok[1]);
            if (sc != SDL_SCANCODE_UNKNOWN && kbd_sdl_key(&pcw->kbd, sc, false))
                pilot_reply(p, "ok key-up name=%s", tok[1]);
            else
                pilot_reply(p, "err key-up unknown=%s", tok[1]);
        } else pilot_reply(p, "err key-up needs NAME");
    } else if (!strcmp(cmd, "key-tap")) {
        if (n >= 3) {
            SDL_Scancode sc = scancode_from_token(tok[1]);
            int frames = atoi(tok[2]);
            if (frames < 1) frames = 1;
            if (sc != SDL_SCANCODE_UNKNOWN && kbd_sdl_key(&pcw->kbd, sc, true)) {
                release_keytap(p, pcw);
                p->keytap_scancode = (int)sc;
                p->keytap_left = frames;
                pilot_reply(p, "ok key-tap name=%s frames=%d", tok[1], frames);
            } else pilot_reply(p, "err key-tap unknown=%s", tok[1]);
        } else pilot_reply(p, "err key-tap needs NAME FRAMES");
    } else if (!strcmp(cmd, "paste")) {
        char *text = raw;
        while (*text && !isspace((unsigned char)*text)) text++;
        while (*text && isspace((unsigned char)*text)) text++;
        paste_text_raw(paste, text);
        pilot_reply(p, "ok paste len=%d", (int)strlen(text));
    } else if (!strcmp(cmd, "snapshot-save")) {
        if (n >= 2) {
            if (snapshot_save(pcw, tok[1]) == 0)
                pilot_reply(p, "ok snapshot-save path=%s", tok[1]);
            else
                pilot_reply(p, "err snapshot-save path=%s", tok[1]);
        } else pilot_reply(p, "err snapshot-save needs PATH");
    } else if (!strcmp(cmd, "snapshot-load")) {
        if (n >= 2) {
            if (snapshot_load(pcw, tok[1]) == 0) {
                p->have_last_pixels = false;
                p->changed_w = p->changed_h = 0;
                p->quiet_frames = 0;
                pilot_reply(p, "ok snapshot-load path=%s", tok[1]);
            } else pilot_reply(p, "err snapshot-load path=%s", tok[1]);
        } else pilot_reply(p, "err snapshot-load needs PATH");
    } else if (!strcmp(cmd, "crop")) {
        if (n >= 6) {
            int x = atoi(tok[2]), y = atoi(tok[3]);
            int w = atoi(tok[4]), h = atoi(tok[5]);
            int scale = (n >= 7) ? atoi(tok[6]) : 1;
            if (display_save_crop_ppm(d, tok[1], x, y, w, h, scale))
                pilot_reply(p, "ok crop path=%s rect=%d,%d,%d,%d scale=%d",
                            tok[1], x, y, w, h, scale < 1 ? 1 : scale);
            else
                pilot_reply(p, "err crop path=%s rect=%d,%d,%d,%d scale=%d",
                            tok[1], x, y, w, h, scale < 1 ? 1 : scale);
        } else pilot_reply(p, "err crop needs PATH X Y W H [SCALE]");
    } else if (!strcmp(cmd, "wait")) {
        if (n < 2) {
            pilot_reply(p, "err wait needs MODE");
        } else if (!strcmp(tok[1], "frames")) {
            if (n >= 3) {
                p->wait_target_frames = atoi(tok[2]);
                if (p->wait_target_frames < 0) p->wait_target_frames = 0;
                start_wait(p, PILOT_WAIT_FRAMES, frame_now,
                           parse_timeout(tok, n, 3, frame_now));
            } else pilot_reply(p, "err wait frames needs N [TIMEOUT]");
        } else if (!strcmp(tok[1], "hash-eq")) {
            if (n >= 3) {
                p->wait_hash = (u32)strtoul(tok[2], NULL, 16);
                start_wait(p, PILOT_WAIT_HASH_EQ, frame_now,
                           parse_timeout(tok, n, 3, frame_now));
            } else pilot_reply(p, "err wait hash-eq needs HEX [TIMEOUT]");
        } else if (!strcmp(tok[1], "hash-ne")) {
            if (n >= 3) {
                p->wait_hash = (u32)strtoul(tok[2], NULL, 16);
                start_wait(p, PILOT_WAIT_HASH_NE, frame_now,
                           parse_timeout(tok, n, 3, frame_now));
            } else pilot_reply(p, "err wait hash-ne needs HEX [TIMEOUT]");
        } else if (!strcmp(tok[1], "change")) {
            start_wait(p, PILOT_WAIT_CHANGE, frame_now,
                       parse_timeout(tok, n, 2, frame_now));
        } else if (!strcmp(tok[1], "quiet")) {
            if (n >= 3) {
                p->wait_quiet_need = atoi(tok[2]);
                if (p->wait_quiet_need < 1) p->wait_quiet_need = 1;
                start_wait(p, PILOT_WAIT_QUIET, frame_now,
                           parse_timeout(tok, n, 3, frame_now));
            } else pilot_reply(p, "err wait quiet needs N [TIMEOUT]");
        } else {
            pilot_reply(p, "err wait unknown=%s", tok[1]);
        }
    } else {
        fprintf(stderr, "[pilot] unknown command: %s\n", cmd);
        pilot_reply(p, "err unknown command=%s", cmd);
    }
}

static unsigned char joy_mask_for(double mag, double ang_deg)
{
    double a;
    int sector;
    static const unsigned char m[8] = {
        1u << 3,
        (1u << 3) | (1u << 0),
        1u << 0,
        (1u << 2) | (1u << 0),
        1u << 2,
        (1u << 2) | (1u << 1),
        1u << 1,
        (1u << 3) | (1u << 1),
    };

    if (mag < JOY_DEADZONE) return 0;
    a = fmod(ang_deg, 360.0);
    if (a < 0) a += 360.0;
    sector = ((int)floor(a / 45.0 + 0.5)) & 7;
    return m[sector];
}

static void joy_set(PCW *pcw, unsigned char mask)
{
    if (mask & (1u << 0)) kbd_press(&pcw->kbd, PCW_KEY_UP_ROW, PCW_KEY_UP_BIT);
    else                  kbd_release(&pcw->kbd, PCW_KEY_UP_ROW, PCW_KEY_UP_BIT);

    if (mask & (1u << 1)) kbd_press(&pcw->kbd, PCW_KEY_DOWN_ROW, PCW_KEY_DOWN_BIT);
    else                  kbd_release(&pcw->kbd, PCW_KEY_DOWN_ROW, PCW_KEY_DOWN_BIT);

    if (mask & (1u << 2)) kbd_press(&pcw->kbd, PCW_KEY_LEFT_ROW, PCW_KEY_LEFT_BIT);
    else                  kbd_release(&pcw->kbd, PCW_KEY_LEFT_ROW, PCW_KEY_LEFT_BIT);

    if (mask & (1u << 3)) kbd_press(&pcw->kbd, PCW_KEY_RIGHT_ROW, PCW_KEY_RIGHT_BIT);
    else                  kbd_release(&pcw->kbd, PCW_KEY_RIGHT_ROW, PCW_KEY_RIGHT_BIT);

    if (mask & (1u << 4)) kbd_press(&pcw->kbd, PCW_KEY_SPACE_ROW, PCW_KEY_SPACE_BIT);
    else                  kbd_release(&pcw->kbd, PCW_KEY_SPACE_ROW, PCW_KEY_SPACE_BIT);

    if (mask & (1u << 5)) kbd_press(&pcw->kbd, PCW_KEY_FIRE2_ROW, PCW_KEY_FIRE2_BIT);
    else                  kbd_release(&pcw->kbd, PCW_KEY_FIRE2_ROW, PCW_KEY_FIRE2_BIT);
}

void pilot_tick(Pilot *p, PCW *pcw, Display *d, Paste *paste)
{
    char c;
    int b;

    if (p->fd < 0) return;

    while (p->wait_mode == PILOT_WAIT_NONE && read(p->fd, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            p->line[p->line_len] = '\0';
            exec_line(p, pcw, d, paste, p->line);
            p->line_len = 0;
        } else if (p->line_len < (int)sizeof(p->line) - 1) {
            p->line[p->line_len++] = c;
        }
    }

    for (b = 0; b < 3; b++)
        if (p->click_left[b] > 0 && --p->click_left[b] == 0)
            p->btn[b] = false;
    if (p->keytap_left > 0 && --p->keytap_left == 0)
        release_keytap(p, pcw);

    if (p->target == PILOT_MOUSE) {
        int dx, dy;
        if (p->joy_held) {
            joy_release(pcw);
            p->joy_held = false;
        }
        p->acc_x += p->vx;
        p->acc_y += p->vy;
        dx = (int)p->acc_x;
        dy = (int)p->acc_y;
        p->acc_x -= dx;
        p->acc_y -= dy;
        if (dx || dy) pcwmouse_add_motion(&pcw->mouse, (float)dx, (float)dy);
        for (b = 0; b < 3; b++)
            pcwmouse_set_button(&pcw->mouse, b, p->btn[b]);
        p->scroll_pending = 0;
    } else {
        unsigned char mask = joy_mask_for(p->mag, p->ang);
        if (p->btn[0]) mask |= 1u << 4;
        if (p->btn[1]) mask |= 1u << 5;
        joy_set(pcw, mask);
        p->joy_held = true;
    }

    if (p->move_left > 0 && --p->move_left == 0)
        set_vector(p, 0.0, p->ang);
}

void pilot_post_frame(Pilot *p, PCW *pcw, Display *d, int frame_count)
{
    bool changed;
    (void)pcw;

    if (p->fd < 0) return;
    p->frame_count = frame_count;
    p->fb_hash = display_hash(d);
    if (!p->last_pixels) return;
    if (!p->have_last_pixels) {
        display_copy_visible(d, p->last_pixels);
        p->have_last_pixels = true;
        p->changed_x = p->changed_y = 0;
        p->changed_w = p->changed_h = 0;
        p->quiet_frames = 0;
        return;
    }

    changed = display_changed_rect(d, p->last_pixels, &p->changed_x,
                                   &p->changed_y, &p->changed_w,
                                   &p->changed_h);
    if (changed) p->quiet_frames = 0;
    else p->quiet_frames++;

    if (p->wait_mode == PILOT_WAIT_NONE) return;
    if (p->wait_timeout_frame >= 0 && frame_count >= p->wait_timeout_frame) {
        fail_wait(p, "timeout");
        return;
    }

    switch (p->wait_mode) {
    case PILOT_WAIT_FRAMES:
        if (frame_count - p->wait_start_frame >= p->wait_target_frames)
            finish_wait(p, "frames");
        break;
    case PILOT_WAIT_HASH_EQ:
        if (p->fb_hash == p->wait_hash) finish_wait(p, "hash-eq");
        break;
    case PILOT_WAIT_HASH_NE:
        if (p->fb_hash != p->wait_hash) finish_wait(p, "hash-ne");
        break;
    case PILOT_WAIT_CHANGE:
        if (changed) finish_wait(p, "change");
        break;
    case PILOT_WAIT_QUIET:
        if (p->quiet_frames >= p->wait_quiet_need) finish_wait(p, "quiet");
        break;
    case PILOT_WAIT_NONE:
    default:
        break;
    }
}

#else

bool pilot_open(Pilot *p, const char *link, PilotTarget initial,
                bool reply_stderr)
{
    (void)link;
    (void)initial;
    (void)reply_stderr;
    memset(p, 0, sizeof(*p));
    p->fd = -1;
    fprintf(stderr, "1985: --pilot is not supported on Windows\n");
    return false;
}

bool pilot_is_open(const Pilot *p)
{
    (void)p;
    return false;
}

void pilot_tick(Pilot *p, PCW *pcw, Display *d, Paste *paste)
{
    (void)p;
    (void)pcw;
    (void)d;
    (void)paste;
}

void pilot_post_frame(Pilot *p, PCW *pcw, Display *d, int frame_count)
{
    (void)p;
    (void)pcw;
    (void)d;
    (void)frame_count;
}

#endif
