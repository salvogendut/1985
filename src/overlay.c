#include "overlay.h"
#include "display.h"
#include "pcw.h"
#include "disk.h"
#include "snapshot.h"
#include "leds.h"
#include "notify.h"
#include <stdio.h>
#include <string.h>

/*
 * Minimal F9 overlay for the PCW scaffold. Four tabs, no inline
 * editor, file picker via SDL3's native dialog. Matches 1984's
 * shape so the same patterns extend later.
 */

#define LINE_H   16
#define ORIGIN_X 24
#define ORIGIN_Y 28
#define VAL_X    144

/* Semantic identity of a row in the Extensions section. The display
 * order shifts when an 8256-only row is hidden, so we route navigation
 * by kind, not by displayed index. */
typedef enum {
    EXT_NONE = 0,
    EXT_SERIAL,
    EXT_PERRYFI,
    EXT_DKTRONICS,
    EXT_MULTILINK,
    EXT_INPUT_DEVICE,
} ExtRow;

static ExtRow ext_row_at(const Config *cfg, int row) {
    (void)cfg;
    int r = 0;
    if (row == r++) return EXT_SERIAL;
    if (row == r++) return EXT_PERRYFI;
    if (row == r++) return EXT_DKTRONICS;
    if (row == r++) return EXT_MULTILINK;
    if (row == r++) return EXT_INPUT_DEVICE;
    return EXT_NONE;
}

typedef enum {
    TINK_SMOOTHING = 0,
    TINK_TINT,
    TINK_TINT_MODE,
    TINK_VIDEO_MODE,
    TINK_REGION,
    TINK_MOUSE_TYPE,
    TINK_JOYSTICK_TYPE,
    TINK_PRINTER_MODE,
    TINK_PRINTER_MODEL,
    TINK_NOTIFICATIONS,
    TINK_DEBUG,
    TINK_DEBUG_OUTPUT,
    TINK_TRACE_IO,
    TINK_TRACE_FDC,
    TINK_TRACE_INPUT,
    TINK_SERIAL_MODE,
    TINK_SERIAL_PATH,
    TINK_KEYBOARD_LAYOUT,
    TINK_SAVE_SNAPSHOT,
    TINK_LOAD_SNAPSHOT,
    TINK_BOOT_ROM,
    TINK_VERSION,
    TINK_ROW_COUNT,
} TinkerRow;

/* General-tab row identity. Same shape as ExtRow: the displayed row
 * index shifts when an 8256-only row is hidden, so navigation routes
 * by kind. */
typedef enum {
    GEN_NONE = 0,
    GEN_MODEL,
    GEN_MEMORY,
    GEN_SECOND_DRIVE,    /* 8256 only — 8512/9512 have two drives stock */
    GEN_PRINTER,         /* PDF capture of the built-in printer port —
                          * every PCW model has the FCh/FDh / Centronics
                          * hardware, so the toggle isn't gated on the
                          * backplane (#79) */
    GEN_BACKPLANE,
    GEN_TINKER,
} GenRow;

static GenRow gen_row_at(const Config *cfg, int row) {
    int r = 0;
    if (row == r++) return GEN_MODEL;
    if (row == r++) return GEN_MEMORY;
    if (cfg->model == PCW_MODEL_8256) {
        if (row == r++) return GEN_SECOND_DRIVE;
    }
    if (row == r++) return GEN_PRINTER;
    if (row == r++) return GEN_BACKPLANE;
    if (row == r++) return GEN_TINKER;
    return GEN_NONE;
}

/* The serial port is built into the 9512; on the 8256/8512 it only
 * appears once the SanPollo backplane is plugged in. */
static bool ext_serial_available(const Config *cfg) {
    return cfg->model == PCW_MODEL_9512 || cfg->ext_sanpollo_backplane;
}

static void apply_pdf_printer(Overlay *ov) {
    if (!ov->pcw) return;
    printer_set_pdf_output_dir(&ov->pcw->printer, ov->cfg->ext_pdf_printer_dir);
    printer_set_pdf_enabled(&ov->pcw->printer,
                            ov->cfg->ext_pdf_printer
                            && ov->cfg->ext_pdf_printer_dir[0]);
    printer_set_sink(&ov->pcw->printer, ov->cfg->ext_print_sink);
    printer_set_kind(&ov->pcw->printer,
                     ov->cfg->model == PCW_MODEL_9512 ? PRINTER_KIND_DAISYWHEEL
                                                      : PRINTER_KIND_DOT_MATRIX);
}

static void apply_input_device(Overlay *ov) {
    if (!ov->pcw) return;
    bool board_on = ov->cfg->ext_sanpollo_backplane
                 && ov->cfg->ext_dktronics;
    bool mouse_on = board_on
                 && ov->cfg->input_device == INPUT_DEVICE_MOUSE;
    pcwmouse_configure(&ov->pcw->mouse, mouse_on, ov->cfg->mouse_type);
    if (mouse_on)
        aysound_set_joystick(&ov->pcw->ay, 0xFF);
}

static const char *section_title(OvSection s) {
    switch (s) {
        case OV_GENERAL:    return "General";
        case OV_MEDIA:      return "Media";
        case OV_EXTENSIONS: return "Extensions";
        case OV_TINKER:     return "Advanced";
        default:            return "?";
    }
}

static int row_count(const Overlay *ov, OvSection s) {
    switch (s) {
        case OV_GENERAL:
            /* model, memory, [second drive on 8256], printer, backplane, tinker */
            return ov->cfg->model == PCW_MODEL_8256 ? 6 : 5;
        case OV_MEDIA:
            /* 8256 shipped with a single floppy; 8512/9512 had two.
             * Users can bolt a second drive onto an 8256 via the
             * Extensions tab — when enabled, drive B reappears here. */
            if (ov->cfg->model == PCW_MODEL_8256)
                return ov->cfg->ext_second_drive ? 2 : 1;
            return 2;
        case OV_EXTENSIONS: {
            /* The Extensions tab only exists when the PCW Backplane is
             * plugged in (toggled from General). Everything in this list
             * physically hangs off the backplane, so hiding the tab when
             * it's absent matches the real machine.
             *
             * Layout:
             *   Serial port
             *   PerryFi                 (AT-modem; lives on the serial line)
             *   DK'TRONICS Sound & Joystick
             *   Multilink               (probe-stub at 0xA6/0xA7)
             *   Input Device
             *
             * The Serial backend toggle (pty/tcp) lives under Advanced —
             * it's a developer convenience, not a hardware option.
             * Second drive and PDF printer moved to General — both are
             * stock-PCW features that don't need the backplane. */
            if (!ov->cfg->ext_sanpollo_backplane) return 0;
            return 5;
        }
        case OV_TINKER:     return ov->cfg->tinker ? TINK_ROW_COUNT : 0;
        default:            return 0;
    }
}

static const char *model_str(PcwModel m) {
    switch (m) {
        case PCW_MODEL_8512: return "PCW 8512";
        case PCW_MODEL_9512: return "PCW 9512";
        case PCW_MODEL_8256:
        default:             return "PCW 8256";
    }
}

static const char *mono_str(MonoMode m) {
    switch (m) {
        case MONO_GREEN: return "green";
        case MONO_AMBER: return "amber";
        case MONO_WHITE: return "white";
        case MONO_OFF:
        default:         return "off";
    }
}

static const char *video_str(VideoMode v) {
    switch (v) {
        case VIDEO_CGA1: return "CGA1 (BGRBrn)";
        case VIDEO_CGA2: return "CGA2 (BCMW)";
        case VIDEO_EGA:  return "EGA (16 col)";
        case VIDEO_PCW:
        default:         return "PCW (mono)";
    }
}

static const char *region_str(Region r) {
    return r == REGION_NTSC ? "NTSC (60 Hz, 200 lines)"
                            : "PAL (50 Hz, 256 lines)";
}

static const char *sink_str(PrintSink s) {
    return (s == PRINT_SINK_REAL) ? "Real printer" : "PDF file";
}

static const char *printer_hw_str(const Config *cfg) {
    if (cfg->model == PCW_MODEL_9512)
        return cfg->printer_centronics_9512 ? "Centronics" : "Daisywheel";
    return "Dot-matrix";
}

static const char *input_device_str(InputDevice input) {
    return input == INPUT_DEVICE_MOUSE ? "Mouse" : "Joystick";
}

static const char *mouse_type_str(MouseType type) {
    switch (type) {
        case MOUSE_TYPE_KEMPSTON: return "Kempston";
        case MOUSE_TYPE_KEYMOUSE: return "Keymouse";
        default:                  return "AMX";
    }
}

static const char *joystick_type_str(JoystickType type) {
    switch (type) {
        case JOYSTICK_TYPE_KEMPSTON:     return "Kempston";
        case JOYSTICK_TYPE_CASCADE:      return "Cascade";
        case JOYSTICK_TYPE_SPECTRAVIDEO: return "Spectravideo";
        case JOYSTICK_TYPE_KEYBOARD:     return "Keyboard";
        default:                         return "DKsound";
    }
}

static const char *bool_str(bool b) { return b ? "yes" : "no"; }

static void item_text(const Overlay *ov, int row, char *label, size_t lsz, char *val, size_t vsz) {
    const Config *cfg = ov->cfg;
    label[0] = 0; val[0] = 0;
    switch (ov->section) {
        case OV_GENERAL:
            switch (gen_row_at(cfg, row)) {
                case GEN_MODEL:        snprintf(label, lsz, "Model");         snprintf(val, vsz, "%s", model_str(cfg->model));                  break;
                case GEN_MEMORY:       snprintf(label, lsz, "RAM (KB)");      snprintf(val, vsz, "%d", cfg->memory_kb);                         break;
                case GEN_SECOND_DRIVE: snprintf(label, lsz, "Second drive");  snprintf(val, vsz, "%s", bool_str(cfg->ext_second_drive));        break;
                case GEN_PRINTER:
                    snprintf(label, lsz, "Printer");
                    if (!cfg->ext_pdf_printer)
                        snprintf(val, vsz, "no");
                    else if (cfg->ext_pdf_printer_dir[0])
                        snprintf(val, vsz, "yes: %s", cfg->ext_pdf_printer_dir);
                    else
                        snprintf(val, vsz, "yes: [choose folder]");
                    break;
                case GEN_BACKPLANE:    snprintf(label, lsz, "PCW Backplane"); snprintf(val, vsz, "%s", bool_str(cfg->ext_sanpollo_backplane));  break;
                case GEN_TINKER:       snprintf(label, lsz, "Tinker");        snprintf(val, vsz, "%s", bool_str(cfg->tinker));                  break;
                case GEN_NONE: default: break;
            }
            break;
        case OV_MEDIA:
            switch (row) {
                case 0: snprintf(label, lsz, "Drive A"); snprintf(val, vsz, "%s", cfg->drive_a[0] ? cfg->drive_a : "(empty — N=new CF2 blank)"); break;
                case 1: snprintf(label, lsz, "Drive B"); snprintf(val, vsz, "%s", cfg->drive_b[0] ? cfg->drive_b : "(empty — N=new CF2DD, Shift+N=CF2)"); break;
            }
            break;
        case OV_EXTENSIONS:
            switch (ext_row_at(cfg, row)) {
                case EXT_SERIAL:
                    snprintf(label, lsz, "Serial port");
                    snprintf(val, vsz, "%s", bool_str(cfg->ext_serial));
                    break;
                case EXT_PERRYFI:
                    snprintf(label, lsz, "PerryFi");
                    if (!cfg->ext_serial)
                        snprintf(val, vsz, "[needs Serial port]");
                    else
                        snprintf(val, vsz, "%s", bool_str(cfg->ext_perryfi));
                    break;
                case EXT_DKTRONICS:
                    /* User-facing label kept short so the value column
                     * doesn't overlap. Full name (DK'TRONICS Sound +
                     * Joystick) lives in code comments and the README. */
                    snprintf(label, lsz, "DK'sound");
                    snprintf(val, vsz, "%s", bool_str(cfg->ext_dktronics));
                    break;
                case EXT_MULTILINK:
                    snprintf(label, lsz, "Multilink");
                    snprintf(val, vsz, "%s", bool_str(cfg->ext_multilink));
                    break;
                case EXT_INPUT_DEVICE:
                    snprintf(label, lsz, "Input Device");
                    snprintf(val, vsz, "%s",
                             input_device_str(cfg->input_device));
                    break;
                default: break;
            }
            break;
        case OV_TINKER:
            switch (row) {
                case TINK_SMOOTHING: snprintf(label, lsz, "Smoothing"); snprintf(val, vsz, "%s", bool_str(cfg->fullscreen_smoothing)); break;
                case TINK_TINT: snprintf(label, lsz, "Tint"); snprintf(val, vsz, "%s", mono_str(cfg->monochrome)); break;
                case TINK_TINT_MODE: snprintf(label, lsz, "Tint mode"); snprintf(val, vsz, "%s", cfg->tint_glow ? "glow" : "normal"); break;
                case TINK_VIDEO_MODE: snprintf(label, lsz, "Video mode"); snprintf(val, vsz, "%s", video_str(cfg->video_mode)); break;
                case TINK_REGION: snprintf(label, lsz, "Region"); snprintf(val, vsz, "%s", region_str(cfg->region)); break;
                case TINK_MOUSE_TYPE: snprintf(label, lsz, "Mouse type"); snprintf(val, vsz, "%s", mouse_type_str(cfg->mouse_type)); break;
                case TINK_JOYSTICK_TYPE: snprintf(label, lsz, "Joystick type"); snprintf(val, vsz, "%s", joystick_type_str(cfg->joystick_type)); break;
                case TINK_PRINTER_MODE: snprintf(label, lsz, "Printer mode"); snprintf(val, vsz, "%s", sink_str(cfg->ext_print_sink)); break;
                case TINK_PRINTER_MODEL: snprintf(label, lsz, "Printer model"); snprintf(val, vsz, "%s", printer_hw_str(cfg)); break;
                case TINK_NOTIFICATIONS:
                    snprintf(label, lsz, "Notifications");
                    snprintf(val, vsz, "%s",
                             cfg->notifications == NOTIFY_MODE_SCREEN  ? "screen"  :
                             cfg->notifications == NOTIFY_MODE_CONSOLE ? "console" : "off");
                    break;
                case TINK_DEBUG: snprintf(label, lsz, "Debugging"); snprintf(val, vsz, "%s", bool_str(cfg->debug)); break;
                case TINK_DEBUG_OUTPUT: snprintf(label, lsz, "Debug output"); snprintf(val, vsz, "%s", bool_str(cfg->debug_traces)); break;
                case TINK_TRACE_IO: snprintf(label, lsz, "Trace IO"); snprintf(val, vsz, "%s", bool_str(cfg->trace_io)); break;
                case TINK_TRACE_FDC: snprintf(label, lsz, "Trace FDC"); snprintf(val, vsz, "%s", bool_str(cfg->trace_fdc)); break;
                case TINK_TRACE_INPUT: snprintf(label, lsz, "Trace Input"); snprintf(val, vsz, "%s", bool_str(cfg->trace_input)); break;
                case TINK_SERIAL_MODE:
                    snprintf(label, lsz, "Serial mode");
                    if (!ext_serial_available(cfg))
                        snprintf(val, vsz, "[needs PCW Backplane]");
                    else if (!cfg->ext_serial)
                        snprintf(val, vsz, "[Serial port disabled]");
                    else if (!strcmp(cfg->ext_serial_backend, "tcp"))
                        snprintf(val, vsz, "TCP:%d", cfg->ext_serial_tcp_port);
                    else if (ov->pcw && ov->pcw->serial.pty_slave[0])
                        snprintf(val, vsz, "PTY:%s", ov->pcw->serial.pty_slave);
                    else
                        snprintf(val, vsz, "PTY");
                    break;
                case TINK_SERIAL_PATH:
                    snprintf(label, lsz, "Serial PATH");
                    snprintf(val, vsz, "%s",
                             cfg->ext_serial_pty_link[0]
                                 ? cfg->ext_serial_pty_link
                                 : "/tmp/1985-serial");
                    break;
                case TINK_KEYBOARD_LAYOUT: snprintf(label, lsz, "Show keyboard layout"); snprintf(val, vsz, "..."); break;
                case TINK_SAVE_SNAPSHOT: snprintf(label, lsz, "Save snapshot"); snprintf(val, vsz, "..."); break;
                case TINK_LOAD_SNAPSHOT: snprintf(label, lsz, "Load snapshot"); snprintf(val, vsz, "..."); break;
                case TINK_BOOT_ROM:
                    snprintf(label, lsz, "Boot ROM");
                    snprintf(val, vsz, "%s",
                             (ov->pcw && ov->pcw->boot.source[0])
                                 ? ov->pcw->boot.source
                                 : "embedded");
                    break;
                case TINK_VERSION:
                    snprintf(label, lsz, "Version");
                    /* PROG_GIT_COMMIT comes from configure.ac via -D;
                     * falls back to "unknown" if the macro isn't set. */
#ifndef PROG_GIT_COMMIT
#define PROG_GIT_COMMIT "unknown"
#endif
                    snprintf(val, vsz, "1985 v" "0.4.2" " (git %s)", PROG_GIT_COMMIT);
                    break;
                case TINK_ROW_COUNT: break;
            }
            break;
        default: break;
    }
}

static void cycle_mono(MonoMode *m) {
    /* Cycle GREEN → AMBER → WHITE only. MONO_OFF stays a valid
     * config value (untinted white) but isn't reachable from the UI
     * — the "no tint at all" use case is covered by switching Video
     * mode away from PCW. */
    switch (*m) {
        case MONO_AMBER: *m = MONO_WHITE; break;
        case MONO_WHITE: *m = MONO_GREEN; break;
        case MONO_GREEN:
        case MONO_OFF:
        default:         *m = MONO_AMBER; break;
    }
}

static void cycle_video(VideoMode *v) {
    *v = (VideoMode)(((int)*v + 1) % 4);
}

/* Copy the dirname component of `path` into `dst` (up to cap-1 chars,
 * NUL-terminated). Drops the trailing filename + final slash. If `path`
 * has no slash, `dst` is left untouched. */
static void copy_dirname(char *dst, size_t cap, const char *path) {
    if (!path || !path[0] || cap == 0) return;
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return;
    size_t n = (size_t)(slash - path);
    if (n >= cap) n = cap - 1;
    memcpy(dst, path, n);
    dst[n] = '\0';
}

static void overlay_file_callback(void *userdata, const char * const *files,
                                  int filter) {
    (void)filter;
    Overlay *ov = userdata;
    if (files && files[0]) {
        snprintf(ov->dialog_path, sizeof(ov->dialog_path), "%s", files[0]);
        SDL_MemoryBarrierRelease();
        ov->dialog_ready = true;
    } else {
        ov->dialog_drive = -1;
        ov->dialog_kind  = DIALOG_NONE;
    }
}

/* SDL3 dialogs treat NULL `default_location` as "platform default", and
 * accept either a directory or a file as the seed. Empty string is NOT
 * the same as NULL — pass NULL when we don't have a remembered dir. */
static inline const char *seed_location(const char *s) {
    return (s && s[0]) ? s : NULL;
}

static void open_disk_dialog(Overlay *ov, int drive) {
    ov->dialog_kind  = DIALOG_DISK;
    ov->dialog_drive = drive;
    ov->dialog_ready = false;
    static const SDL_DialogFileFilter filters[] = {
        { "DSK images", "dsk;DSK" },
        { "All files",  "*"       },
    };
    SDL_ShowOpenFileDialog(overlay_file_callback, ov, NULL,
                           filters, 2, seed_location(ov->cfg->last_disk_dir),
                           false);
}

static void open_disk_new_dialog(Overlay *ov, int drive, DiskType type) {
    ov->dialog_kind      = DIALOG_DISK_NEW;
    ov->dialog_drive     = drive;
    ov->dialog_disk_type = type;
    ov->dialog_ready     = false;
    static const SDL_DialogFileFilter filters[] = {
        { "DSK images", "dsk;DSK" },
        { "All files",  "*"       },
    };
    SDL_ShowSaveFileDialog(overlay_file_callback, ov, NULL,
                           filters, 2, seed_location(ov->cfg->last_disk_dir));
}

static void open_snapshot_load_dialog(Overlay *ov) {
    ov->dialog_kind  = DIALOG_SNAPSHOT_LOAD;
    ov->dialog_drive = -1;
    ov->dialog_ready = false;
    static const SDL_DialogFileFilter filters[] = {
        { "1985 snapshots", "sna;SNA" },
        { "All files",      "*"       },
    };
    SDL_ShowOpenFileDialog(overlay_file_callback, ov, NULL,
                           filters, 2, seed_location(ov->cfg->last_snap_dir),
                           false);
}

static void open_snapshot_save_dialog(Overlay *ov) {
    ov->dialog_kind  = DIALOG_SNAPSHOT_SAVE;
    ov->dialog_drive = -1;
    ov->dialog_ready = false;
    static const SDL_DialogFileFilter filters[] = {
        { "1985 snapshots", "sna;SNA" },
        { "All files",      "*"       },
    };
    SDL_ShowSaveFileDialog(overlay_file_callback, ov, NULL,
                           filters, 2, seed_location(ov->cfg->last_snap_dir));
}

static void open_printer_dir_dialog(Overlay *ov) {
    ov->dialog_kind  = DIALOG_PRINT_DIR;
    ov->dialog_drive = -1;
    ov->dialog_ready = false;
    const char *where = ov->cfg->ext_pdf_printer_dir[0]
                      ? ov->cfg->ext_pdf_printer_dir
                      : NULL;
    SDL_ShowOpenFolderDialog(overlay_file_callback, ov, NULL, where, false);
}

static void open_boot_rom_dir_dialog(Overlay *ov) {
    ov->dialog_kind  = DIALOG_BOOT_ROM_DIR;
    ov->dialog_drive = -1;
    ov->dialog_ready = false;
    /* Seed: active override wins (most relevant — user is probably
     * adjusting it), then the remembered last-picked dir (survives a
     * Del-clear), then platform default. */
    const char *where = ov->cfg->boot_rom_dir[0]    ? ov->cfg->boot_rom_dir
                      : ov->cfg->last_boot_rom_dir[0] ? ov->cfg->last_boot_rom_dir
                      : NULL;
    SDL_ShowOpenFolderDialog(overlay_file_callback, ov, NULL, where, false);
}

static void eject_disk(Overlay *ov, int drive) {
    Config *c = ov->cfg;
    char *slot = (drive == 0) ? c->drive_a : c->drive_b;
    if (slot[0] == 0) return;
    /* Persist any pending in-memory writes back to the host file
     * before throwing the in-RAM image away. */
    Disk *d = &ov->pcw->fdc.drive[drive];
    if (d->dirty) disk_save(d, slot);
    slot[0] = 0;
    disk_eject(d);
    ov->dirty = true;
}

static void activate(Overlay *ov) {
    Config *c = ov->cfg;
    switch (ov->section) {
        case OV_GENERAL:
            switch (gen_row_at(c, ov->row)) {
                case GEN_MODEL:
                    c->model = (PcwModel)(((int)c->model + 1) % 3);
                    /* Snap RAM and monitor to the chosen model's stock
                     * configuration: 8256=256K+green, 8512=512K+green,
                     * 9512=512K+white. The user can still override RAM
                     * afterwards (e.g. an upgraded 8256 with 512K). */
                    if (c->model == PCW_MODEL_8256) {
                        c->memory_kb  = 256;
                        c->monochrome = MONO_GREEN;
                    } else if (c->model == PCW_MODEL_8512) {
                        c->memory_kb  = 512;
                        c->monochrome = MONO_GREEN;
                    } else { /* PCW_MODEL_9512 */
                        c->memory_kb  = 512;
                        c->monochrome = MONO_WHITE;
                    }
                    /* Live-preview the new tint so the user sees the
                     * model-appropriate phosphor immediately (#69),
                     * and refresh the printer kind so the 9512's
                     * daisywheel takes effect without waiting for the
                     * cold-boot at overlay close (#70). */
                    if (ov->disp) display_set_monochrome(ov->disp, c->monochrome);
                    apply_pdf_printer(ov);
                    leds_set_enabled(LED_FDC_B,
                                     c->model != PCW_MODEL_8256 || c->ext_second_drive);
                    /* GEN_SECOND_DRIVE only exists on 8256 — snap the
                     * cursor back to a valid row if it now points off
                     * the end. */
                    if (ov->row >= row_count(ov, OV_GENERAL))
                        ov->row = row_count(ov, OV_GENERAL) - 1;
                    ov->dirty = true;
                    break;
                case GEN_MEMORY:
                    c->memory_kb = (c->memory_kb == 256) ? 512 : (c->memory_kb == 512) ? 2048 : 256;
                    ov->dirty = true;
                    break;
                case GEN_SECOND_DRIVE:
                    c->ext_second_drive = !c->ext_second_drive;
                    leds_set_enabled(LED_FDC_B,
                                     c->model != PCW_MODEL_8256 || c->ext_second_drive);
                    ov->dirty = true;
                    break;
                case GEN_PRINTER:
                    if (c->ext_pdf_printer) {
                        c->ext_pdf_printer = false;
                        apply_pdf_printer(ov);
                        leds_set_enabled(LED_PRINTER, false);
                        ov->dirty = true;
                    } else {
                        open_printer_dir_dialog(ov);
                    }
                    break;
                case GEN_BACKPLANE:
                    c->ext_sanpollo_backplane = !c->ext_sanpollo_backplane;
                    /* Pulling the backplane disables only what physically
                     * hangs off it — serial / PerryFi / DK'tronics. The
                     * PDF printer lives in General now (#79) because the
                     * built-in printer port is on every PCW. */
                    if (!c->ext_sanpollo_backplane) {
                        c->ext_dktronics   = false;
                        c->ext_perryfi     = false;
                        c->ext_multilink   = false;
                        if (ov->pcw)
                            multilink_set_present(&ov->pcw->multilink, false);
                        if (c->model != PCW_MODEL_9512) {
                            c->ext_serial = false;
                            if (ov->pcw) {
                                serial_shutdown(&ov->pcw->serial);
                                cps_set_present(&ov->pcw->cps, false);
                            }
                            leds_set_enabled(LED_SERIAL, false);
                        }
                    }
                    apply_input_device(ov);
                    ov->dirty = true;
                    break;
                case GEN_TINKER:
                    c->tinker = !c->tinker;
                    ov->dirty = true;
                    break;
                case GEN_NONE: default: break;
            }
            break;
        case OV_MEDIA:
            open_disk_dialog(ov, ov->row);
            break;
        case OV_EXTENSIONS:
            switch (ext_row_at(c, ov->row)) {
                case EXT_SERIAL:
                    c->ext_serial = !c->ext_serial;
                    if (ov->pcw) {
                        serial_shutdown(&ov->pcw->serial);
                        serial_init(&ov->pcw->serial,
                                    c->ext_serial,
                                    c->ext_serial_backend,
                                    c->ext_serial_tcp_port,
                                    c->ext_serial_pty_link);
                        cps_set_present(&ov->pcw->cps, c->ext_serial);
                    }
                    leds_set_enabled(LED_SERIAL, c->ext_serial);
                    ov->dirty = true;
                    break;
                case EXT_PERRYFI:
                    if (c->ext_serial) {
                        c->ext_perryfi = !c->ext_perryfi;
                        ov->dirty = true;
                    }
                    break;
                case EXT_DKTRONICS:
                    c->ext_dktronics = !c->ext_dktronics;
                    apply_input_device(ov);
                    ov->dirty = true;
                    break;
                case EXT_MULTILINK:
                    c->ext_multilink = !c->ext_multilink;
                    if (ov->pcw)
                        multilink_set_present(&ov->pcw->multilink,
                                              c->ext_sanpollo_backplane
                                              && c->ext_multilink);
                    ov->dirty = true;
                    break;
                case EXT_INPUT_DEVICE:
                    c->input_device =
                        c->input_device == INPUT_DEVICE_MOUSE
                        ? INPUT_DEVICE_JOYSTICK : INPUT_DEVICE_MOUSE;
                    apply_input_device(ov);
                    ov->dirty = true;
                    break;
                case EXT_NONE:
                default:
                    break;
            }
            break;
        case OV_TINKER:
            switch (ov->row) {
                case TINK_SMOOTHING:
                    c->fullscreen_smoothing = !c->fullscreen_smoothing;
                    if (ov->disp)
                        display_set_smoothing(ov->disp, c->fullscreen_smoothing);
                    ov->dirty = true;
                    break;
                case TINK_TINT:
                    cycle_mono(&c->monochrome);
                    if (ov->disp) display_set_monochrome(ov->disp, c->monochrome);
                    ov->dirty = true;
                    break;
                case TINK_TINT_MODE:
                    c->tint_glow = !c->tint_glow;
                    if (ov->disp) display_set_tint_glow(ov->disp, c->tint_glow);
                    ov->dirty = true;
                    break;
                case TINK_VIDEO_MODE:
                    cycle_video(&c->video_mode);
                    if (ov->disp) display_set_video_mode(ov->disp, c->video_mode);
                    ov->dirty = true;
                    break;
                case TINK_REGION:
                    c->region = (c->region == REGION_PAL)
                              ? REGION_NTSC : REGION_PAL;
                    ov->dirty = true;
                    break;
                case TINK_MOUSE_TYPE:
                    c->mouse_type =
                        (MouseType)(((int)c->mouse_type + 1)
                                    % MOUSE_TYPE_COUNT);
                    apply_input_device(ov);
                    ov->dirty = true;
                    break;
                case TINK_JOYSTICK_TYPE:
                    c->joystick_type =
                        (JoystickType)(((int)c->joystick_type + 1)
                                       % JOYSTICK_TYPE_COUNT);
                    ov->dirty = true;
                    break;
                case TINK_PRINTER_MODE:
                    c->ext_print_sink = (c->ext_print_sink == PRINT_SINK_REAL)
                                      ? PRINT_SINK_PDF : PRINT_SINK_REAL;
                    apply_pdf_printer(ov);
                    ov->dirty = true;
                    break;
                case TINK_PRINTER_MODEL:
                    /* Toggle daisywheel ↔ Centronics — only meaningful on
                     * the 9512. On 8256/8512 the row reads "Dot-matrix"
                     * and the toggle is a no-op. */
                    if (c->model == PCW_MODEL_9512) {
                        c->printer_centronics_9512 = !c->printer_centronics_9512;
                        apply_pdf_printer(ov);
                        ov->dirty = true;
                    }
                    break;
                case TINK_NOTIFICATIONS:
                    c->notifications = (NotifyMode)((c->notifications + 1) % 3);
                    notify_set_mode(c->notifications);
                    ov->dirty = true;
                    break;
                case TINK_DEBUG: c->debug = !c->debug; ov->dirty = true; break;
                case TINK_DEBUG_OUTPUT: c->debug_traces = !c->debug_traces; ov->dirty = true; break;
                case TINK_TRACE_IO: c->trace_io = !c->trace_io; ov->dirty = true; break;
                case TINK_TRACE_FDC: c->trace_fdc = !c->trace_fdc; ov->dirty = true; break;
                case TINK_TRACE_INPUT: c->trace_input = !c->trace_input; ov->dirty = true; break;
                case TINK_SERIAL_MODE:
                    /* Serial mode — flip pty ↔ tcp and re-initialise the
                     * backend so the row value updates immediately. */
                    if (ext_serial_available(c) && c->ext_serial) {
                        if (!strcmp(c->ext_serial_backend, "tcp"))
                            snprintf(c->ext_serial_backend,
                                     sizeof(c->ext_serial_backend), "pty");
                        else
                            snprintf(c->ext_serial_backend,
                                     sizeof(c->ext_serial_backend), "tcp");
                        if (ov->pcw) {
                            serial_shutdown(&ov->pcw->serial);
                            serial_init(&ov->pcw->serial,
                                        c->ext_serial,
                                        c->ext_serial_backend,
                                        c->ext_serial_tcp_port,
                                        c->ext_serial_pty_link);
                        }
                        ov->dirty = true;
                    }
                    break;
                case TINK_SERIAL_PATH:
                    /* Serial PATH — open inline editor. Pre-fill with
                     * the current value (or default if empty). */
                    snprintf(ov->edit_buf, sizeof(ov->edit_buf), "%s",
                             c->ext_serial_pty_link[0]
                                 ? c->ext_serial_pty_link
                                 : "/tmp/1985-serial");
                    snprintf(ov->edit_orig, sizeof(ov->edit_orig),
                             "%s", ov->edit_buf);
                    ov->edit_target = 1;        /* ext_serial_pty_link */
                    ov->state = OV_STATE_EDIT;
                    if (ov->disp) SDL_StartTextInput(ov->disp->win);
                    break;
                case TINK_KEYBOARD_LAYOUT: ov->state = OV_STATE_KEYS; break;
                case TINK_SAVE_SNAPSHOT: open_snapshot_save_dialog(ov); break;
                case TINK_LOAD_SNAPSHOT: open_snapshot_load_dialog(ov); break;
                case TINK_BOOT_ROM: open_boot_rom_dir_dialog(ov); break;
                case TINK_VERSION:
                case TINK_ROW_COUNT:
                    break;
            }
            break;
        default: break;
    }
}

void overlay_init(Overlay *ov, Config *cfg, struct PCW *pcw, struct Display *disp) {
    memset(ov, 0, sizeof(*ov));
    ov->cfg  = cfg;
    ov->pcw  = pcw;
    ov->disp = disp;
}

static void close_overlay(Overlay *ov, bool save) {
    if (save && ov->dirty) {
        /* Spot model / RAM changes before persisting and trigger a
         * full power-cycle so the new hardware actually takes effect
         * (warm reset would leave stale paging, FDC and ASIC state). */
        bool need_cold_boot = (ov->cfg->model                != ov->saved.model)
                           || (ov->cfg->memory_kb            != ov->saved.memory_kb)
                           || (ov->cfg->ext_second_drive     != ov->saved.ext_second_drive)
                           || (ov->cfg->ext_sanpollo_backplane != ov->saved.ext_sanpollo_backplane)
                           || (ov->cfg->ext_serial           != ov->saved.ext_serial)
                           || (ov->cfg->ext_perryfi          != ov->saved.ext_perryfi)
                           || (ov->cfg->ext_dktronics        != ov->saved.ext_dktronics)
                           /* Mouse drivers latch the protocol at boot:
                            * AMX probes the 8255 once, Keymouse changes
                            * the keyboard scan window contract. Switching
                            * without a power cycle leaves stale driver
                            * state and the new device looks dead. */
                           || (ov->cfg->mouse_type           != ov->saved.mouse_type)
                           /* Flipping between Mouse and Joystick re-wires
                            * what the AY port-A / mouse-port handlers
                            * report. The guest only probes once at boot,
                            * so without a cold reset the new device is
                            * invisible to it. Same rationale for the
                            * joystick-type swap: each variant lives at
                            * a different port (DKsound 0xA9, Kempston
                            * 0x9F, Cascade/Spectravideo 0xE0) and games
                            * latch the choice on first poll. */
                           || (ov->cfg->input_device         != ov->saved.input_device)
                           || (ov->cfg->joystick_type        != ov->saved.joystick_type)
                           /* Region (PAL/NTSC) changes the frame
                            * cadence and visible-line count — both
                            * are sampled at frame-loop entry, so a
                            * warm reset would tear the next frame.
                            * Boot ROMs probe F8 bit 4 to decide the
                            * visible window too. */
                           || (ov->cfg->region               != ov->saved.region);
        config_save(ov->cfg);
        ov->dirty = false;
        if (need_cold_boot && ov->pcw) {
            /* Flush in-memory disk writes before pcw_cold_boot
             * discards the FDC drive arrays. */
            if (ov->pcw->fdc.drive[0].dirty && ov->cfg->drive_a[0])
                disk_save(&ov->pcw->fdc.drive[0], ov->cfg->drive_a);
            if (ov->pcw->fdc.drive[1].dirty && ov->cfg->drive_b[0])
                disk_save(&ov->pcw->fdc.drive[1], ov->cfg->drive_b);
            printer_shutdown(&ov->pcw->printer);
            pcw_cold_boot(ov->pcw, ov->cfg->model, ov->cfg->memory_kb);
            /* Re-apply the boot-ROM override on the freshly-reset
             * bootstrap so it sticks across cold-boots driven from
             * here too (otherwise the override only takes effect on
             * F5 / next launch). */
            bootstrap_set_override_dir(&ov->pcw->boot, ov->cfg->boot_rom_dir);
            bootstrap_reset(&ov->pcw->boot);
            /* Mirror region into the freshly-reset ASIC before the
             * first new frame runs, so cycles_per_frame / ticks-per-
             * frame are already correct on the first tick. Also
             * resize the SDL window so the visible image area shrinks
             * (NTSC) or grows (PAL) to match. */
            ov->pcw->asic.refresh_60hz = (ov->cfg->region == REGION_NTSC);
            if (ov->disp) display_set_region(ov->disp, ov->cfg->region);
            /* pcw_init zeroed the FDC — re-mount the disk images so
             * the machine boots from them just like at first launch. */
            if (ov->cfg->drive_a[0]) disk_load(&ov->pcw->fdc.drive[0], ov->cfg->drive_a);
            if (ov->cfg->drive_b[0]) disk_load(&ov->pcw->fdc.drive[1], ov->cfg->drive_b);
            bool savail = (ov->cfg->model == PCW_MODEL_9512) || ov->cfg->ext_sanpollo_backplane;
            bool s_on   = ov->cfg->ext_serial && savail;
            bool p_on   = s_on && ov->cfg->ext_perryfi;
            serial_init(&ov->pcw->serial, s_on && !p_on,
                        ov->cfg->ext_serial_backend,
                        ov->cfg->ext_serial_tcp_port,
                        ov->cfg->ext_serial_pty_link);
            perryfi_init(&ov->pcw->perryfi, p_on);
            cps_set_present(&ov->pcw->cps, s_on);
            leds_set_enabled(LED_SERIAL, s_on);

            bool dk_on = ov->cfg->ext_sanpollo_backplane && ov->cfg->ext_dktronics;
            aysound_init(&ov->pcw->ay, dk_on);
            pcwmouse_configure(&ov->pcw->mouse,
                               dk_on
                               && ov->cfg->input_device == INPUT_DEVICE_MOUSE,
                               ov->cfg->mouse_type);
            apply_pdf_printer(ov);
        }
    } else if (!save) {
        *ov->cfg = ov->saved;
        apply_pdf_printer(ov);
        apply_input_device(ov);
        /* Undo any live tint / glow / video-mode preview the user
         * cycled. Glow must come after monochrome — display_set_*
         * both write d->bg, glow needs to win. */
        if (ov->disp) {
            display_set_monochrome(ov->disp, ov->cfg->monochrome);
            display_set_tint_glow(ov->disp, ov->cfg->tint_glow);
            display_set_video_mode(ov->disp, ov->cfg->video_mode);
        }
    }
    ov->visible = false;
    ov->state   = OV_STATE_MENU;
}

bool overlay_handle_event(Overlay *ov, SDL_Event *ev) {
    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_F9) {
        if (!ov->visible) {
            ov->saved   = *ov->cfg;
            ov->visible = true;
            ov->section = OV_GENERAL;
            ov->row     = 0;
            ov->dirty   = false;
        } else {
            close_overlay(ov, true);
        }
        return true;
    }

    if (!ov->visible) return false;

    /* Never swallow window-close — the user must always be able to quit. */
    if (ev->type == SDL_EVENT_QUIT) return false;

    /* Inline text editor for Serial PATH (and any future text fields).
     * Eats raw TEXT_INPUT events for printable characters; key-down
     * handles control keys (Enter/Esc/Del/Backspace). */
    if (ov->state == OV_STATE_EDIT) {
        if (ev->type == SDL_EVENT_TEXT_INPUT) {
            size_t len = strlen(ov->edit_buf);
            const char *t = ev->text.text;
            while (*t && len + 1 < sizeof(ov->edit_buf)) {
                ov->edit_buf[len++] = *t++;
            }
            ov->edit_buf[len] = '\0';
            return true;
        }
        if (ev->type != SDL_EVENT_KEY_DOWN) return true;
        SDL_Keycode k = ev->key.key;
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            /* Commit: copy buffer into the target config field and
             * re-apply. Empty value falls back to the default. */
            const char *v = ov->edit_buf[0] ? ov->edit_buf : "/tmp/1985-serial";
            if (ov->edit_target == 1) {
                snprintf(ov->cfg->ext_serial_pty_link,
                         sizeof(ov->cfg->ext_serial_pty_link), "%s", v);
                if (ov->pcw) {
                    bool savail = (ov->cfg->model == PCW_MODEL_9512)
                               || ov->cfg->ext_sanpollo_backplane;
                    bool s_on   = ov->cfg->ext_serial && savail;
                    bool p_on   = s_on && ov->cfg->ext_perryfi;
                    serial_shutdown(&ov->pcw->serial);
                    serial_init(&ov->pcw->serial, s_on && !p_on,
                                ov->cfg->ext_serial_backend,
                                ov->cfg->ext_serial_tcp_port,
                                ov->cfg->ext_serial_pty_link);
                }
            }
            ov->dirty = true;
            if (ov->disp) SDL_StopTextInput(ov->disp->win);
            ov->state = OV_STATE_MENU;
        } else if (k == SDLK_ESCAPE) {
            /* If unchanged, just leave. Otherwise prompt save/discard. */
            if (strcmp(ov->edit_buf, ov->edit_orig) == 0) {
                if (ov->disp) SDL_StopTextInput(ov->disp->win);
                ov->state = OV_STATE_MENU;
            } else {
                ov->state = OV_STATE_EDIT_CONFIRM;
            }
        } else if (k == SDLK_DELETE) {
            /* Restore the default. */
            snprintf(ov->edit_buf, sizeof(ov->edit_buf), "%s",
                     "/tmp/1985-serial");
        } else if (k == SDLK_BACKSPACE) {
            size_t len = strlen(ov->edit_buf);
            if (len > 0) ov->edit_buf[len - 1] = '\0';
        }
        return true;
    }

    if (ov->state == OV_STATE_EDIT_CONFIRM) {
        if (ev->type != SDL_EVENT_KEY_DOWN) return true;
        SDL_Keycode k = ev->key.key;
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) {
            /* Save: commit edit_buf (same path as RETURN in EDIT). */
            const char *v = ov->edit_buf[0] ? ov->edit_buf : "/tmp/1985-serial";
            if (ov->edit_target == 1) {
                snprintf(ov->cfg->ext_serial_pty_link,
                         sizeof(ov->cfg->ext_serial_pty_link), "%s", v);
                if (ov->pcw) {
                    bool savail = (ov->cfg->model == PCW_MODEL_9512)
                               || ov->cfg->ext_sanpollo_backplane;
                    bool s_on   = ov->cfg->ext_serial && savail;
                    bool p_on   = s_on && ov->cfg->ext_perryfi;
                    serial_shutdown(&ov->pcw->serial);
                    serial_init(&ov->pcw->serial, s_on && !p_on,
                                ov->cfg->ext_serial_backend,
                                ov->cfg->ext_serial_tcp_port,
                                ov->cfg->ext_serial_pty_link);
                }
            }
            ov->dirty = true;
            if (ov->disp) SDL_StopTextInput(ov->disp->win);
            ov->state = OV_STATE_MENU;
        } else if (k == SDLK_N || k == SDLK_ESCAPE) {
            /* Discard. */
            if (ov->disp) SDL_StopTextInput(ov->disp->win);
            ov->state = OV_STATE_MENU;
        }
        return true;
    }

    if (ev->type != SDL_EVENT_KEY_DOWN) return true;

    if (ov->state == OV_STATE_KEYS) {
        /* Any key dismisses the help panel. */
        ov->state = OV_STATE_MENU;
        return true;
    }

    if (ov->state == OV_STATE_CONFIRM) {
        if (ev->key.key == SDLK_RETURN || ev->key.key == SDLK_KP_ENTER
            || ev->key.key == SDLK_Y) {
            close_overlay(ov, true);
        } else if (ev->key.key == SDLK_ESCAPE || ev->key.key == SDLK_N) {
            close_overlay(ov, false);
        }
        return true;
    }

    int rc = row_count(ov, ov->section);
    switch (ev->key.key) {
        case SDLK_ESCAPE:
            if (ov->dirty) ov->state = OV_STATE_CONFIRM;
            else close_overlay(ov, false);
            break;
        case SDLK_TAB:
        case SDLK_RIGHT:
            do {
                ov->section = (OvSection)((ov->section + 1) % OV_SEC_COUNT);
            } while (row_count(ov, ov->section) == 0);
            ov->row = 0;
            break;
        case SDLK_LEFT:
            do {
                ov->section = (OvSection)((ov->section + OV_SEC_COUNT - 1) % OV_SEC_COUNT);
            } while (row_count(ov, ov->section) == 0);
            ov->row = 0;
            break;
        case SDLK_UP:    if (rc) ov->row = (ov->row + rc - 1) % rc; break;
        case SDLK_DOWN:  if (rc) ov->row = (ov->row + 1) % rc;      break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            activate(ov);
            break;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
            if (ov->section == OV_MEDIA && (ov->row == 0 || ov->row == 1))
                eject_disk(ov, ov->row);
            else if (ov->section == OV_TINKER && ov->cfg->tinker
                  && ov->row == TINK_BOOT_ROM
                  && (ov->cfg->boot_rom_dir[0]
                      || ov->cfg->last_boot_rom_dir[0])) {
                /* Clear both the active override and the remembered
                 * last-picked dir so the next picker opens at the
                 * platform default. The Advanced row's source line
                 * reverts to the default search chain (or "embedded"). */
                ov->cfg->boot_rom_dir[0]      = '\0';
                ov->cfg->last_boot_rom_dir[0] = '\0';
                if (ov->pcw) {
                    bootstrap_set_override_dir(&ov->pcw->boot, NULL);
                    bootstrap_reset(&ov->pcw->boot);
                }
                ov->dirty = true;
            }
            break;
        case SDLK_N:
            if (ov->section == OV_MEDIA && (ov->row == 0 || ov->row == 1)) {
                /* Default format by drive slot: drive A = CF2 (180k SS),
                 * drive B = CF2DD (720k DS). Shift+N inverts the choice
                 * so the user can still create the other format. */
                bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                DiskType t = (ov->row == 1) ? DISK_TYPE_CF2DD : DISK_TYPE_CF2;
                if (shift) t = (t == DISK_TYPE_CF2) ? DISK_TYPE_CF2DD : DISK_TYPE_CF2;
                open_disk_new_dialog(ov, ov->row, t);
            }
            break;
    }
    return true;
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *s, u8 R, u8 G, u8 B) {
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_RenderDebugText(r, (float)x, (float)y, s);
}

static void fill_rect(SDL_Renderer *r, float x, float y, float w, float h,
                      u8 R, u8 G, u8 B, u8 A) {
    SDL_SetRenderDrawBlendMode(r, A < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect_outline(SDL_Renderer *r, float x, float y, float w, float h,
                              u8 R, u8 G, u8 B) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderRect(r, &rect);
}

void overlay_render(Overlay *ov, SDL_Renderer *r) {
    if (!ov->visible) return;

    int rc = row_count(ov, ov->section);
    int footer_y = ORIGIN_Y + rc * LINE_H + 6;
    int panel_h = footer_y + 22;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect bg = { 8, 8, 600, (float)panel_h };
    SDL_RenderFillRect(r, &bg);

    /* Tabs row. */
    int tx = ORIGIN_X;
    for (int s = 0; s < OV_SEC_COUNT; s++) {
        if (row_count(ov, (OvSection)s) == 0) continue;
        bool active = (ov->section == (OvSection)s);
        draw_text(r, tx, 12, section_title((OvSection)s),
                  active ? 255 : 180, active ? 255 : 180, active ? 0 : 180);
        tx += (int)strlen(section_title((OvSection)s)) * 8 + 16;
    }

    for (int i = 0; i < rc; i++) {
        char label[64], val[PATH_MAX + 64];
        item_text(ov, i, label, sizeof(label), val, sizeof(val));
        int y = ORIGIN_Y + i * LINE_H;
        bool sel = (i == ov->row);
        u8 R = sel ? 255 : 200, G = sel ? 255 : 200, B = sel ? 0 : 200;
        if (sel) draw_text(r, ORIGIN_X - 12, y, ">", R, G, B);
        draw_text(r, ORIGIN_X, y, label, R, G, B);
        draw_text(r, VAL_X,    y, val,   R, G, B);
    }

    draw_text(r, ORIGIN_X, footer_y,
              "\x1B \x1A: tabs   \x18 \x19: row   Enter: change   Esc: close",
              140, 140, 140);

    if (ov->section == OV_MEDIA) {
        draw_text(r, ORIGIN_X, ORIGIN_Y + 4 * LINE_H,
                  "Enter: select DSK   Del: eject", 160, 160, 160);
    }

    /* Keyboard help — opened from Advanced ▸ "Show keyboard layout".
     * Drawn on top of the dimmed window; any key returns to the menu. */
    if (ov->state == OV_STATE_KEYS) {
        static const struct { const char *pcw, *host; } rows[] = {
            { "f1 .. f8",          "Shift + F1 .. F8" },
            { "EXIT",              "Esc" },
            { "STOP",              "`" },
            { "EXTRA",             "Ctrl" },
            { "ALT",               "Alt" },
            { "COPY",              "Home" },
            { "CUT",               "Insert" },
            { "PASTE",             "PageUp" },
            { "DOC / PAGE",        "PageDown" },
            { "CAN",               "End  /  Keypad ." },
            { "DEL\x1A",           "Delete" },
            { "LINE / EOL",        "Keypad 7" },
            { "WORD / CHAR",       "Keypad 9" },
            { "FIND / EXCH",       "Keypad 1" },
            { "UNIT / PARA",       "Keypad 3" },
            { "RELAY",             "Keypad 0" },
            { "[+]   (Set)",       "Keypad +" },
            { "[-]   (Clear)",     "Keypad -" },
            { "PTR",               "PrintScreen  /  Keypad *" },
            { "Return",            "Enter  /  Keypad Enter" },
        };
        int n = (int)(sizeof(rows) / sizeof(rows[0]));

        int ww = DISPLAY_LOGICAL_W;
        int wh = ov->disp ? ov->disp->logical_h : DISPLAY_LOGICAL_H;
        const int FONT_H = 8;
        int box_w = 520;
        int box_h = FONT_H + 12 + n * (FONT_H + 4) + 20;
        float bx = (ww - box_w) / 2.0f;
        float by = (wh - box_h) / 2.0f;

        fill_rect(r, 0, 0, (float)ww, (float)wh, 0, 0, 0, 160);
        fill_rect(r, bx, by, (float)box_w, (float)box_h, 20, 20, 50, 255);
        draw_rect_outline(r, bx, by, (float)box_w, (float)box_h, 70, 90, 200);

        const char *title = "PCW key                 Host key";
        draw_text(r, (int)(bx + 16), (int)(by + 8), title, 255, 255, 100);

        int row_y = (int)(by + 8 + FONT_H + 8);
        for (int i = 0; i < n; i++) {
            draw_text(r, (int)(bx + 16),  row_y, rows[i].pcw,  220, 220, 220);
            draw_text(r, (int)(bx + 216), row_y, rows[i].host, 200, 200, 255);
            row_y += FONT_H + 4;
        }
        draw_text(r, (int)(bx + 16), (int)(by + box_h - FONT_H - 6),
                  "Press any key to return.", 140, 140, 140);
        return;
    }

    /* Inline text editor — a small modal that shows the current edit
     * buffer with a cursor and the available commands. */
    if (ov->state == OV_STATE_EDIT) {
        int ww = DISPLAY_LOGICAL_W;
        int wh = ov->disp ? ov->disp->logical_h : DISPLAY_LOGICAL_H;
        const int FONT_W = 8, FONT_H = 8;
        const char *title = "Serial PATH";
        const char *help  = "Enter=Save  Esc=Cancel  Del=Default";
        int max_w = (int)strlen(help) * FONT_W;
        int buf_w = ((int)strlen(ov->edit_buf) + 1) * FONT_W;
        if (buf_w > max_w) max_w = buf_w;
        int box_w = max_w + 24;
        int box_h = FONT_H * 4 + 32;
        float bx = (ww - box_w) / 2.0f;
        float by = (wh - box_h) / 2.0f;

        fill_rect(r, 0, 0, (float)ww, (float)wh, 0, 0, 0, 140);
        fill_rect(r, bx, by, (float)box_w, (float)box_h, 25, 25, 60, 255);
        draw_rect_outline(r, bx, by, (float)box_w, (float)box_h, 70, 90, 200);

        draw_text(r, (int)(bx + 12), (int)(by + 6),
                  title, 255, 255, 255);

        /* Edit field on its own line, with a trailing underscore as a
         * cursor so users can see where input lands. */
        char shown[PATH_MAX + 2];
        snprintf(shown, sizeof(shown), "%s_", ov->edit_buf);
        draw_text(r, (int)(bx + 12), (int)(by + 6 + FONT_H + 8),
                  shown, 180, 240, 180);

        draw_text(r, (int)(bx + 12), (int)(by + 6 + (FONT_H + 8) * 2 + 8),
                  help, 200, 200, 100);
        return;
    }
    if (ov->state == OV_STATE_EDIT_CONFIRM) {
        int ww = DISPLAY_LOGICAL_W;
        int wh = ov->disp ? ov->disp->logical_h : DISPLAY_LOGICAL_H;
        const int FONT_W = 8, FONT_H = 8;
        const char *line1 = "Save changes to Serial PATH?";
        const char *line2 = "Enter/Y = Save    Esc/N = Discard";
        int l1w = (int)strlen(line1) * FONT_W;
        int l2w = (int)strlen(line2) * FONT_W;
        int box_w = l2w + 24;
        int box_h = FONT_H * 2 + 24;
        float bx = (ww - box_w) / 2.0f;
        float by = (wh - box_h) / 2.0f;

        fill_rect(r, 0, 0, (float)ww, (float)wh, 0, 0, 0, 140);
        fill_rect(r, bx, by, (float)box_w, (float)box_h, 25, 25, 60, 255);
        draw_rect_outline(r, bx, by, (float)box_w, (float)box_h, 70, 90, 200);

        draw_text(r, (int)(bx + (box_w - l1w) / 2.0f), (int)(by + 6),
                  line1, 255, 255, 255);
        draw_text(r, (int)(bx + (box_w - l2w) / 2.0f), (int)(by + 6 + FONT_H + 8),
                  line2, 200, 200, 100);
        return;
    }

    /* Centered confirm dialog (matches 1984's modal). Drawn on top of
     * the dimmed window so the user can read context behind it. The
     * renderer uses a fixed logical-presentation size (see display.c
     * SDL_SetRenderLogicalPresentation), so we centre against that
     * rather than the physical output. */
    if (ov->state == OV_STATE_CONFIRM) {
        int ww = DISPLAY_LOGICAL_W;
        int wh = ov->disp ? ov->disp->logical_h : DISPLAY_LOGICAL_H;
        /* SDL3 debug font cell is ~8 logical pixels wide / tall. */
        const int FONT_W = 8, FONT_H = 8;
        const char *line1 = "Save changes?";
        const char *line2 = "Enter = Save      Esc = Discard";
        int l1w = (int)strlen(line1) * FONT_W;
        int l2w = (int)strlen(line2) * FONT_W;
        int box_w = l2w + 24;
        int box_h = FONT_H * 2 + 24;
        float bx = (ww - box_w) / 2.0f;
        float by = (wh - box_h) / 2.0f;

        /* Dim the whole window behind the box. */
        fill_rect(r, 0, 0, (float)ww, (float)wh, 0, 0, 0, 140);
        fill_rect(r, bx, by, (float)box_w, (float)box_h, 25, 25, 60, 255);
        draw_rect_outline(r, bx, by, (float)box_w, (float)box_h, 70, 90, 200);

        draw_text(r, (int)(bx + (box_w - l1w) / 2.0f), (int)(by + 6),
                  line1, 255, 255, 255);
        draw_text(r, (int)(bx + (box_w - l2w) / 2.0f), (int)(by + 6 + FONT_H + 8),
                  line2, 200, 200, 100);
    }
}

void overlay_tick(Overlay *ov) {
    if (!ov->dialog_ready) return;
    ov->dialog_ready = false;
    switch (ov->dialog_kind) {
        case DIALOG_DISK: {
            char *slot = ov->dialog_drive == 0
                       ? ov->cfg->drive_a : ov->cfg->drive_b;
            Disk *d = &ov->pcw->fdc.drive[ov->dialog_drive];
            /* If the user is swapping a disk that has unsaved writes,
             * flush them to the old path before mounting the new file. */
            if (d->dirty && slot[0]) disk_save(d, slot);
            snprintf(slot,
                     ov->dialog_drive == 0
                         ? sizeof(ov->cfg->drive_a) : sizeof(ov->cfg->drive_b),
                     "%s", ov->dialog_path);
            disk_load(d, ov->dialog_path);
            copy_dirname(ov->cfg->last_disk_dir,
                         sizeof(ov->cfg->last_disk_dir), ov->dialog_path);
            ov->dirty = true;
            break;
        }
        case DIALOG_DISK_NEW: {
            char *slot = ov->dialog_drive == 0
                       ? ov->cfg->drive_a : ov->cfg->drive_b;
            Disk *d = &ov->pcw->fdc.drive[ov->dialog_drive];
            if (d->dirty && slot[0]) disk_save(d, slot);
            if (disk_create_blank(ov->dialog_path, ov->dialog_disk_type) == 0) {
                snprintf(slot,
                         ov->dialog_drive == 0
                             ? sizeof(ov->cfg->drive_a)
                             : sizeof(ov->cfg->drive_b),
                         "%s", ov->dialog_path);
                disk_load(d, ov->dialog_path);
                copy_dirname(ov->cfg->last_disk_dir,
                             sizeof(ov->cfg->last_disk_dir), ov->dialog_path);
                ov->dirty = true;
            }
            break;
        }
        case DIALOG_SNAPSHOT_LOAD:
            snapshot_load(ov->pcw, ov->dialog_path);
            copy_dirname(ov->cfg->last_snap_dir,
                         sizeof(ov->cfg->last_snap_dir), ov->dialog_path);
            ov->dirty = true;
            break;
        case DIALOG_SNAPSHOT_SAVE:
            snapshot_save(ov->pcw, ov->dialog_path);
            copy_dirname(ov->cfg->last_snap_dir,
                         sizeof(ov->cfg->last_snap_dir), ov->dialog_path);
            ov->dirty = true;
            break;
        case DIALOG_PRINT_DIR:
            snprintf(ov->cfg->ext_pdf_printer_dir,
                     sizeof(ov->cfg->ext_pdf_printer_dir), "%s", ov->dialog_path);
            ov->cfg->ext_pdf_printer = true;
            apply_pdf_printer(ov);
            leds_set_enabled(LED_PRINTER, true);
            ov->dirty = true;
            break;
        case DIALOG_BOOT_ROM_DIR:
            snprintf(ov->cfg->boot_rom_dir,
                     sizeof(ov->cfg->boot_rom_dir), "%s", ov->dialog_path);
            /* Remember this dir so the picker reopens here next time —
             * survives a Del-clear of boot_rom_dir alone (only an
             * explicit Del on the Boot ROM row wipes both). */
            snprintf(ov->cfg->last_boot_rom_dir,
                     sizeof(ov->cfg->last_boot_rom_dir), "%s",
                     ov->dialog_path);
            if (ov->pcw) {
                bootstrap_set_override_dir(&ov->pcw->boot,
                                           ov->cfg->boot_rom_dir);
                bootstrap_reset(&ov->pcw->boot);
            }
            ov->dirty = true;
            break;
        default: break;
    }
    ov->dialog_kind = DIALOG_NONE;
}
