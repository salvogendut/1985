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

typedef struct {
    bool      inserted;
    bool      write_protected;
    bool      dirty;        /* set when in-memory data diverges from the file on disk */
    int       track_count;
    int       sides;
    DiskTrack track[DISK_MAX_TRACKS][DISK_MAX_SIDES];
    int       cur_track;    /* current head position */
    int       cur_sector;   /* last-used sector index (for READ ID rotation) */
} Disk;

void disk_init(Disk *d);
void disk_eject(Disk *d);

/* Returns 0 on success, -1 on error. */
int  disk_load(Disk *d, const char *path);

/* Serialize the in-memory disk to an EXTENDED CPC DSK file at `path`.
 * Returns 0 on success, -1 on error. Clears Disk.dirty on success. */
int  disk_save(Disk *d, const char *path);

/* Create a freshly-formatted blank EXTENDED DSK file at `path` with
 * 0 tracks (the guest will FORMAT TRACK to populate it). Returns 0
 * on success, -1 on error. Does not modify any in-memory Disk. */
int  disk_create_blank(const char *path);

/* Find a sector by CHRN on the current track. Returns NULL if not found. */
DiskSector *disk_find_sector(Disk *d, int side, uint8_t C, uint8_t H,
                             uint8_t R, uint8_t N);
