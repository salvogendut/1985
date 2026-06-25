#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DISK_MAX_TRACKS   84
#define DISK_MAX_SIDES     2
#define DISK_MAX_SECTORS  29

typedef struct {
    uint8_t C, H, R, N;    /* CHRN — cylinder, head, record, size code */
    uint8_t st1, st2;       /* pre-set status flags from DSK file */
    int     offset;         /* byte offset within track data[] */
    int     size;           /* actual data bytes (128 << N, or per extended header) */
} DiskSector;

typedef struct {
    int        sector_count;
    DiskSector sectors[DISK_MAX_SECTORS];
    uint8_t   *data;        /* raw sector data (heap-allocated) */
    int        data_size;
} DiskTrack;

/* Override for how a mounted disk is interpreted by drive B's stepping.
 * AUTO uses disk geometry to decide; CF2 / CF2DD force the choice. */
typedef enum {
    DISK_TYPE_AUTO = 0,
    DISK_TYPE_CF2,            /* 3" SS DD, 40 tracks, 180k — Amsoft CF2 */
    DISK_TYPE_CF2DD,          /* 3"/3.5" DS DD, 80 tracks, 720k — CF2DD */
} DiskType;

typedef struct {
    bool      inserted;
    bool      write_protected;
    bool      dirty;        /* set when in-memory data diverges from the file on disk */
    bool      is_dd_drive;  /* drive B on PCW 8512/9512: 80-track CF2DD; double-steps 40-track media */
    DiskType  type;         /* user override for double-step decision (AUTO by default) */
    int       track_count;
    int       sides;
    DiskTrack track[DISK_MAX_TRACKS][DISK_MAX_SIDES];
    int       cur_track;    /* current head position (in drive's physical track units) */
    int       cur_sector;   /* last-used sector index (for READ ID rotation) */
} Disk;

/* Map drive-head physical track to media track. A CF2 (40-track) disk in
 * a CF2DD (80-track) drive — drive B on PCW 8512 — is double-stepped by
 * BIOS: cur_track 2 = media track 1. The disk's type override forces
 * this decision when set; AUTO falls back to a geometry-based heuristic. */
static inline int disk_media_track(const Disk *d) {
    if (!d->is_dd_drive) return d->cur_track;
    bool dbl;
    switch (d->type) {
        case DISK_TYPE_CF2:   dbl = true;  break;
        case DISK_TYPE_CF2DD: dbl = false; break;
        default:              dbl = (d->track_count > 0 && d->track_count <= 42);
    }
    return dbl ? d->cur_track / 2 : d->cur_track;
}

void disk_init(Disk *d);
void disk_eject(Disk *d);

/* Returns 0 on success, -1 on error. */
int  disk_load(Disk *d, const char *path);

/* Serialize the in-memory disk to an EXTENDED CPC DSK file at `path`.
 * Returns 0 on success, -1 on error. Clears Disk.dirty on success. */
int  disk_save(Disk *d, const char *path);

/* Create a freshly-formatted blank EXTENDED DSK file at `path`.
 * `type` selects geometry: CF2 (or AUTO) → 40-track SS 180k; CF2DD →
 * 80-track DS 720k. Returns 0 on success, -1 on error. */
int  disk_create_blank(const char *path, DiskType type);

/* Find a sector by CHRN on the current track. Returns NULL if not found. */
DiskSector *disk_find_sector(Disk *d, int side, uint8_t C, uint8_t H,
                             uint8_t R, uint8_t N);
