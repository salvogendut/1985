#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include "leds.h"
#include "z80dis.h"
#include "mem.h"
#include "fdc.h"
#include "asic.h"
#include "monitor.h"
#include "aysound.h"
#include "notify.h"
#include "shutter_wav.h"
#include "webgui.h"
#include "websvc.h"

/* PCW DK'tronics AY clock — same 1 MHz the CPC sibling uses for its
 * AY-3-8912. Real DK'tronics divides off the PCW system clock; 1 MHz
 * is the conventional rate for the audible bandwidth modelled here. */
#define AY_AUDIO_RATE         44100
#define AY_SAMPLES_FRAME      (AY_AUDIO_RATE / 50)
#define AY_CLOCK_HZ           1000000
#define MAX_PASTE_EVENTS      16

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
"  --boot-ems PATH             load raw EMS/EMT image at 0000h\n"
"  --auto-space                send SPACE once after boot image appears\n"
"  --paste TEXT                type TEXT after boot (\\n = Enter)\n"
"  --paste-at N                start --paste injection at frame N\n"
"  --paste-event N:TEXT        inject TEXT at frame N (repeatable)\n"
"  --disk-event N:D:PATH       at frame N put PATH in drive D (a/b);\n"
"                              empty PATH ejects (repeatable)\n"
"  --load-sna PATH             load .sna at init (stub)\n"
"  --save-sna-at N:PATH        save .sna at frame N (stub)\n"
"  --screenshot-at N:PATH      save PPM at frame N and exit\n"
"  --gif-out PATH              record GIF until exit\n"
"  --exit-after N              quit after frame N\n"
"  --dump-at N                 dump CPU, memory and FDC state at frame N\n"
"  --unthrottled               disable 50 Hz frame pacing (diagnostics)\n"
"  --web[=PORT]                serve the emulator to a browser over HTTP\n"
"                              (default port 1985). Implies --headless: no\n"
"                              window on the host, the browser is the only\n"
"                              interface. (The overlay toggle / web_gui\n"
"                              config key serve with the window visible.)\n"
"                              Binds 0.0.0.0 — anyone on the network can\n"
"                              view and type; no authentication.\n"
"  --headless                  no window on the host: offscreen video and\n"
"                              dummy audio drivers (implied by --web)\n"
"  -h, --help                  show this help\n";

typedef struct {
    int         frame;
    const char *text;
} CliPasteEvent;

/* Scripted disk change: mirrors the overlay Media tab's swap/eject so
 * multi-disk software (boot CP/M, swap in the app disk, run it) can be
 * driven headlessly. path == "" means eject. */
typedef struct {
    int         frame;
    int         drive;            /* 0 = A, 1 = B */
    const char *path;
} CliDiskEvent;

typedef struct {
    const char *config_path;
    const char *paste_text;
    const char *disk_a;
    const char *disk_b;
    const char *boot_ems;
    const char *load_sna;
    const char *save_sna_arg;     /* "N:PATH" */
    const char *screenshot_arg;   /* "N:PATH" */
    const char *gif_out;
    bool        auto_space;
    bool        unthrottled;
    CliPasteEvent paste_event[MAX_PASTE_EVENTS];
    int         paste_event_count;
    CliDiskEvent disk_event[MAX_PASTE_EVENTS];
    int         disk_event_count;
    int         memory_kb;        /* 0 = leave config alone */
    int         paste_at;         /* 0 = begin immediately */
    int         exit_after;       /* -1 = run forever */
    int         dump_at;          /* -1 = no dump */
    bool        web;              /* --web[=PORT] */
    int         web_port;         /* 0 = use config value */
    bool        headless;         /* --headless: offscreen video */
} Cli;

static void dump_state(PCW *pcw, int frame) {
    Z80 *c = &pcw->cpu;
    fprintf(stderr, "\n==== DUMP frame=%d ====\n", frame);
    fprintf(stderr, "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n",
            c->pc, c->sp, c->af, c->bc, c->de, c->hl, c->ix, c->iy);
    fprintf(stderr, "AF'=%04X BC'=%04X DE'=%04X HL'=%04X I=%02X R=%02X IM=%d iff1=%d iff2=%d halted=%d\n",
            c->af_, c->bc_, c->de_, c->hl_, c->i, c->r, c->im, c->iff1, c->iff2, c->halted);
    fprintf(stderr, "bank R[%02X %02X %02X %02X] W[%02X %02X %02X %02X] force=%02X\n",
            pcw->mem.read_bank[0], pcw->mem.read_bank[1], pcw->mem.read_bank[2], pcw->mem.read_bank[3],
            pcw->mem.write_bank[0], pcw->mem.write_bank[1], pcw->mem.write_bank[2], pcw->mem.write_bank[3],
            pcw->mem.bank_force);
    fprintf(stderr, "asic ic=%X fdc_irq_mode=%d prev_fdc_irq=%d flyback=%d\n",
            pcw->asic.interrupt_counter, pcw->asic.fdc_irq_mode, pcw->asic.prev_fdc_irq, pcw->asic.flyback);
    fprintf(stderr,
            "fdc  phase=%d msr=%02X irq=%d arm=%d pulses=%u tc=%d motor=%d "
            "unit=%d head=%d cyl=[%d %d] int_pending=%d\n",
            pcw->fdc.phase, pcw->fdc.msr, pcw->fdc.irq,
            pcw->fdc.irq_arm_ticks, pcw->fdc.irq_pulse_count,
            pcw->fdc.tc, pcw->fdc.motor_on, pcw->fdc.cur_unit,
            pcw->fdc.cur_head, pcw->fdc.cur_cyl[0], pcw->fdc.cur_cyl[1],
            pcw->fdc.int_pending);
    fprintf(stderr,
            "fdc  cmd=%02X %02X %02X %02X %02X %02X %02X %02X %02X "
            "pos=%d/%d exec=%d/%d result=%d/%d st=%02X/%02X/%02X/%02X\n",
            pcw->fdc.cmd_buf[0], pcw->fdc.cmd_buf[1], pcw->fdc.cmd_buf[2],
            pcw->fdc.cmd_buf[3], pcw->fdc.cmd_buf[4], pcw->fdc.cmd_buf[5],
            pcw->fdc.cmd_buf[6], pcw->fdc.cmd_buf[7], pcw->fdc.cmd_buf[8],
            pcw->fdc.cmd_pos, pcw->fdc.cmd_len,
            pcw->fdc.exec_pos, pcw->fdc.exec_len,
            pcw->fdc.result_pos, pcw->fdc.result_len,
            pcw->fdc.st0, pcw->fdc.st1, pcw->fdc.st2, pcw->fdc.st3);

    /* Disassemble 24 instructions around PC. */
    static u8 snap[65536];
    for (int a = 0; a < 65536; a++) snap[a] = mem_read(&pcw->mem, (u16)a);
    fprintf(stderr, "--- disasm @ PC ---\n");
    u16 dp = c->pc;
    for (int i = 0; i < 24; i++) {
        char buf[64];
        int n = z80dis(snap, dp, buf, sizeof(buf));
        fprintf(stderr, "  %04X: %s\n", dp, buf);
        dp = (u16)(dp + n);
    }

    /* Also disassemble at known dispatcher entry/timer-handler points. */
    const u16 dump_dis[] = {
        0x0770, 0x077B, 0x078B, 0x07A4, 0x07C3, 0x07E6, 0x0853, 0x0880, 0x08A0, 0x08AB, 0x08E9,
        0x0D43, 0x0AD0, 0x0A98, 0x07D4, 0x0030,
        0x0B6A, 0x4734, 0x4E84,
        0x0F50, 0x0F5A, 0x1D40,
        /* BIOS common-area jumpblock and SELDSK chain */
        0xFC00, 0xFC1B, 0xFC42, 0xFC4B, 0xFC51, 0xFC5A,
        0xFD2C, 0xFD2D, 0xFD64, 0xFD70, 0xFD84, 0xFDA8,
        0xFE3A, 0xFE3E,
    };
    for (size_t k = 0; k < sizeof(dump_dis)/sizeof(dump_dis[0]); k++) {
        u16 dpp = dump_dis[k];
        fprintf(stderr, "--- disasm @ %04X ---\n", dpp);
        for (int i = 0; i < 20; i++) {
            char buf[64];
            int n = z80dis(snap, dpp, buf, sizeof(buf));
            fprintf(stderr, "  %04X: %s\n", dpp, buf);
            dpp = (u16)(dpp + n);
        }
    }
    /* Follow the (0x10A0) callback pointer. */
    u16 cb = mem_read(&pcw->mem, 0x10A0) | (mem_read(&pcw->mem, 0x10A1) << 8);
    fprintf(stderr, "--- disasm @ (0x10A0)=%04X ---\n", cb);
    u16 dpp = cb;
    for (int i = 0; i < 16; i++) {
        char buf[64];
        int n = z80dis(snap, dpp, buf, sizeof(buf));
        fprintf(stderr, "  %04X: %s\n", dpp, buf);
        dpp = (u16)(dpp + n);
    }

    /* Disassemble bank 8 (CCP) directly: the BIOS inter-bank trampoline
     * maps bank 8 to slot 1 starting at 0x4000, so bank-8 offset 0x1D80
     * corresponds to CPU PC 0x5D80 when bank 8 is mapped. We've seen
     * pc=5D9D loop hot in the trace — dump the surrounding code. */
    {
        static u8 b8snap[65536];
        memset(b8snap, 0, sizeof(b8snap));
        memcpy(b8snap + 0x4000, &pcw->mem.ram[8 * MEM_BLOCK_SIZE], MEM_BLOCK_SIZE);
        /* Also dump helpers at 4D3C/48DC and a few other histogram-hit
         * addresses that called from the hot loop at 5180. */
        const u16 b8_help[] = { 0x4D3C, 0x48DC, 0x4A00, 0x4992, 0x499F, 0x49A0, 0x49A4, 0x47C0 };
        for (size_t i = 0; i < sizeof(b8_help)/sizeof(b8_help[0]); i++) {
            u16 a = b8_help[i];
            fprintf(stderr, "--- disasm bank8 helper @ %04X ---\n", a);
            for (int j = 0; j < 12; j++) {
                char buf[64];
                int n = z80dis(b8snap, a, buf, sizeof(buf));
                fprintf(stderr, "  %04X: %s\n", a, buf);
                a = (u16)(a + n);
            }
        }
        /* Also disassemble the foreground hot-loop region 0x5180-0x51A8
         * identified by the fg_hist PC sampler. That's bank-8 offset
         * 0x1180. */
        fprintf(stderr, "--- disasm bank8@4000+1180 (i.e. pc=5180) ---\n");
        u16 b8h = 0x5180;
        for (int i = 0; i < 50; i++) {
            char buf[64];
            int n = z80dis(b8snap, b8h, buf, sizeof(buf));
            fprintf(stderr, "  %04X: %s\n", b8h, buf);
            b8h = (u16)(b8h + n);
        }
        fprintf(stderr, "--- disasm bank8@4000+1D80 (i.e. pc=5D80) ---\n");
        u16 bp = 0x5D80;
        for (int i = 0; i < 32; i++) {
            char buf[64];
            int n = z80dis(b8snap, bp, buf, sizeof(buf));
            fprintf(stderr, "  %04X: %s\n", bp, buf);
            bp = (u16)(bp + n);
        }
        fprintf(stderr, "raw blk8@1D80:");
        for (int i = 0; i < 64; i++)
            fprintf(stderr, " %02X", pcw->mem.ram[8 * MEM_BLOCK_SIZE + 0x1D80 + i]);
        fprintf(stderr, "\n");
    }

    /* Memory dump of likely work-queue heads. */
    static const u16 mem_pts[] = { 0x0000, 0x0021, 0x0030, 0x0038, 0x0040, 0x0060, 0x0100, 0x0D00, 0x1010, 0x10A0, 0x0E80, 0x4720, 0xBFF0, 0xFC00, 0xFE70, 0xFE77, 0xFFF0 };
    /* Follow (0xFE77) — BIOS-installed custom-ISR pointer. */
    u16 fe77 = mem_read(&pcw->mem, 0xFE77) | (mem_read(&pcw->mem, 0xFE78) << 8);
    fprintf(stderr, "--- disasm @ (0xFE77)=%04X ---\n", fe77);
    u16 fp = fe77;
    for (int i = 0; i < 24; i++) {
        char buf[64];
        int n = z80dis(snap, fp, buf, sizeof(buf));
        fprintf(stderr, "  %04X: %s\n", fp, buf);
        fp = (u16)(fp + n);
    }
    /* Also dump raw block 3 at offset 0x3FF0 (where keyboard window
     * physically lives, independent of which slot maps it). */
    fprintf(stderr, "raw blk3@3FF0:");
    for (int i = 0; i < 16; i++)
        fprintf(stderr, " %02X", pcw->mem.ram[3 * MEM_BLOCK_SIZE + 0x3FF0 + i]);
    fprintf(stderr, "\n");
    for (size_t k = 0; k < sizeof(mem_pts)/sizeof(mem_pts[0]); k++) {
        u16 base = mem_pts[k];
        fprintf(stderr, "mem %04X:", base);
        for (int i = 0; i < 32; i++)
            fprintf(stderr, " %02X", mem_read(&pcw->mem, (u16)(base + i)));
        fprintf(stderr, "\n");
    }
    /* Stack peek (SP..SP+16) */
    fprintf(stderr, "stk %04X:", c->sp);
    for (int i = 0; i < 16; i++)
        fprintf(stderr, " %02X", mem_read(&pcw->mem, (u16)(c->sp + i)));
    fprintf(stderr, "\n==== END DUMP ====\n");
    fflush(stderr);
}

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

static int add_paste_event(Cli *cli, const char *spec) {
    if (cli->paste_event_count >= MAX_PASTE_EVENTS) {
        fprintf(stderr, "too many --paste-event options (max %d)\n",
                MAX_PASTE_EVENTS);
        return -1;
    }

    char *end;
    long frame = strtol(spec, &end, 10);
    if (end == spec || *end != ':' || frame < 0 || !end[1]) {
        fprintf(stderr, "invalid --paste-event '%s' (expected N:TEXT)\n", spec);
        return -1;
    }

    CliPasteEvent *event = &cli->paste_event[cli->paste_event_count++];
    event->frame = (int)frame;
    event->text = end + 1;
    return 0;
}

static int add_disk_event(Cli *cli, const char *spec) {
    if (cli->disk_event_count >= MAX_PASTE_EVENTS) {
        fprintf(stderr, "too many --disk-event options (max %d)\n",
                MAX_PASTE_EVENTS);
        return -1;
    }

    char *end;
    long frame = strtol(spec, &end, 10);
    if (end == spec || *end != ':' || frame < 0
        || (end[1] != 'a' && end[1] != 'b' && end[1] != 'A' && end[1] != 'B')
        || end[2] != ':') {
        fprintf(stderr,
                "invalid --disk-event '%s' (expected N:D:PATH, D = a|b)\n",
                spec);
        return -1;
    }

    CliDiskEvent *event = &cli->disk_event[cli->disk_event_count++];
    event->frame = (int)frame;
    event->drive = (end[1] == 'b' || end[1] == 'B') ? 1 : 0;
    event->path  = end + 3;
    return 0;
}

static int parse_cli(int argc, char **argv, Cli *cli) {
    memset(cli, 0, sizeof(*cli));
    cli->exit_after = -1;
    cli->dump_at    = -1;

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
        if ((v = eq_value(a, "--boot-ems"))    && *v) { cli->boot_ems    = v; continue; }
        if ((v = eq_value(a, "--paste"))       && *v) { cli->paste_text  = v; continue; }
        if ((v = eq_value(a, "--paste-at"))    && *v) { cli->paste_at    = atoi(v); continue; }
        if ((v = eq_value(a, "--paste-event")) && *v) {
            if (add_paste_event(cli, v) < 0) return -1;
            continue;
        }
        if ((v = eq_value(a, "--disk-event")) && *v) {
            if (add_disk_event(cli, v) < 0) return -1;
            continue;
        }
        if ((v = eq_value(a, "--load-sna"))    && *v) { cli->load_sna    = v; continue; }
        if ((v = eq_value(a, "--save-sna-at")) && *v) { cli->save_sna_arg = v; continue; }
        if ((v = eq_value(a, "--screenshot-at")) && *v) { cli->screenshot_arg = v; continue; }
        if ((v = eq_value(a, "--gif-out"))     && *v) { cli->gif_out     = v; continue; }
        if ((v = eq_value(a, "--exit-after"))  && *v) { cli->exit_after  = atoi(v); continue; }
        if ((v = eq_value(a, "--dump-at"))     && *v) { cli->dump_at     = atoi(v); continue; }
        if (strcmp(a, "--auto-space") == 0) { cli->auto_space = true; continue; }
        if (strcmp(a, "--unthrottled") == 0) { cli->unthrottled = true; continue; }
        if ((v = eq_value(a, "--web")) && *v) {
            int p = atoi(v);
            if (p < 1 || p > 65535) {
                fprintf(stderr, "--web=PORT must be 1..65535\n");
                return -1;
            }
            cli->web = true;
            cli->web_port = p;
            continue;
        }
        if (strcmp(a, "--web") == 0)      { cli->web = true; continue; }
        if (strcmp(a, "--headless") == 0) { cli->headless = true; continue; }

        /* Two-token form: --flag VALUE */
        if (strcmp(a, "--config")        == 0 && i+1 < argc) { cli->config_path = argv[++i]; continue; }
        if (strcmp(a, "--memory")        == 0 && i+1 < argc) { cli->memory_kb   = atoi(argv[++i]); continue; }
        if (strcmp(a, "--disk-a")        == 0 && i+1 < argc) { cli->disk_a      = argv[++i]; continue; }
        if (strcmp(a, "--disk-b")        == 0 && i+1 < argc) { cli->disk_b      = argv[++i]; continue; }
        if (strcmp(a, "--boot-ems")      == 0 && i+1 < argc) { cli->boot_ems    = argv[++i]; continue; }
        if (strcmp(a, "--paste")         == 0 && i+1 < argc) { cli->paste_text  = argv[++i]; continue; }
        if (strcmp(a, "--paste-at")      == 0 && i+1 < argc) { cli->paste_at    = atoi(argv[++i]); continue; }
        if (strcmp(a, "--paste-event")   == 0 && i+1 < argc) {
            if (add_paste_event(cli, argv[++i]) < 0) return -1;
            continue;
        }
        if (strcmp(a, "--disk-event")    == 0 && i+1 < argc) {
            if (add_disk_event(cli, argv[++i]) < 0) return -1;
            continue;
        }
        if (strcmp(a, "--load-sna")      == 0 && i+1 < argc) { cli->load_sna    = argv[++i]; continue; }
        if (strcmp(a, "--save-sna-at")   == 0 && i+1 < argc) { cli->save_sna_arg = argv[++i]; continue; }
        if (strcmp(a, "--screenshot-at") == 0 && i+1 < argc) { cli->screenshot_arg = argv[++i]; continue; }
        if (strcmp(a, "--gif-out")       == 0 && i+1 < argc) { cli->gif_out     = argv[++i]; continue; }
        if (strcmp(a, "--exit-after")    == 0 && i+1 < argc) { cli->exit_after  = atoi(argv[++i]); continue; }
        if (strcmp(a, "--dump-at")       == 0 && i+1 < argc) { cli->dump_at     = atoi(argv[++i]); continue; }
        if (strcmp(a, "--auto-space")    == 0) { cli->auto_space = true; continue; }
        if (strcmp(a, "--unthrottled")   == 0) { cli->unthrottled = true; continue; }

        if (starts_with(a, "--")) {
            fprintf(stderr, "unknown option: %s\n%s", a, USAGE);
            return -1;
        }
        fprintf(stderr, "unexpected positional argument: %s\n%s", a, USAGE);
        return -1;
    }
    return 0;
}

static int load_raw_image(PCW *pcw, const char *path, u16 base) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0 || size > (long)(MEM_SIZE - base)) {
        fclose(f);
        fprintf(stderr, "boot image too large: %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    if (fread(&pcw->mem.ram[base], 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        fprintf(stderr, "boot image read failed: %s\n", path);
        return -1;
    }
    fclose(f);
    return 0;
}

static bool mouse_input_enabled(const Config *cfg) {
    return cfg->ext_sanpollo_backplane
        && cfg->ext_dktronics
        && cfg->input_device == INPUT_DEVICE_MOUSE;
}

static void set_mouse_capture(Display *disp, PcwMouse *mouse,
                              bool *captured, bool enable) {
    if (*captured == enable) return;
    if (!SDL_SetWindowRelativeMouseMode(disp->win, enable)) {
        fprintf(stderr, "mouse capture: %s\n", SDL_GetError());
        return;
    }
    *captured = enable;
    if (enable) leds_set_mouse_position(0.0f, 0.0f, false);
    if (!enable) pcwmouse_clear_input(mouse);
}

static void leds_track_event(Display *disp, SDL_WindowID window_id,
                             const SDL_Event *ev, bool mouse_captured) {
    if (mouse_captured) {
        if (ev->type == SDL_EVENT_MOUSE_MOTION ||
            ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            ev->type == SDL_EVENT_MOUSE_BUTTON_UP ||
            ev->type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
            leds_set_mouse_position(0.0f, 0.0f, false);
        return;
    }

    float rx = 0.0f, ry = 0.0f;
    if (ev->type == SDL_EVENT_MOUSE_MOTION &&
        ev->motion.windowID == window_id &&
        SDL_RenderCoordinatesFromWindow(disp->renderer,
                                        ev->motion.x, ev->motion.y,
                                        &rx, &ry)) {
        leds_set_mouse_position(rx, ry, true);
    } else if ((ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                ev->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
               ev->button.windowID == window_id &&
               SDL_RenderCoordinatesFromWindow(disp->renderer,
                                               ev->button.x, ev->button.y,
                                               &rx, &ry)) {
        leds_set_mouse_position(rx, ry, true);
    } else if ((ev->type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
                ev->type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
               ev->window.windowID == window_id) {
        leds_set_mouse_position(0.0f, 0.0f, false);
    }
}

/* Re-apply every config-driven setting that pcw_init/pcw_cold_boot
 * does NOT cover by itself: printer mode/paths, debug traces, disk
 * mounts, serial/PerryFi/CPS gating, AY-sound, LED visibility. Called
 * once at startup (right after pcw_init) and again on F5 (right after
 * pcw_cold_boot) so a cold reset puts the machine into the exact same
 * shape as a fresh process launch. */
void apply_runtime_config(PCW *pcw, const Config *cfg) {
    /* Region (PAL/NTSC) drives frame cadence, ticks-per-frame, and
     * the visible-window clamp. Sampled at frame-loop entry, so the
     * caller will normally have cold-booted before getting here. */
    pcw->asic.refresh_60hz = (cfg->region == REGION_NTSC);

    printer_set_pdf_output_dir(&pcw->printer, cfg->ext_pdf_printer_dir);
    printer_set_pdf_enabled(&pcw->printer,
                            cfg->ext_pdf_printer && cfg->ext_pdf_printer_dir[0]);
    printer_set_sink(&pcw->printer, cfg->ext_print_sink);
    printer_set_kind(&pcw->printer,
                     cfg->model == PCW_MODEL_9512 ? PRINTER_KIND_DAISYWHEEL
                                                  : PRINTER_KIND_DOT_MATRIX);
    pcw->turbo        = cfg->turbo;
    pcw->debug_traces = cfg->debug_traces;
    pcw->trace_io     = cfg->debug_traces && cfg->trace_io;
    pcw->fdc.trace    = cfg->debug_traces && cfg->trace_fdc;

    if (cfg->drive_a[0]) disk_load(&pcw->fdc.drive[0], cfg->drive_a);
    if (cfg->drive_b[0]) disk_load(&pcw->fdc.drive[1], cfg->drive_b);

    bool serial_avail = (cfg->model == PCW_MODEL_9512) || cfg->ext_sanpollo_backplane;
    bool serial_on    = cfg->ext_serial  && serial_avail;
    bool perryfi_on   = serial_on        && cfg->ext_perryfi;
    /* Only (re)open the host-side backends if they're not already
     * alive. F5 cold-reset preserves the Serial/Perryfi structs across
     * pcw_cold_boot specifically so the user's PTY stays at the same
     * /dev/pts/N and any attached peer (e.g. tools/pty_modem.py)
     * keeps working. */
    if (!pcw->serial.present)
        serial_init(&pcw->serial, serial_on && !perryfi_on,
                    cfg->ext_serial_backend, cfg->ext_serial_tcp_port,
                    cfg->ext_serial_pty_link);
    if (!pcw->perryfi.present)
        perryfi_init(&pcw->perryfi, perryfi_on, cfg->perryfi_mode);
    cps_set_present(&pcw->cps, serial_on);
    leds_set_enabled(LED_SERIAL, serial_on);

    bool dk_on = cfg->ext_sanpollo_backplane && cfg->ext_dktronics;
    aysound_init(&pcw->ay, dk_on);
    multilink_set_present(&pcw->multilink,
                          cfg->ext_sanpollo_backplane && cfg->ext_multilink);
    pcwmouse_configure(&pcw->mouse,
                       dk_on && cfg->input_device == INPUT_DEVICE_MOUSE,
                       cfg->mouse_type);

    leds_set_enabled(LED_FDC_A, true);
    leds_set_enabled(LED_FDC_B,
                     cfg->model != PCW_MODEL_8256 || cfg->ext_second_drive);
    leds_set_enabled(LED_PRINTER, cfg->ext_pdf_printer);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    /* GUI-subsystem build: stderr/stdout have nowhere to go when the
     * exe is double-clicked. Redirect them to log files next to the
     * binary so init failures are diagnosable. */
    freopen("1985.log", "w", stderr);
    freopen("1985.out", "w", stdout);
    setvbuf(stderr, NULL, _IONBF, 0);
#endif

    Cli cli;
    if (parse_cli(argc, argv, &cli) < 0) return 1;

    /* --web is "emulator as a service": a self-contained multi-session
     * daemon, not just a headless flavor of the classic single-PCW loop.
     * Dispatch here before config_load() ever touches the host user's
     * real config — every session boots from clean defaults instead
     * (see websvc.c's session_create()). */
    if (cli.web)
        return websvc_run(cli.web_port ? cli.web_port : 1985);

    Config cfg;
    config_load(&cfg, cli.config_path);

    if (cli.memory_kb)   cfg.memory_kb = cli.memory_kb;
    if (cli.disk_a)      snprintf(cfg.drive_a, sizeof(cfg.drive_a), "%s", cli.disk_a);
    if (cli.disk_b)      snprintf(cfg.drive_b, sizeof(cfg.drive_b), "%s", cli.disk_b);
    /* cli.web already returned above via websvc_run(); reaching this
     * point means the classic single-PCW path — either fully
     * interactive, or headless-via-web_gui=true in the config file /
     * the F9 overlay toggle, neither of which forces --headless. */
    if (cli.headless) {
        /* Truly windowless: force the offscreen video and dummy audio
         * drivers with OVERRIDE priority so a session's DISPLAY /
         * WAYLAND_DISPLAY / SDL_VIDEODRIVER cannot bring a window up.
         * Must run before display_init (which calls SDL_Init). */
        SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "offscreen",
                                SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy",
                                SDL_HINT_OVERRIDE);
    }

    Display disp;
    if (display_init(&disp, &cfg) < 0) return 1;
    SDL_WindowID display_window_id = SDL_GetWindowID(disp.win);

    /* PCW carries the full 2 MB RAM inline; that would blow the default
     * 1 MB Windows main-thread stack before main() even runs. Static
     * storage parks it in BSS instead. (Linux mains get 8 MB, hence
     * Windows-only crash — see #55.) */
    static PCW pcw;
    pcw_init(&pcw, cfg.model, cfg.memory_kb);
    /* Apply the user's boot-ROM override (if any) and re-run reset so
     * the bootstrap stream reflects it. pcw_init already called
     * bootstrap_reset once with no override; re-running is cheap. */
    bootstrap_set_override_dir(&pcw.boot, cfg.boot_rom_dir);
    bootstrap_reset(&pcw.boot);
    notify_init();
    notify_set_mode(cfg.notifications);
    apply_runtime_config(&pcw, &cfg);

    if (cli.load_sna) snapshot_load(&pcw, cli.load_sna);
    if (cli.boot_ems && load_raw_image(&pcw, cli.boot_ems, 0) < 0) return 1;

    Overlay ov;
    overlay_init(&ov, &cfg, &pcw, &disp);

    /* F8 memory monitor / disassembler — lives in its own SDL window
     * created hidden; SDLK_F8 reveals it. Survives the lifetime of
     * the emulator. */
    Monitor *mon = monitor_create(&pcw);
    if (!mon) fprintf(stderr, "warning: monitor_create failed\n");

    /* SDL audio for the DK'tronics AY chip. Stream stays open for the
     * lifetime of the run; render_silence when ay isn't present. */
    SDL_AudioSpec ayspec = { SDL_AUDIO_S16, 1, AY_AUDIO_RATE };
    SDL_AudioStream *ay_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                  &ayspec, NULL, NULL);
    if (!ay_stream) {
        fprintf(stderr, "audio: SDL_OpenAudioDeviceStream failed: %s\n",
                SDL_GetError());
    } else if (!SDL_ResumeAudioStreamDevice(ay_stream)) {
        fprintf(stderr, "audio: SDL_ResumeAudioStreamDevice failed: %s\n",
                SDL_GetError());
    } else if (cfg.debug_traces) {
        fprintf(stderr, "audio: S16 mono %d Hz playback stream opened\n",
                AY_AUDIO_RATE);
    }

    /* PCW built-in beeper. pcw_init() already constructed the struct;
     * now that we know the audio sample rate, configure the phase step. */
    beeper_init(&pcw.beeper, AY_AUDIO_RATE);

    /* Camera-shutter SFX for F4 — same approach as 1984. Decode the
     * embedded WAV once, open a dedicated audio stream, and clear+push
     * the PCM on each screenshot trigger so it can replay overlapping
     * AY/beeper output. */
    SDL_AudioStream *sfx_stream = NULL;
    Uint8  *sfx_buf     = NULL;
    Uint32  sfx_buf_len = 0;
    {
        SDL_AudioSpec sfx_spec;
        SDL_IOStream *io = SDL_IOFromConstMem(shutter_wav, shutter_wav_len);
        if (io && SDL_LoadWAV_IO(io, true, &sfx_spec, &sfx_buf, &sfx_buf_len)) {
            sfx_stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sfx_spec, NULL, NULL);
            if (sfx_stream)
                SDL_ResumeAudioStreamDevice(sfx_stream);
        }
    }

    /* SDL gamepad for the DK'tronics joystick. Pick the first one that
     * shows up; hot-plug handled below via SDL_EVENT_GAMEPAD_ADDED. */
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    SDL_Gamepad *gamepad = NULL;
    bool mouse_captured = false;
    {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids && count > 0) gamepad = SDL_OpenGamepad(ids[0]);
        if (ids) SDL_free(ids);
    }

    Paste paste;
    paste_init(&paste);

    webgui_init(&pcw, &disp, &paste);
    webgui_set_log(false);   /* --web now goes through websvc_run(); this path is toast-only */
    if (cfg.web_gui)
        webgui_start(cfg.web_port);

    bool paste_pending = cli.paste_text && cli.paste_at > 0;
    if (cli.paste_text && !paste_pending) paste_text(&paste, cli.paste_text);
    bool auto_space_pending = cli.auto_space && cli.boot_ems;
    int  auto_space_frame = 120;
    bool auto_space_down = false;

    GifCap *gc = NULL;
    int gc_skip = 0;   /* halve to 25 fps for F6-triggered captures */
    if (cli.gif_out) {
        /* PCW framebuffer is 720x256 non-square pixels; stretch to
         * 720x540 for a 4:3 GIF, matching 1984's output. */
        gc = gifcap_open(cli.gif_out, DISPLAY_W, DISPLAY_H,
                         DISPLAY_W, DISPLAY_W * 3 / 4, 4);
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

    if (cfg.debug_traces) {
        printf("1985 — Amstrad PCW 8256 / 8512 / 9512 emulator (git %s)\n", PROG_GIT_COMMIT);
        printf("config: %s\n", cfg.path);
        printf("F9 = options, F11 = fullscreen, F12 = quit\n");
    }

    bool running = true;
    int  frame   = 0;
    Uint64 next_tick_ms = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (monitor_handle_event(mon, &ev)) continue;
            if (overlay_handle_event(&ov, &ev)) {
                if (mouse_captured
                    && (ov.visible || !mouse_input_enabled(&cfg)))
                    set_mouse_capture(&disp, &pcw.mouse,
                                      &mouse_captured, false);
                if (ov.visible)
                    leds_set_mouse_position(0.0f, 0.0f, false);
                continue;
            }
            leds_track_event(&disp, display_window_id, &ev, mouse_captured);
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    running = false; break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    if (ev.window.windowID == SDL_GetWindowID(disp.win))
                        set_mouse_capture(&disp, &pcw.mouse,
                                          &mouse_captured, false);
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (!gamepad) gamepad = SDL_OpenGamepad(ev.gdevice.which);
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (gamepad &&
                        SDL_GetGamepadID(gamepad) == ev.gdevice.which) {
                        SDL_CloseGamepad(gamepad);
                        gamepad = NULL;
                    }
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (mouse_captured
                        && (ev.key.mod & SDL_KMOD_CTRL)
                        && (ev.key.key == SDLK_RETURN
                            || ev.key.key == SDLK_KP_ENTER)) {
                        set_mouse_capture(&disp, &pcw.mouse,
                                          &mouse_captured, false);
                        kbd_release(&pcw.kbd, 10, 1);
                        break;
                    }
                    /* Shift+F1..Shift+F8 are PCW f1..f8 — let the matrix
                     * handler take them before any host shortcut fires. */
                    if (ev.key.scancode >= SDL_SCANCODE_F1
                        && ev.key.scancode <= SDL_SCANCODE_F8
                        && (ev.key.mod & SDL_KMOD_SHIFT)) {
                        kbd_handle(&pcw.kbd, &ev.key);
                        break;
                    }
                    /* Ctrl + / Ctrl − : step the window scale 1×..4×.
                     * EQUALS covers "=/+" regardless of shift; KP_PLUS /
                     * KP_MINUS cover the numeric keypad. No-op while
                     * fullscreen — there's nothing to resize. */
                    if ((ev.key.mod & SDL_KMOD_CTRL) && !disp.fullscreen) {
                        bool kp = (ev.key.scancode == SDL_SCANCODE_EQUALS
                                || ev.key.scancode == SDL_SCANCODE_KP_PLUS);
                        bool km = (ev.key.scancode == SDL_SCANCODE_MINUS
                                || ev.key.scancode == SDL_SCANCODE_KP_MINUS);
                        if (kp || km) {
                            int s = disp.scale + (kp ? 1 : -1);
                            if (s < 1) s = 1;
                            if (s > 4) s = 4;
                            if (s != disp.scale) {
                                disp.scale = s;
                                cfg.scale  = s;
                                SDL_SetWindowSize(disp.win,
                                                  DISPLAY_LOGICAL_W * s,
                                                  disp.logical_h * s);
                            }
                            break;
                        }
                    }
                    switch (ev.key.key) {
                        case SDLK_F4: {
                            char path[64];
                            snprintf(path, sizeof(path), "1985-%05d.ppm", frame);
                            display_save_ppm(&disp, path);
                            printf("screenshot: %s\n", path);
                            if (sfx_stream && sfx_buf) {
                                SDL_ClearAudioStream(sfx_stream);
                                SDL_PutAudioStreamData(sfx_stream, sfx_buf, (int)sfx_buf_len);
                            }
                            break;
                        }
                        case SDLK_F5: {
                            /* F5: full cold reset. Earlier this only called
                             * pcw_reset which left RAM contents untouched —
                             * stale BIOS / RSX (EMU TSR, etc.) state then
                             * leaked across "resets" and broke
                             * re-launches of the same program. Cold-boot
                             * gives the same behavior as quitting and
                             * relaunching the emulator.
                             *
                             * The raw host-side Serial backend (PTY fd or
                             * TCP listener) is preserved so an attached
                             * peer (e.g. tools/pty_modem.py) keeps its
                             * connection across the reboot. PerryFi is
                             * deliberately NOT preserved — its modem
                             * state (online vs command mode, dial-out
                             * TCP socket) is guest-visible and must look
                             * like a fresh modem to the rebooted CP/M,
                             * not a connection to whoever the guest had
                             * dialed before reset (#90). */
                            pcw.paused = false;
                            /* Flush in-memory disk writes before the
                             * cold boot discards the FDC's drive[]
                             * structs. apply_runtime_config will
                             * re-load the .dsk files afterwards. */
                            if (pcw.fdc.drive[0].dirty && cfg.drive_a[0])
                                disk_save(&pcw.fdc.drive[0], cfg.drive_a);
                            if (pcw.fdc.drive[1].dirty && cfg.drive_b[0])
                                disk_save(&pcw.fdc.drive[1], cfg.drive_b);
                            Serial saved_serial = pcw.serial;
                            perryfi_shutdown(&pcw.perryfi);
                            printer_shutdown(&pcw.printer);
                            pcw_cold_boot(&pcw, cfg.model, cfg.memory_kb);
                            bootstrap_set_override_dir(&pcw.boot, cfg.boot_rom_dir);
                            bootstrap_reset(&pcw.boot);
                            pcw.serial = saved_serial;
                            apply_runtime_config(&pcw, &cfg);
                            break;
                        }
                        case SDLK_F8: monitor_open(mon); break;
                        case SDLK_F6: {
                            /* Toggle GIF capture. Auto-name in CWD on start;
                             * finalise and report frame count on stop. */
                            if (gc) {
                                int n = gifcap_frame_count(gc);
                                gifcap_close(gc);
                                gc = NULL;
                                if (cfg.debug_traces)
                                    fprintf(stderr, "[videocap] GIF stopped (%d frames)\n", n);
                            } else {
                                char path[256];
                                time_t t = time(NULL);
                                struct tm *lt = localtime(&t);
                                if (lt)
                                    strftime(path, sizeof(path),
                                             "1985-%Y%m%d-%H%M%S.gif", lt);
                                else
                                    snprintf(path, sizeof(path), "1985-capture.gif");
                                /* 4 cs = 25 fps; halve frame rate to keep size manageable. */
                                gc = gifcap_open(path, DISPLAY_W, DISPLAY_H,
                                                 DISPLAY_W, DISPLAY_W * 3 / 4, 4);
                                if (!gc) {
                                    fprintf(stderr, "[videocap] GIF open failed for '%s'\n", path);
                                } else {
                                    gc_skip = 0;
                                    if (cfg.debug_traces)
                                        fprintf(stderr, "[videocap] recording to %s\n", path);
                                }
                            }
                            break;
                        }
                        case SDLK_F11: display_toggle_fullscreen(&disp); break;
                        case SDLK_F12: running = false; break;
                        case SDLK_V:
                            if (ev.key.mod & SDL_KMOD_CTRL) {
                                /* Ctrl+V: clipboard → keyboard injection.
                                 * Release the PCW's Ctrl key first so the
                                 * first injected char isn't seen as Ctrl+key
                                 * (would arrive as a control character to
                                 * CP/M instead of plain text). */
                                kbd_release(&pcw.kbd, 10, 1);   /* L/R Ctrl */
                                char *txt = SDL_GetClipboardText();
                                if (txt) {
                                    paste_text_raw(&paste, txt);
                                    SDL_free(txt);
                                }
                                break;
                            }
                            kbd_handle(&pcw.kbd, &ev.key);
                            break;
                        default:
                            kbd_handle(&pcw.kbd, &ev.key);
                            break;
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    kbd_handle(&pcw.kbd, &ev.key);
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (mouse_captured
                        && ev.motion.windowID == SDL_GetWindowID(disp.win))
                        pcwmouse_add_motion(&pcw.mouse,
                                            ev.motion.xrel, ev.motion.yrel);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (ev.button.windowID != SDL_GetWindowID(disp.win)
                        || !mouse_input_enabled(&cfg))
                        break;
                    if (!mouse_captured) {
                        set_mouse_capture(&disp, &pcw.mouse,
                                          &mouse_captured, true);
                        break;
                    }
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        pcwmouse_set_button(&pcw.mouse, 0, true);
                    else if (ev.button.button == SDL_BUTTON_MIDDLE)
                        pcwmouse_set_button(&pcw.mouse, 1, true);
                    else if (ev.button.button == SDL_BUTTON_RIGHT)
                        pcwmouse_set_button(&pcw.mouse, 2, true);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (!mouse_captured
                        || ev.button.windowID != SDL_GetWindowID(disp.win))
                        break;
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        pcwmouse_set_button(&pcw.mouse, 0, false);
                    else if (ev.button.button == SDL_BUTTON_MIDDLE)
                        pcwmouse_set_button(&pcw.mouse, 1, false);
                    else if (ev.button.button == SDL_BUTTON_RIGHT)
                        pcwmouse_set_button(&pcw.mouse, 2, false);
                    break;
                default: break;
            }
        }

        if (mouse_captured
            && (ov.visible || !mouse_input_enabled(&cfg)))
            set_mouse_capture(&disp, &pcw.mouse, &mouse_captured, false);

        if (paste_pending && frame >= cli.paste_at) {
            paste_text(&paste, cli.paste_text);
            paste_pending = false;
        }
        for (int i = 0; i < cli.paste_event_count; i++) {
            if (cli.paste_event[i].frame == frame)
                paste_text(&paste, cli.paste_event[i].text);
        }
        for (int i = 0; i < cli.disk_event_count; i++) {
            if (cli.disk_event[i].frame != frame) continue;
            int   drive = cli.disk_event[i].drive;
            char *slot  = drive == 0 ? cfg.drive_a : cfg.drive_b;
            size_t ssz  = drive == 0 ? sizeof(cfg.drive_a)
                                     : sizeof(cfg.drive_b);
            Disk *d = &pcw.fdc.drive[drive];
            /* Same contract as the overlay Media tab: flush pending
             * writes to the outgoing image before it goes away. */
            if (d->dirty && slot[0]) disk_save(d, slot);
            if (cli.disk_event[i].path[0]) {
                snprintf(slot, ssz, "%s", cli.disk_event[i].path);
                disk_load(d, slot);
                printf("disk-event: drive %c <- %s\n",
                       'A' + drive, slot);
            } else {
                slot[0] = 0;
                disk_eject(d);
                printf("disk-event: drive %c ejected\n", 'A' + drive);
            }
        }
        paste_tick(&paste, &pcw.kbd);
        webgui_poll();

        if (auto_space_pending && frame == auto_space_frame) {
            kbd_press(&pcw.kbd, 5, 7);
            auto_space_down = true;
        }
        if (auto_space_down && frame == auto_space_frame + 4) {
            kbd_release(&pcw.kbd, 5, 7);
            auto_space_down = false;
            auto_space_pending = false;
        }
        overlay_tick(&ov);
        notify_tick(16);
        /* Live-propagate trace flags from cfg in case the overlay toggled them. */
        pcw.turbo        = cfg.turbo;
        pcw.debug_traces = cfg.debug_traces;
        pcw.trace_io  = cfg.debug_traces && cfg.trace_io;
        pcw.fdc.trace = cfg.debug_traces && cfg.trace_fdc;
        bool was_paused   = pcw.paused;
        bool was_stepping = pcw.step_once;

        /* Poll the first host gamepad and expose it through whichever
         * PCW joystick port the user selected. DKsound rides on the
         * AY's port-A (only meaningful when DK'sound is enabled);
         * Kempston (0x9F) and Cascade (0xE0) are stand-alone latches
         * served by pcw.c regardless of the AY's presence. Mouse mode
         * leaves everything idle. */
        bool js_up = false, js_down = false, js_left = false, js_right = false;
        bool js_fire1 = false, js_fire2 = false;
        if (cfg.input_device == INPUT_DEVICE_JOYSTICK && gamepad) {
            js_up = SDL_GetGamepadButton(
                        gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)
                 || SDL_GetGamepadAxis(
                        gamepad, SDL_GAMEPAD_AXIS_LEFTY) < -16000;
            js_down = SDL_GetGamepadButton(
                          gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                   || SDL_GetGamepadAxis(
                          gamepad, SDL_GAMEPAD_AXIS_LEFTY) > 16000;
            js_left = SDL_GetGamepadButton(
                          gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                   || SDL_GetGamepadAxis(
                          gamepad, SDL_GAMEPAD_AXIS_LEFTX) < -16000;
            js_right = SDL_GetGamepadButton(
                           gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                    || SDL_GetGamepadAxis(
                           gamepad, SDL_GAMEPAD_AXIS_LEFTX) > 16000;
            js_fire1 = SDL_GetGamepadButton(
                           gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            js_fire2 = SDL_GetGamepadButton(
                           gamepad, SDL_GAMEPAD_BUTTON_EAST);
        }
        pcw.joystick.type  = cfg.joystick_type;
        pcw.joystick.up    = js_up;
        pcw.joystick.down  = js_down;
        pcw.joystick.left  = js_left;
        pcw.joystick.right = js_right;
        pcw.joystick.fire1 = js_fire1;
        pcw.joystick.fire2 = js_fire2;
        if (pcw.ay.present) {
            u8 js = (cfg.joystick_type == JOYSTICK_TYPE_DKSOUND)
                  ? aysound_pack_dksound(js_up, js_down, js_left, js_right,
                                         js_fire1, js_fire2)
                  : 0xFF;
            aysound_set_joystick(&pcw.ay, js);
        }

        cpc_frame_count = frame;
        pcw_frame(&pcw);
        printer_tick(&pcw.printer);

        /* Render one frame of AY audio (clears to silence when AY is
         * absent — keeps the SDL stream pacing steady). */
        if (ay_stream) {
            s16 abuf[AY_SAMPLES_FRAME];
            aysound_render(&pcw.ay, abuf, AY_SAMPLES_FRAME,
                           AY_CLOCK_HZ, AY_AUDIO_RATE);
            /* Mix the PCW built-in beeper on top of the AY output —
             * the BEL (^G) tone, boot-ROM error blips, and any game
             * that toggles F8 cmd 0x0B/0x0C all come through here. */
            beeper_render(&pcw.beeper, abuf, AY_SAMPLES_FRAME);
            SDL_PutAudioStreamData(ay_stream, abuf, sizeof(abuf));
        }
        /* Auto-open the monitor on a breakpoint hit and refresh the
         * disasm pane after a single-step. */
        if (!was_paused && pcw.paused) {
            monitor_open(mon);
            monitor_notify_break(mon);
        } else if (was_stepping && pcw.paused) {
            monitor_notify_step(mon);
        }
        roller_render(&pcw.mem, &pcw.asic, &disp);
        display_draw_framebuffer(&disp);

        /* Bottom status strip: model name in red + F-key hints in
         * grey. Drawn in WINDOW pixels (logical presentation disabled
         * for this block) so the SDL debug font stays at its native
         * 8x8 size like in 1984, instead of being scaled up by the
         * renderer's logical->window mapping. The strip sits just
         * above the LED bar area. */
        {
            const char *model_str =
                (cfg.model == PCW_MODEL_8512) ? "PCW 8512"
              : (cfg.model == PCW_MODEL_9512) ? "PCW 9512"
              : "PCW 8256";
            const char *keys = mouse_captured
                ? "  Mouse captured  Ctrl+Enter=release"
                : "  F4=screenshot  F5=reset  F6=capture  F8=monitor  "
                  "F9=options  F11=fullscreen  F12=quit";

            SDL_SetRenderLogicalPresentation(disp.renderer, 0, 0,
                                             SDL_LOGICAL_PRESENTATION_DISABLED);
            int ww, wh;
            SDL_GetWindowSize(disp.win, &ww, &wh);
            /* The strip owns the DISPLAY_STRIP_H logical band between
             * the PCW image and the LED bar — it must not overdraw the
             * image, since CP/M's status line uses the bottom scanlines
             * (#143). Compute the band's physical extent under
             * letterbox scaling (the smaller of the two axis scales,
             * content centred vertically); the 8x8 debug font stays
             * native-size, centred in the band. */
            float scale_x = (float)ww / (float)DISPLAY_LOGICAL_W;
            float scale_y = (float)wh / (float)disp.logical_h;
            float scale   = scale_x < scale_y ? scale_x : scale_y;
            float scaled_h = (float)disp.logical_h * scale;
            float letterbox_top = ((float)wh - scaled_h) * 0.5f;
            float strip_y = letterbox_top + (float)disp.screen_h * scale;
            float strip_h = (float)DISPLAY_STRIP_H * scale;
            float text_y  = strip_y + (strip_h - 8.0f) * 0.5f;
            SDL_FRect strip = { 0.0f, strip_y, (float)ww, strip_h };
            SDL_SetRenderDrawBlendMode(disp.renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(disp.renderer, 0x10, 0x10, 0x14, 255);
            SDL_RenderFillRect(disp.renderer, &strip);

            float text_w = (float)(strlen(model_str) + strlen(keys)) * 8.0f;
            float model_x = ((float)ww - text_w) * 0.5f;
            if (model_x < 0.0f) model_x = 0.0f;
            float keys_x  = model_x + (float)strlen(model_str) * 8.0f;

            SDL_SetRenderDrawColor(disp.renderer, 0xFF, 0x40, 0x40, 255);
            SDL_RenderDebugText(disp.renderer, model_x,        text_y, model_str);
            SDL_RenderDebugText(disp.renderer, model_x + 1.0f, text_y, model_str);
            SDL_SetRenderDrawColor(disp.renderer, 0xE0, 0xE0, 0xE0, 255);
            SDL_RenderDebugText(disp.renderer, keys_x, text_y, keys);

            SDL_SetRenderLogicalPresentation(disp.renderer,
                                             DISPLAY_LOGICAL_W, disp.logical_h,
                                             SDL_LOGICAL_PRESENTATION_LETTERBOX);
        }

        /* Debug-mode FPS overlay (bottom-left, above the status strip). */
        if (cfg.debug) {
            static Uint64 last_ns = 0;
            static int    samples = 0;
            static float  fps_smooth = 0.0f;
            Uint64 now = SDL_GetTicksNS();
            if (last_ns) {
                float dt_s = (now - last_ns) / 1.0e9f;
                if (dt_s > 0.0f) {
                    float fps_inst = 1.0f / dt_s;
                    if (samples == 0) fps_smooth = fps_inst;
                    else              fps_smooth = fps_smooth * 0.95f + fps_inst * 0.05f;
                    samples++;
                }
            }
            last_ns = now;
            char buf[64];
            snprintf(buf, sizeof(buf), "DBG  %.1f fps", (double)fps_smooth);
            /* Sit just above the status strip band — i.e. at the
             * bottom of the PCW image — in logical coords (renderer
             * uses LogicalPresentation). */
            const float sh = (float)disp.screen_h;
            SDL_SetRenderDrawColor(disp.renderer, 0, 0, 0, 255);
            SDL_RenderDebugText(disp.renderer, 7.0f, sh - 12.0f, buf);
            SDL_SetRenderDrawColor(disp.renderer, 0xFF, 0xC0, 0x40, 255);
            SDL_RenderDebugText(disp.renderer, 6.0f, sh - 13.0f, buf);
        }

        overlay_render(&ov, disp.renderer);
        /* Anchor toasts to the bottom of the PCW image, not the full
         * logical area — the strip + LED bands below it are chrome. */
        notify_render(disp.renderer, DISPLAY_LOGICAL_W, disp.screen_h);
        {
            int ww, wh;
            SDL_GetWindowSize(disp.win, &ww, &wh);
            leds_render_hover(disp.renderer, ww, wh);
        }

        if (gc) {
            if ((gc_skip ^= 1) == 0)
                gifcap_frame(gc, disp.fb);
        }
        webgui_frame();

        if (frame == screenshot_frame) {
            display_save_ppm(&disp, screenshot_path);
            printf("screenshot: %s\n", screenshot_path);
            running = false;
        }
        if (frame == save_sna_frame) snapshot_save(&pcw, save_sna_path);
        if (cli.dump_at >= 0 && frame == cli.dump_at) dump_state(&pcw, frame);
        if (cli.exit_after >= 0 && frame >= cli.exit_after) running = false;

        display_present(&disp);
        monitor_render(mon);
        monitor_pty_tick(mon);

        if (!cli.unthrottled) {
            /* Frame pacing — aim for 50 Hz. */
            next_tick_ms += 20;
            Uint64 now = SDL_GetTicks();
            if (now < next_tick_ms) SDL_Delay((Uint32)(next_tick_ms - now));
            else                    next_tick_ms = now;
        }
        frame++;
    }

    /* Flush any in-memory disk writes (WRITE DATA / FORMAT TRACK)
     * back to the host .dsk files before exit. dirty=false makes
     * this a no-op for clean disks. */
    if (pcw.fdc.drive[0].dirty && cfg.drive_a[0])
        disk_save(&pcw.fdc.drive[0], cfg.drive_a);
    if (pcw.fdc.drive[1].dirty && cfg.drive_b[0])
        disk_save(&pcw.fdc.drive[1], cfg.drive_b);

    if (gc) gifcap_close(gc);
    webgui_stop();
    set_mouse_capture(&disp, &pcw.mouse, &mouse_captured, false);
    monitor_destroy(mon);
    if (gamepad)   SDL_CloseGamepad(gamepad);
    if (ay_stream)  SDL_DestroyAudioStream(ay_stream);
    if (sfx_stream) SDL_DestroyAudioStream(sfx_stream);
    if (sfx_buf)    SDL_free(sfx_buf);
    paste_free(&paste);
    printer_shutdown(&pcw.printer);
    display_quit(&disp);
    return 0;
}
