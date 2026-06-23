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

/* PCW DK'tronics AY clock — same 1 MHz the CPC sibling uses for its
 * AY-3-8912. Real DK'tronics divides off the PCW system clock; 1 MHz
 * is the conventional rate for the audible bandwidth modelled here. */
#define AY_AUDIO_RATE         44100
#define AY_SAMPLES_FRAME      (AY_AUDIO_RATE / 50)
#define AY_CLOCK_HZ           1000000

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
    const char *boot_ems;
    const char *load_sna;
    const char *save_sna_arg;     /* "N:PATH" */
    const char *screenshot_arg;   /* "N:PATH" */
    const char *gif_out;
    bool        auto_space;
    int         memory_kb;        /* 0 = leave config alone */
    int         exit_after;       /* -1 = run forever */
    int         dump_at;          /* -1 = no dump */
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
    fprintf(stderr, "fdc  phase=%d irq=%d tc=%d motor=%d cur_cyl=[%d %d]\n",
            pcw->fdc.phase, pcw->fdc.irq, pcw->fdc.tc, pcw->fdc.motor_on,
            pcw->fdc.cur_cyl[0], pcw->fdc.cur_cyl[1]);

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
        if ((v = eq_value(a, "--load-sna"))    && *v) { cli->load_sna    = v; continue; }
        if ((v = eq_value(a, "--save-sna-at")) && *v) { cli->save_sna_arg = v; continue; }
        if ((v = eq_value(a, "--screenshot-at")) && *v) { cli->screenshot_arg = v; continue; }
        if ((v = eq_value(a, "--gif-out"))     && *v) { cli->gif_out     = v; continue; }
        if ((v = eq_value(a, "--exit-after"))  && *v) { cli->exit_after  = atoi(v); continue; }
        if ((v = eq_value(a, "--dump-at"))     && *v) { cli->dump_at     = atoi(v); continue; }
        if (strcmp(a, "--auto-space") == 0) { cli->auto_space = true; continue; }

        /* Two-token form: --flag VALUE */
        if (strcmp(a, "--config")        == 0 && i+1 < argc) { cli->config_path = argv[++i]; continue; }
        if (strcmp(a, "--memory")        == 0 && i+1 < argc) { cli->memory_kb   = atoi(argv[++i]); continue; }
        if (strcmp(a, "--disk-a")        == 0 && i+1 < argc) { cli->disk_a      = argv[++i]; continue; }
        if (strcmp(a, "--disk-b")        == 0 && i+1 < argc) { cli->disk_b      = argv[++i]; continue; }
        if (strcmp(a, "--boot-ems")      == 0 && i+1 < argc) { cli->boot_ems    = argv[++i]; continue; }
        if (strcmp(a, "--paste")         == 0 && i+1 < argc) { cli->paste_text  = argv[++i]; continue; }
        if (strcmp(a, "--load-sna")      == 0 && i+1 < argc) { cli->load_sna    = argv[++i]; continue; }
        if (strcmp(a, "--save-sna-at")   == 0 && i+1 < argc) { cli->save_sna_arg = argv[++i]; continue; }
        if (strcmp(a, "--screenshot-at") == 0 && i+1 < argc) { cli->screenshot_arg = argv[++i]; continue; }
        if (strcmp(a, "--gif-out")       == 0 && i+1 < argc) { cli->gif_out     = argv[++i]; continue; }
        if (strcmp(a, "--exit-after")    == 0 && i+1 < argc) { cli->exit_after  = atoi(argv[++i]); continue; }
        if (strcmp(a, "--dump-at")       == 0 && i+1 < argc) { cli->dump_at     = atoi(argv[++i]); continue; }
        if (strcmp(a, "--auto-space")    == 0) { cli->auto_space = true; continue; }

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

    Config cfg;
    config_load(&cfg, cli.config_path);

    if (cli.memory_kb)   cfg.memory_kb = cli.memory_kb;
    if (cli.disk_a)      snprintf(cfg.drive_a, sizeof(cfg.drive_a), "%s", cli.disk_a);
    if (cli.disk_b)      snprintf(cfg.drive_b, sizeof(cfg.drive_b), "%s", cli.disk_b);

    Display disp;
    if (display_init(&disp, &cfg) < 0) return 1;

    /* PCW carries the full 2 MB RAM inline; that would blow the default
     * 1 MB Windows main-thread stack before main() even runs. Static
     * storage parks it in BSS instead. (Linux mains get 8 MB, hence
     * Windows-only crash — see #55.) */
    static PCW pcw;
    pcw_init(&pcw, cfg.model, cfg.memory_kb);
    printer_set_pdf_output_dir(&pcw.printer, cfg.ext_pdf_printer_dir);
    printer_set_pdf_enabled(&pcw.printer,
                            cfg.ext_pdf_printer && cfg.ext_pdf_printer_dir[0]);
    pcw.debug_traces = cfg.debug_traces;
    pcw.trace_io  = cfg.debug_traces && cfg.trace_io;
    pcw.fdc.trace = cfg.debug_traces && cfg.trace_fdc;

    if (cfg.drive_a[0]) disk_load(&pcw.fdc.drive[0], cfg.drive_a);
    if (cfg.drive_b[0]) disk_load(&pcw.fdc.drive[1], cfg.drive_b);

    /* Serial port — opens a PTY or TCP listener when ext_serial is on
     * AND the model / backplane state actually exposes it. The CPS8256
     * I/O port window (0xE0-0xE8) is hooked on the same gate; the
     * split-LED in the bar tracks live RX/TX traffic. */
    {
        bool serial_avail  = (cfg.model == PCW_MODEL_9512) || cfg.ext_sanpollo_backplane;
        bool serial_on     = cfg.ext_serial  && serial_avail;
        bool perryfi_on    = serial_on        && cfg.ext_perryfi;
        /* When PerryFi takes over the serial line, the raw pty/tcp
         * backend stays dark — the AT modem is what's "plugged in". */
        serial_init(&pcw.serial,
                    serial_on && !perryfi_on,
                    cfg.ext_serial_backend,
                    cfg.ext_serial_tcp_port);
        perryfi_init(&pcw.perryfi, perryfi_on);
        cps_set_present(&pcw.cps, serial_on);
        leds_set_enabled(LED_SERIAL, serial_on);

        bool dk_on = cfg.ext_sanpollo_backplane && cfg.ext_dktronics;
        aysound_init(&pcw.ay, dk_on);
    }

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

    /* SDL gamepad for the DK'tronics joystick. Pick the first one that
     * shows up; hot-plug handled below via SDL_EVENT_GAMEPAD_ADDED. */
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    SDL_Gamepad *gamepad = NULL;
    {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids && count > 0) gamepad = SDL_OpenGamepad(ids[0]);
        if (ids) SDL_free(ids);
    }

    leds_set_enabled(LED_FDC_A, true);
    leds_set_enabled(LED_FDC_B, true);
    leds_set_enabled(LED_PRINTER, true);

    Paste paste;
    paste_init(&paste);
    if (cli.paste_text) paste_text(&paste, cli.paste_text);
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
            if (overlay_handle_event(&ov, &ev)) continue;
            switch (ev.type) {
                case SDL_EVENT_QUIT:
                    running = false; break;
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
                                                  DISPLAY_LOGICAL_H * s);
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
                            break;
                        }
                        case SDLK_F5:
                            /* F5: warm reset; also clears the paused state so
                             * the monitor can be resumed without explicit "G". */
                            pcw.paused = false;
                            pcw_reset(&pcw);
                            break;
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
                default: break;
            }
        }

        paste_tick(&paste, &pcw.kbd);

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
        /* Live-propagate trace flags from cfg in case the overlay toggled them. */
        pcw.debug_traces = cfg.debug_traces;
        pcw.trace_io  = cfg.debug_traces && cfg.trace_io;
        pcw.fdc.trace = cfg.debug_traces && cfg.trace_fdc;
        bool was_paused   = pcw.paused;
        bool was_stepping = pcw.step_once;

        /* DK'tronics: poll the host gamepad and pack the AY's Port A
         * byte (Atari-style, active-low). Reads from the AY when the
         * guest selects R14 latch the current snapshot. */
        if (pcw.ay.present && gamepad) {
            u8 js = 0xFF;
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)    ||
                SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) < -16000)
                js &= ~(1 << 0);
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)  ||
                SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) >  16000)
                js &= ~(1 << 1);
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)  ||
                SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) < -16000)
                js &= ~(1 << 2);
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) >  16000)
                js &= ~(1 << 3);
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH))    js &= ~(1 << 4);
            if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST))     js &= ~(1 << 5);
            aysound_set_joystick(&pcw.ay, js);
        }

        pcw_frame(&pcw);
        printer_tick(&pcw.printer);

        /* Render one frame of AY audio (clears to silence when AY is
         * absent — keeps the SDL stream pacing steady). */
        if (ay_stream) {
            s16 abuf[AY_SAMPLES_FRAME];
            aysound_render(&pcw.ay, abuf, AY_SAMPLES_FRAME,
                           AY_CLOCK_HZ, AY_AUDIO_RATE);
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
            const char *keys =
                "  F4=screenshot  F5=reset  F6=capture  F8=monitor  "
                "F9=options  F11=fullscreen  F12=quit";

            SDL_SetRenderLogicalPresentation(disp.renderer, 0, 0,
                                             SDL_LOGICAL_PRESENTATION_DISABLED);
            int ww, wh;
            SDL_GetWindowSize(disp.win, &ww, &wh);
            const float strip_h = 16.0f;
            const float led_bar = (float)DISPLAY_LED_BAR_H;
            float strip_y = (float)wh - led_bar - strip_h;
            float text_y  = strip_y + 4.0f;
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
                                             DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H,
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
            /* Sit just above the bottom status strip (16 px), in
             * logical coords (renderer uses LogicalPresentation). */
            const float lh = (float)DISPLAY_LOGICAL_H;
            const float strip_h = 16.0f;
            SDL_SetRenderDrawColor(disp.renderer, 0, 0, 0, 255);
            SDL_RenderDebugText(disp.renderer, 7.0f, lh - strip_h - 12.0f, buf);
            SDL_SetRenderDrawColor(disp.renderer, 0xFF, 0xC0, 0x40, 255);
            SDL_RenderDebugText(disp.renderer, 6.0f, lh - strip_h - 13.0f, buf);
        }

        overlay_render(&ov, disp.renderer);

        if (gc) {
            if ((gc_skip ^= 1) == 0)
                gifcap_frame(gc, disp.fb);
        }

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

        /* Frame pacing — aim for 50 Hz. */
        next_tick_ms += 20;
        Uint64 now = SDL_GetTicks();
        if (now < next_tick_ms) SDL_Delay((Uint32)(next_tick_ms - now));
        else                    next_tick_ms = now;
        frame++;
    }

    if (gc) gifcap_close(gc);
    monitor_destroy(mon);
    if (gamepad)   SDL_CloseGamepad(gamepad);
    if (ay_stream) SDL_DestroyAudioStream(ay_stream);
    paste_free(&paste);
    printer_shutdown(&pcw.printer);
    display_quit(&disp);
    return 0;
}
