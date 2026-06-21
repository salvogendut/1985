#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "overlay.h"
#include "pcw.h"
#include "paste.h"
#include "kbd.h"
#include "disk.h"
#include "roller.h"
#include "snapshot.h"
#include "gifcap.h"

#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif

/* z80.c references these via `extern` inside a #102 instrumentation
 * block gated on ONE_K_TRACE_IM1. Provide stub storage so the link
 * succeeds; the trace itself is dormant unless someone sets the env
 * variable AND g_debug_enabled is true (which we leave at 0). */
int g_debug_enabled = 0;
int cpc_frame_count = 0;

static const char *USAGE =
"Usage: 1985 [options]\n"
"  --config PATH               override config file path\n"
"  --memory KB                 RAM size: 256, 512 or 2048\n"
"  --disk-a PATH               mount .dsk in drive A\n"
"  --disk-b PATH               mount .dsk in drive B\n"
"  --paste TEXT                type TEXT after boot (\\n = Enter)\n"
"  --load-sna PATH             load .sna at init (stub)\n"
"  --save-sna-at N:PATH        save .sna at frame N (stub)\n"
"  --screenshot-at N:PATH      save PPM at frame N and exit\n"
"  --gif-out PATH              record GIF until exit\n"
"  --exit-after N              quit after frame N\n"
"  -h, --help                  show this help\n";

typedef struct {
    const char *config_path;
    const char *paste_text;
    const char *disk_a;
    const char *disk_b;
    const char *load_sna;
    const char *save_sna_arg;     /* "N:PATH" */
    const char *screenshot_arg;   /* "N:PATH" */
    const char *gif_out;
    int         memory_kb;        /* 0 = leave config alone */
    int         exit_after;       /* -1 = run forever */
} Cli;

static int parse_n_path(const char *arg, char *path_out, size_t path_size) {
    const char *colon = strchr(arg, ':');
    if (!colon) return -1;
    int n = atoi(arg);
    snprintf(path_out, path_size, "%s", colon + 1);
    return n;
}

static bool starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static const char *eq_value(const char *s, const char *flag) {
    /* Returns NULL if s != flag (with optional =VAL). Returns VAL if s
     * matches flag exactly with =VAL, or empty string if no '='. */
    size_t fl = strlen(flag);
    if (strncmp(s, flag, fl) != 0) return NULL;
    if (s[fl] == 0)   return "";
    if (s[fl] == '=') return s + fl + 1;
    return NULL;
}

static int parse_cli(int argc, char **argv, Cli *cli) {
    memset(cli, 0, sizeof(*cli));
    cli->exit_after = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v;

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            fputs(USAGE, stdout);
            exit(0);
        }
        if ((v = eq_value(a, "--config"))      && *v) { cli->config_path = v; continue; }
        if ((v = eq_value(a, "--memory"))      && *v) { cli->memory_kb   = atoi(v); continue; }
        if ((v = eq_value(a, "--disk-a"))      && *v) { cli->disk_a      = v; continue; }
        if ((v = eq_value(a, "--disk-b"))      && *v) { cli->disk_b      = v; continue; }
        if ((v = eq_value(a, "--paste"))       && *v) { cli->paste_text  = v; continue; }
        if ((v = eq_value(a, "--load-sna"))    && *v) { cli->load_sna    = v; continue; }
        if ((v = eq_value(a, "--save-sna-at")) && *v) { cli->save_sna_arg = v; continue; }
        if ((v = eq_value(a, "--screenshot-at")) && *v) { cli->screenshot_arg = v; continue; }
        if ((v = eq_value(a, "--gif-out"))     && *v) { cli->gif_out     = v; continue; }
        if ((v = eq_value(a, "--exit-after"))  && *v) { cli->exit_after  = atoi(v); continue; }

        /* Two-token form: --flag VALUE */
        if (strcmp(a, "--config")        == 0 && i+1 < argc) { cli->config_path = argv[++i]; continue; }
        if (strcmp(a, "--memory")        == 0 && i+1 < argc) { cli->memory_kb   = atoi(argv[++i]); continue; }
        if (strcmp(a, "--disk-a")        == 0 && i+1 < argc) { cli->disk_a      = argv[++i]; continue; }
        if (strcmp(a, "--disk-b")        == 0 && i+1 < argc) { cli->disk_b      = argv[++i]; continue; }
        if (strcmp(a, "--paste")         == 0 && i+1 < argc) { cli->paste_text  = argv[++i]; continue; }
        if (strcmp(a, "--load-sna")      == 0 && i+1 < argc) { cli->load_sna    = argv[++i]; continue; }
        if (strcmp(a, "--save-sna-at")   == 0 && i+1 < argc) { cli->save_sna_arg = argv[++i]; continue; }
        if (strcmp(a, "--screenshot-at") == 0 && i+1 < argc) { cli->screenshot_arg = argv[++i]; continue; }
        if (strcmp(a, "--gif-out")       == 0 && i+1 < argc) { cli->gif_out     = argv[++i]; continue; }
        if (strcmp(a, "--exit-after")    == 0 && i+1 < argc) { cli->exit_after  = atoi(argv[++i]); continue; }

        if (starts_with(a, "--")) {
            fprintf(stderr, "unknown option: %s\n%s", a, USAGE);
            return -1;
        }
        fprintf(stderr, "unexpected positional argument: %s\n%s", a, USAGE);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Cli cli;
    if (parse_cli(argc, argv, &cli) < 0) return 1;

    Config cfg;
    config_load(&cfg, cli.config_path);

    if (cli.memory_kb)   cfg.memory_kb = cli.memory_kb;
    if (cli.disk_a)      snprintf(cfg.drive_a, sizeof(cfg.drive_a), "%s", cli.disk_a);
    if (cli.disk_b)      snprintf(cfg.drive_b, sizeof(cfg.drive_b), "%s", cli.disk_b);

    Display disp;
    if (display_init(&disp, &cfg) < 0) return 1;

    PCW pcw;
    pcw_init(&pcw, cfg.model);
    pcw.trace_io = cfg.trace_io;

    if (cfg.drive_a[0]) disk_load(&pcw.fdc.drive[0], cfg.drive_a);
    if (cfg.drive_b[0]) disk_load(&pcw.fdc.drive[1], cfg.drive_b);

    if (cli.load_sna) snapshot_load(&pcw, cli.load_sna);

    Overlay ov;
    overlay_init(&ov, &cfg, &pcw);

    Paste paste;
    paste_init(&paste);
    if (cli.paste_text) paste_text(&paste, cli.paste_text);

    GifCap *gc = NULL;
    if (cli.gif_out) {
        gc = gifcap_open(cli.gif_out, DISPLAY_W, DISPLAY_H,
                         DISPLAY_W, DISPLAY_H, 2);
        if (!gc) fprintf(stderr, "gif-out: failed to open %s\n", cli.gif_out);
    }

    char screenshot_path[512] = {0};
    int  screenshot_frame = -1;
    if (cli.screenshot_arg)
        screenshot_frame = parse_n_path(cli.screenshot_arg, screenshot_path, sizeof(screenshot_path));

    char save_sna_path[512] = {0};
    int  save_sna_frame = -1;
    if (cli.save_sna_arg)
        save_sna_frame = parse_n_path(cli.save_sna_arg, save_sna_path, sizeof(save_sna_path));

    printf("1985 — Amstrad PCW 8256 emulator (git %s)\n", PROG_GIT_COMMIT);
    printf("config: %s\n", cfg.path);
    printf("F9 = options, F11 = fullscreen, F12 = quit\n");

    bool running = true;
    int  frame   = 0;
    Uint64 next_tick_ms = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (overlay_handle_event(&ov, &ev)) continue;
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    running = false; break;
                case SDL_EVENT_KEY_DOWN:
                    switch (ev.key.key) {
                        case SDLK_F4: {
                            char path[64];
                            snprintf(path, sizeof(path), "1985-%05d.ppm", frame);
                            display_save_ppm(&disp, path);
                            printf("screenshot: %s\n", path);
                            break;
                        }
                        case SDLK_F5: pcw_reset(&pcw); break;
                        case SDLK_F11: display_toggle_fullscreen(&disp); break;
                        case SDLK_F12: running = false; break;
                        default:
                            kbd_handle(&pcw.kbd, &ev.key);
                            break;
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    kbd_handle(&pcw.kbd, &ev.key);
                    break;
                default: break;
            }
        }

        paste_tick(&paste, &pcw.kbd);
        overlay_tick(&ov);
        pcw_frame(&pcw);
        roller_render(&pcw.mem, &pcw.asic, &disp);
        display_draw_framebuffer(&disp);
        overlay_render(&ov, disp.renderer);

        if (gc) gifcap_frame(gc, disp.fb);

        if (frame == screenshot_frame) {
            display_save_ppm(&disp, screenshot_path);
            printf("screenshot: %s\n", screenshot_path);
            running = false;
        }
        if (frame == save_sna_frame) snapshot_save(&pcw, save_sna_path);
        if (cli.exit_after >= 0 && frame >= cli.exit_after) running = false;

        display_present(&disp);

        /* Frame pacing — aim for 50 Hz. */
        next_tick_ms += 20;
        Uint64 now = SDL_GetTicks();
        if (now < next_tick_ms) SDL_Delay((Uint32)(next_tick_ms - now));
        else                    next_tick_ms = now;
        frame++;
    }

    if (gc) gifcap_close(gc);
    paste_free(&paste);
    display_quit(&disp);
    return 0;
}
