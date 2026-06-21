#include "overlay.h"
#include "pcw.h"
#include "disk.h"
#include "snapshot.h"
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
#define VAL_X    220

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
        case OV_GENERAL:    return 3;  /* model, memory, tinker */
        case OV_MEDIA:      return 2;  /* drive A, drive B */
        case OV_EXTENSIONS: return 1;  /* printer placeholder */
        case OV_TINKER:     return ov->cfg->tinker ? 8 : 0;
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

static const char *bool_str(bool b) { return b ? "yes" : "no"; }

static void item_text(const Overlay *ov, int row, char *label, size_t lsz, char *val, size_t vsz) {
    const Config *cfg = ov->cfg;
    label[0] = 0; val[0] = 0;
    switch (ov->section) {
        case OV_GENERAL:
            switch (row) {
                case 0: snprintf(label, lsz, "Model");     snprintf(val, vsz, "%s", model_str(cfg->model)); break;
                case 1: snprintf(label, lsz, "RAM (KB)");  snprintf(val, vsz, "%d", cfg->memory_kb);        break;
                case 2: snprintf(label, lsz, "Tinker");    snprintf(val, vsz, "%s", bool_str(cfg->tinker)); break;
            }
            break;
        case OV_MEDIA:
            switch (row) {
                case 0: snprintf(label, lsz, "Drive A"); snprintf(val, vsz, "%s", cfg->drive_a[0] ? cfg->drive_a : "(empty)"); break;
                case 1: snprintf(label, lsz, "Drive B"); snprintf(val, vsz, "%s", cfg->drive_b[0] ? cfg->drive_b : "(empty)"); break;
            }
            break;
        case OV_EXTENSIONS:
            switch (row) {
                case 0: snprintf(label, lsz, "Printer"); snprintf(val, vsz, "(not implemented)"); break;
            }
            break;
        case OV_TINKER:
            switch (row) {
                case 0: snprintf(label, lsz, "Smoothing");      snprintf(val, vsz, "%s", bool_str(cfg->fullscreen_smoothing)); break;
                case 1: snprintf(label, lsz, "Monochrome");     snprintf(val, vsz, "%s", mono_str(cfg->monochrome));           break;
                case 2: snprintf(label, lsz, "Debugging");      snprintf(val, vsz, "%s", bool_str(cfg->debug));                break;
                case 3: snprintf(label, lsz, "Trace IO");       snprintf(val, vsz, "%s", bool_str(cfg->trace_io));             break;
                case 4: snprintf(label, lsz, "Trace FDC");      snprintf(val, vsz, "%s", bool_str(cfg->trace_fdc));            break;
                case 5: snprintf(label, lsz, "Trace Input");    snprintf(val, vsz, "%s", bool_str(cfg->trace_input));          break;
                case 6: snprintf(label, lsz, "Load snapshot");  snprintf(val, vsz, "...");                                     break;
                case 7: snprintf(label, lsz, "Version");        snprintf(val, vsz, "1985 v" "0.1.0");                          break;
            }
            break;
        default: break;
    }
}

static void cycle_mono(MonoMode *m) {
    *m = (MonoMode)(((int)*m + 1) % 4);
}

static void open_disk_dialog(Overlay *ov, int drive) {
    /* File picker wiring lands in a follow-up; for the scaffold we
     * just mark a pending dialog so the caller knows nothing happens
     * — disk paths are set via 1985.conf for now. */
    ov->dialog_kind  = DIALOG_DISK;
    ov->dialog_drive = drive;
    ov->dialog_ready = false;
}

static void activate(Overlay *ov) {
    Config *c = ov->cfg;
    switch (ov->section) {
        case OV_GENERAL:
            switch (ov->row) {
                case 0:
                    c->model = (PcwModel)(((int)c->model + 1) % 3);
                    ov->dirty = true;
                    break;
                case 1:
                    c->memory_kb = (c->memory_kb == 256) ? 512 : (c->memory_kb == 512) ? 2048 : 256;
                    ov->dirty = true;
                    break;
                case 2:
                    c->tinker = !c->tinker;
                    ov->dirty = true;
                    break;
            }
            break;
        case OV_MEDIA:
            open_disk_dialog(ov, ov->row);
            break;
        case OV_EXTENSIONS:
            break;
        case OV_TINKER:
            switch (ov->row) {
                case 0: c->fullscreen_smoothing = !c->fullscreen_smoothing; ov->dirty = true; break;
                case 1: cycle_mono(&c->monochrome); ov->dirty = true; break;
                case 2: c->debug       = !c->debug;       ov->dirty = true; break;
                case 3: c->trace_io    = !c->trace_io;    ov->dirty = true; break;
                case 4: c->trace_fdc   = !c->trace_fdc;   ov->dirty = true; break;
                case 5: c->trace_input = !c->trace_input; ov->dirty = true; break;
                case 6: ov->dialog_kind = DIALOG_SNAPSHOT_LOAD; break;
                case 7: break;
            }
            break;
        default: break;
    }
}

void overlay_init(Overlay *ov, Config *cfg, struct PCW *pcw) {
    memset(ov, 0, sizeof(*ov));
    ov->cfg = cfg;
    ov->pcw = pcw;
}

static void close_overlay(Overlay *ov, bool save) {
    if (save && ov->dirty) {
        config_save(ov->cfg);
        ov->dirty = false;
    } else if (!save) {
        *ov->cfg = ov->saved;
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

    if (ov->state == OV_STATE_CONFIRM) {
        if (ev->key.key == SDLK_Y) close_overlay(ov, true);
        else if (ev->key.key == SDLK_N) close_overlay(ov, false);
        return true;
    }

    int rc = row_count(ov, ov->section);
    switch (ev->key.key) {
        case SDLK_ESCAPE:
            if (ov->dirty) ov->state = OV_STATE_CONFIRM;
            else close_overlay(ov, false);
            break;
        case SDLK_TAB:
            do {
                ov->section = (OvSection)((ov->section + 1) % OV_SEC_COUNT);
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
    }
    return true;
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *s, u8 R, u8 G, u8 B) {
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_RenderDebugText(r, (float)x, (float)y, s);
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
        bool active = (ov->section == s);
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

    if (ov->state == OV_STATE_CONFIRM) {
        draw_text(r, ORIGIN_X, ORIGIN_Y + 9 * LINE_H + 8,
                  "Save changes? (Y/N)", 255, 80, 80);
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
        default: break;
    }
    ov->dialog_kind = DIALOG_NONE;
}
