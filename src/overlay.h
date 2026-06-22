#pragma once
#include <stdbool.h>
#include <SDL3/SDL.h>
#include "config.h"

typedef enum {
    OV_GENERAL    = 0,
    OV_MEDIA      = 1,
    OV_EXTENSIONS = 2,
    OV_TINKER     = 3,
    OV_SEC_COUNT  = 4
} OvSection;

typedef enum {
    OV_STATE_MENU    = 0,
    OV_STATE_CONFIRM = 1,
    OV_STATE_KEYS    = 2,
} OvState;

typedef enum {
    DIALOG_NONE          = 0,
    DIALOG_DISK          = 1,
    DIALOG_SNAPSHOT_LOAD = 2,
    DIALOG_SNAPSHOT_SAVE = 3,
    DIALOG_PRINT_DIR     = 4,
} DialogKind;

struct PCW;

typedef struct {
    bool        visible;
    OvSection   section;
    int         row;
    OvState     state;
    bool        dirty;
    Config     *cfg;
    Config      saved;
    struct PCW *pcw;
    /* pending file-dialog result */
    DialogKind  dialog_kind;
    int         dialog_drive;    /* 0=A, 1=B */
    char        dialog_path[PATH_MAX];
    bool        dialog_ready;
    bool        needs_cold_boot;
} Overlay;

void overlay_init(Overlay *ov, Config *cfg, struct PCW *pcw);

bool overlay_handle_event(Overlay *ov, SDL_Event *ev);
void overlay_render(Overlay *ov, SDL_Renderer *r);
void overlay_tick  (Overlay *ov);
