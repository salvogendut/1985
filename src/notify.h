#pragma once

#include <stdbool.h>

struct SDL_Renderer;

/* On-screen toast notifications. notify_post() always writes to stderr
 * (so headless runs and CI logs are unchanged); when enabled, it also
 * queues the message for a fading bottom-left overlay rendered by
 * notify_render(). Toggle via the F9 → Advanced "Notifications" row.
 *
 * The whole module is a single global singleton — call-sites in
 * disk.c / serial.c / perryfi.c need no Notify* in scope. */

void notify_init(void);
void notify_set_enabled(bool on);

void notify_post(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void notify_tick(int dt_ms);
void notify_render(struct SDL_Renderer *r, int win_w, int win_h);
