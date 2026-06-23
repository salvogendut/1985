#include "overlay.h"
#include "display.h"
#include "pcw.h"
#include "disk.h"
#include "snapshot.h"
#include "leds.h"
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
    EXT_PRINTER,
    EXT_SERIAL,
    EXT_PERRYFI,
    EXT_DKTRONICS,
} ExtRow;

static ExtRow ext_row_at(const Config *cfg, int row) {
    (void)cfg;
    int r = 0;
    if (row == r++) return EXT_PRINTER;
    if (row == r++) return EXT_SERIAL;
    if (row == r++) return EXT_PERRYFI;
    if (row == r++) return EXT_DKTRONICS;
    return EXT_NONE;
}

/* General-tab row identity. Same shape as ExtRow: the displayed row
 * index shifts when an 8256-only row is hidden, so navigation routes
 * by kind. */
typedef enum {
    GEN_NONE = 0,
    GEN_MODEL,
    GEN_MEMORY,
    GEN_SECOND_DRIVE,    /* 8256 only — 8512/9512 have two drives stock */
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
            /* model, memory, [second drive on 8256], backplane, tinker */
            return ov->cfg->model == PCW_MODEL_8256 ? 5 : 4;
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
             *   Printer
             *   Serial port
             *   PerryFi                 (AT-modem; lives on the serial line)
             *   DK'TRONICS Sound & Joystick
             *
             * The Serial backend toggle (pty/tcp) lives under Advanced —
             * it's a developer convenience, not a hardware option.
             * Second drive moved to General — a stock-PCW accessory that
             * doesn't need the backplane. */
            if (!ov->cfg->ext_sanpollo_backplane) return 0;
            return 4;
        }
        case OV_TINKER:     return ov->cfg->tinker ? 16 : 0;
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

static const char *sink_str(PrintSink s) {
    return (s == PRINT_SINK_REAL) ? "Real printer" : "PDF file";
}

static const char *printer_hw_str(const Config *cfg) {
    if (cfg->model == PCW_MODEL_9512)
        return cfg->printer_centronics_9512 ? "Centronics" : "Daisywheel";
    return "Dot-matrix";
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
                case GEN_BACKPLANE:    snprintf(label, lsz, "PCW Backplane"); snprintf(val, vsz, "%s", bool_str(cfg->ext_sanpollo_backplane));  break;
                case GEN_TINKER:       snprintf(label, lsz, "Tinker");        snprintf(val, vsz, "%s", bool_str(cfg->tinker));                  break;
                case GEN_NONE: default: break;
            }
            break;
        case OV_MEDIA:
            switch (row) {
                case 0: snprintf(label, lsz, "Drive A"); snprintf(val, vsz, "%s", cfg->drive_a[0] ? cfg->drive_a : "(empty)"); break;
                case 1: snprintf(label, lsz, "Drive B"); snprintf(val, vsz, "%s", cfg->drive_b[0] ? cfg->drive_b : "(empty)"); break;
            }
            break;
        case OV_EXTENSIONS:
            switch (ext_row_at(cfg, row)) {
                case EXT_PRINTER:
                    snprintf(label, lsz, "PDF printer");
                    if (!cfg->ext_pdf_printer)
                        snprintf(val, vsz, "no");
                    else if (cfg->ext_pdf_printer_dir[0])
                        snprintf(val, vsz, "yes: %s", cfg->ext_pdf_printer_dir);
                    else
                        snprintf(val, vsz, "yes: [choose folder]");
                    break;
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
                default: break;
            }
            break;
        case OV_TINKER:
            switch (row) {
                case 0: snprintf(label, lsz, "Smoothing");      snprintf(val, vsz, "%s", bool_str(cfg->fullscreen_smoothing)); break;
                case 1: snprintf(label, lsz, "Tint");           snprintf(val, vsz, "%s", mono_str(cfg->monochrome));           break;
                case 2: snprintf(label, lsz, "Tint mode");      snprintf(val, vsz, "%s", cfg->tint_glow ? "glow" : "normal");  break;
                case 3: snprintf(label, lsz, "Video mode");     snprintf(val, vsz, "%s", video_str(cfg->video_mode));          break;
                case 4: snprintf(label, lsz, "Printer mode");   snprintf(val, vsz, "%s", sink_str(cfg->ext_print_sink));       break;
                case 5: snprintf(label, lsz, "Printer model");  snprintf(val, vsz, "%s", printer_hw_str(cfg));                 break;
                case 6: snprintf(label, lsz, "Debugging");      snprintf(val, vsz, "%s", bool_str(cfg->debug));                break;
                case 7: snprintf(label, lsz, "Debug output");   snprintf(val, vsz, "%s", bool_str(cfg->debug_traces));         break;
                case 8: snprintf(label, lsz, "Trace IO");       snprintf(val, vsz, "%s", bool_str(cfg->trace_io));             break;
                case 9: snprintf(label, lsz, "Trace FDC");      snprintf(val, vsz, "%s", bool_str(cfg->trace_fdc));            break;
                case 10: snprintf(label, lsz, "Trace Input");   snprintf(val, vsz, "%s", bool_str(cfg->trace_input));          break;
                case 11:
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
                case 12: snprintf(label, lsz, "Show keyboard layout"); snprintf(val, vsz, "...");                              break;
                case 13: snprintf(label, lsz, "Save snapshot"); snprintf(val, vsz, "...");                                     break;
                case 14: snprintf(label, lsz, "Load snapshot"); snprintf(val, vsz, "...");                                     break;
                case 15: snprintf(label, lsz, "Version");       snprintf(val, vsz, "1985 v" "0.4.0");                          break;
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

static void open_disk_dialog(Overlay *ov, int drive) {
    ov->dialog_kind  = DIALOG_DISK;
    ov->dialog_drive = drive;
    ov->dialog_ready = false;
    static const SDL_DialogFileFilter filters[] = {
        { "DSK images", "dsk;DSK" },
        { "All files",  "*"       },
    };
    SDL_ShowOpenFileDialog(overlay_file_callback, ov, NULL,
                           filters, 2, NULL, false);
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
                           filters, 2, NULL, false);
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
                           filters, 2, NULL);
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

static void eject_disk(Overlay *ov, int drive) {
    Config *c = ov->cfg;
    char *slot = (drive == 0) ? c->drive_a : c->drive_b;
    if (slot[0] == 0) return;
    slot[0] = 0;
    disk_eject(&ov->pcw->fdc.drive[drive]);
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
                case GEN_BACKPLANE:
                    c->ext_sanpollo_backplane = !c->ext_sanpollo_backplane;
                    /* Pulling the backplane disables everything that
                     * was plugged into it. PDF printer goes too — the
                     * whole Extensions tab disappears. */
                    if (!c->ext_sanpollo_backplane) {
                        c->ext_pdf_printer = false;
                        c->ext_dktronics   = false;
                        c->ext_perryfi     = false;
                        if (c->model != PCW_MODEL_9512) {
                            c->ext_serial = false;
                            if (ov->pcw) {
                                serial_shutdown(&ov->pcw->serial);
                                cps_set_present(&ov->pcw->cps, false);
                            }
                            leds_set_enabled(LED_SERIAL, false);
                        }
                        apply_pdf_printer(ov);
                    }
                    leds_set_enabled(LED_PRINTER, c->ext_sanpollo_backplane);
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
                                    c->ext_serial_tcp_port);
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
                    ov->dirty = true;
                    break;
                case EXT_PRINTER:
                    if (c->ext_pdf_printer) {
                        c->ext_pdf_printer = false;
                        apply_pdf_printer(ov);
                        ov->dirty = true;
                    } else {
                        open_printer_dir_dialog(ov);
                    }
                    break;
                case EXT_NONE:
                default:
                    break;
            }
            break;
        case OV_TINKER:
            switch (ov->row) {
                case 0: c->fullscreen_smoothing = !c->fullscreen_smoothing; ov->dirty = true; break;
                case 1:
                    cycle_mono(&c->monochrome);
                    if (ov->disp) display_set_monochrome(ov->disp, c->monochrome);
                    ov->dirty = true;
                    break;
                case 2:
                    c->tint_glow = !c->tint_glow;
                    if (ov->disp) display_set_tint_glow(ov->disp, c->tint_glow);
                    ov->dirty = true;
                    break;
                case 3:
                    cycle_video(&c->video_mode);
                    if (ov->disp) display_set_video_mode(ov->disp, c->video_mode);
                    ov->dirty = true;
                    break;
                case 4:
                    c->ext_print_sink = (c->ext_print_sink == PRINT_SINK_REAL)
                                      ? PRINT_SINK_PDF : PRINT_SINK_REAL;
                    apply_pdf_printer(ov);
                    ov->dirty = true;
                    break;
                case 5:
                    /* Toggle daisywheel ↔ Centronics — only meaningful on
                     * the 9512. On 8256/8512 the row reads "Dot-matrix"
                     * and the toggle is a no-op. */
                    if (c->model == PCW_MODEL_9512) {
                        c->printer_centronics_9512 = !c->printer_centronics_9512;
                        apply_pdf_printer(ov);
                        ov->dirty = true;
                    }
                    break;
                case 6: c->debug        = !c->debug;        ov->dirty = true; break;
                case 7: c->debug_traces = !c->debug_traces; ov->dirty = true; break;
                case 8: c->trace_io     = !c->trace_io;     ov->dirty = true; break;
                case 9: c->trace_fdc    = !c->trace_fdc;    ov->dirty = true; break;
                case 10: c->trace_input = !c->trace_input;  ov->dirty = true; break;
                case 11:
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
                                        c->ext_serial_tcp_port);
                        }
                        ov->dirty = true;
                    }
                    break;
                case 12: ov->state = OV_STATE_KEYS;       break;
                case 13: open_snapshot_save_dialog(ov);   break;
                case 14: open_snapshot_load_dialog(ov);   break;
                case 15: break;
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
                           || (ov->cfg->ext_dktronics        != ov->saved.ext_dktronics);
        config_save(ov->cfg);
        ov->dirty = false;
        if (need_cold_boot && ov->pcw) {
            printer_shutdown(&ov->pcw->printer);
            pcw_cold_boot(ov->pcw, ov->cfg->model, ov->cfg->memory_kb);
            /* pcw_init zeroed the FDC — re-mount the disk images so
             * the machine boots from them just like at first launch. */
            if (ov->cfg->drive_a[0]) disk_load(&ov->pcw->fdc.drive[0], ov->cfg->drive_a);
            if (ov->cfg->drive_b[0]) disk_load(&ov->pcw->fdc.drive[1], ov->cfg->drive_b);
            bool savail = (ov->cfg->model == PCW_MODEL_9512) || ov->cfg->ext_sanpollo_backplane;
            bool s_on   = ov->cfg->ext_serial && savail;
            bool p_on   = s_on && ov->cfg->ext_perryfi;
            serial_init(&ov->pcw->serial, s_on && !p_on,
                        ov->cfg->ext_serial_backend,
                        ov->cfg->ext_serial_tcp_port);
            perryfi_init(&ov->pcw->perryfi, p_on);
            cps_set_present(&ov->pcw->cps, s_on);
            leds_set_enabled(LED_SERIAL, s_on);

            bool dk_on = ov->cfg->ext_sanpollo_backplane && ov->cfg->ext_dktronics;
            aysound_init(&ov->pcw->ay, dk_on);
            apply_pdf_printer(ov);
        }
    } else if (!save) {
        *ov->cfg = ov->saved;
        apply_pdf_printer(ov);
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

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_FRect bg = { 8, 8, 600, 280 };
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

    int rc = row_count(ov, ov->section);
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

    draw_text(r, ORIGIN_X, 260,
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
        int wh = DISPLAY_LOGICAL_H;
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

    /* Centered confirm dialog (matches 1984's modal). Drawn on top of
     * the dimmed window so the user can read context behind it. The
     * renderer uses a fixed logical-presentation size (see display.c
     * SDL_SetRenderLogicalPresentation), so we centre against that
     * rather than the physical output. */
    if (ov->state == OV_STATE_CONFIRM) {
        int ww = DISPLAY_LOGICAL_W;
        int wh = DISPLAY_LOGICAL_H;
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
            if (ov->dialog_drive == 0) snprintf(ov->cfg->drive_a, sizeof(ov->cfg->drive_a), "%s", ov->dialog_path);
            else                       snprintf(ov->cfg->drive_b, sizeof(ov->cfg->drive_b), "%s", ov->dialog_path);
            Disk *d = &ov->pcw->fdc.drive[ov->dialog_drive];
            disk_load(d, ov->dialog_path);
            ov->dirty = true;
            break;
        }
        case DIALOG_SNAPSHOT_LOAD: snapshot_load(ov->pcw, ov->dialog_path); break;
        case DIALOG_SNAPSHOT_SAVE: snapshot_save(ov->pcw, ov->dialog_path); break;
        case DIALOG_PRINT_DIR:
            snprintf(ov->cfg->ext_pdf_printer_dir,
                     sizeof(ov->cfg->ext_pdf_printer_dir), "%s", ov->dialog_path);
            ov->cfg->ext_pdf_printer = true;
            apply_pdf_printer(ov);
            ov->dirty = true;
            break;
        default: break;
    }
    ov->dialog_kind = DIALOG_NONE;
}
