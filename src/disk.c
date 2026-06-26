#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void disk_init(Disk *d) {
    memset(d, 0, sizeof(*d));
}

void disk_eject(Disk *d) {
    for (int t = 0; t < DISK_MAX_TRACKS; t++)
        for (int s = 0; s < DISK_MAX_SIDES; s++)
            free(d->track[t][s].data);
    int cur_track = d->cur_track;
    bool dd = d->is_dd_drive;
    DiskType t = d->type;
    memset(d, 0, sizeof(*d));
    d->cur_track = cur_track;   /* preserve head position across eject */
    d->is_dd_drive = dd;        /* drive type is a hardware property, not media */
    d->type = t;                /* per-drive format override persists across swaps */
}

static int load_track(DiskTrack *tr, FILE *f, int track_size) {
    if (track_size < 256) return 0;

    uint8_t hdr[256];
    if (fread(hdr, 1, 256, f) != 256) return -1;

    /* "Track-Info" marker */
    if (memcmp(hdr, "Track-Info", 10) != 0) return 0;

    int spt  = hdr[0x15];
    if (spt > DISK_MAX_SECTORS) spt = DISK_MAX_SECTORS;
    tr->sector_count = spt;

    /* Sector data follows the 256-byte header */
    int data_size = track_size - 256;
    if (data_size > 0) {
        tr->data = malloc(data_size);
        if (!tr->data) return -1;
        tr->data_size = (int)fread(tr->data, 1, data_size, f);
    }

    int offset = 0;
    for (int i = 0; i < spt; i++) {
        uint8_t *si = hdr + 0x18 + i * 8;
        DiskSector *sec = &tr->sectors[i];
        sec->C   = si[0];
        sec->H   = si[1];
        sec->R   = si[2];
        sec->N   = si[3];
        sec->st1 = si[4];
        sec->st2 = si[5];
        /* Extended DSK stores actual size in bytes at si[6..7] */
        int sz = (si[7] << 8) | si[6];
        if (sz == 0) sz = 128 << sec->N;
        sec->size   = sz;
        sec->offset = offset;
        offset += sz;
    }
    return 0;
}

int disk_load(Disk *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "disk: cannot open %s\n", path); return -1; }

    uint8_t hdr[256];
    if (fread(hdr, 1, 256, f) != 256) { fclose(f); return -1; }

    bool extended;
    if      (memcmp(hdr, "MV - CPC", 8) == 0) extended = false;
    else if (memcmp(hdr, "EXTENDED", 8) == 0) extended = true;
    else {
        fprintf(stderr, "disk: %s is not a CPC/PCW DSK file\n", path);
        fclose(f);
        return -1;
    }

    disk_eject(d);

    int tracks = hdr[0x30];
    int sides  = hdr[0x31];
    if (sides  < 1) sides  = 1;
    if (sides  > 2) sides  = 2;
    if (tracks > DISK_MAX_TRACKS) tracks = DISK_MAX_TRACKS;

    d->track_count = tracks;
    d->sides       = sides;
    d->inserted    = true;

    /* Normal DSK: fixed track size for all tracks */
    int fixed_track_size = 0;
    if (!extended)
        fixed_track_size = (hdr[0x33] << 8) | hdr[0x32];

    for (int t = 0; t < tracks; t++) {
        for (int s = 0; s < sides; s++) {
            int ts;
            if (extended)
                ts = hdr[0x34 + t * sides + s] * 256;
            else
                ts = fixed_track_size;

            if (ts == 0) continue;  /* missing track in extended DSK */

            if (load_track(&d->track[t][s], f, ts) < 0) {
                fprintf(stderr, "disk: error reading track %d side %d\n", t, s);
                fclose(f);
                return -1;
            }
        }
    }

    fclose(f);
    return 0;
}

/* Round n up to the next multiple of 256. */
static int round_up_256(int n) {
    return (n + 255) & ~255;
}

/* Build a 256-byte EXTENDED DSK Disk-Info header. */
static void build_disk_info(uint8_t hdr[256], int tracks, int sides,
                            const uint8_t *track_size_table) {
    memset(hdr, 0, 256);
    memcpy(hdr, "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
    memcpy(hdr + 0x22, "1985        ", 12);   /* creator (14 byte field) */
    hdr[0x30] = (uint8_t)tracks;
    hdr[0x31] = (uint8_t)sides;
    /* 0x32..0x33: unused in EXTENDED variant. */
    int n = tracks * sides;
    if (n > 256 - 0x34) n = 256 - 0x34;
    memcpy(hdr + 0x34, track_size_table, (size_t)n);
}

/* Compute the on-disk size of a track including its 256-byte header,
 * rounded up to a 256-byte boundary. Empty tracks contribute 0. */
static int track_on_disk_size(const DiskTrack *tr) {
    if (tr->sector_count == 0) return 0;
    int total = 0;
    for (int i = 0; i < tr->sector_count; i++)
        total += tr->sectors[i].size;
    return 256 + round_up_256(total);
}

int disk_save(Disk *d, const char *path) {
    if (!path || !path[0]) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "disk: cannot write %s\n", path);
        return -1;
    }

    int tracks = d->track_count;
    int sides  = d->sides ? d->sides : 1;
    if (tracks > DISK_MAX_TRACKS) tracks = DISK_MAX_TRACKS;
    if (sides  > DISK_MAX_SIDES)  sides  = DISK_MAX_SIDES;

    /* Build per-track size table (1 byte = size_in_bytes / 256). */
    uint8_t track_table[256 - 0x34];
    memset(track_table, 0, sizeof(track_table));
    int idx = 0;
    for (int t = 0; t < tracks; t++) {
        for (int s = 0; s < sides; s++) {
            int sz = track_on_disk_size(&d->track[t][s]);
            if (sz > 0xFF * 256) sz = 0xFF * 256;
            if (idx < (int)sizeof(track_table))
                track_table[idx] = (uint8_t)(sz / 256);
            idx++;
        }
    }

    uint8_t disk_info[256];
    build_disk_info(disk_info, tracks, sides, track_table);
    if (fwrite(disk_info, 1, 256, f) != 256) goto err;

    /* Emit each non-empty track: 256-byte Track-Info + sector data. */
    for (int t = 0; t < tracks; t++) {
        for (int s = 0; s < sides; s++) {
            const DiskTrack *tr = &d->track[t][s];
            if (tr->sector_count == 0) continue;

            uint8_t hdr[256];
            memset(hdr, 0, 256);
            memcpy(hdr, "Track-Info\r\n", 12);
            hdr[0x10] = (uint8_t)t;
            hdr[0x11] = (uint8_t)s;
            /* 0x12 data rate: 2 = 250 kbps. 0x13 recording mode: 2 = MFM.
             * PCW always uses MFM/250 kbps; omitting these (leaving 0)
             * makes CP/M+ BIOS treat the disk as "unknown encoding" and
             * refuse to write — even Joyce reproduces the bug. */
            hdr[0x12] = 0x02;   /* data rate = 250 kbps */
            hdr[0x13] = 0x02;   /* recording mode = MFM */
            /* 0x14 sector size code: use first sector's N (real-disk
             * convention; per-sector sizes still live in the sector
             * info table at 0x18+). */
            hdr[0x14] = tr->sectors[0].N;
            hdr[0x15] = (uint8_t)tr->sector_count;
            hdr[0x16] = 0x4E;   /* GAP3 length (industry standard) */
            hdr[0x17] = 0xE5;   /* filler byte */

            for (int i = 0; i < tr->sector_count && i < DISK_MAX_SECTORS; i++) {
                const DiskSector *sec = &tr->sectors[i];
                uint8_t *si = hdr + 0x18 + i * 8;
                si[0] = sec->C;
                si[1] = sec->H;
                si[2] = sec->R;
                si[3] = sec->N;
                si[4] = sec->st1;
                si[5] = sec->st2;
                si[6] = (uint8_t)(sec->size & 0xFF);
                si[7] = (uint8_t)((sec->size >> 8) & 0xFF);
            }
            if (fwrite(hdr, 1, 256, f) != 256) goto err;

            /* Sector data, packed end-to-end in record order, padded
             * up to the next 256-byte boundary. */
            int data_bytes = 0;
            for (int i = 0; i < tr->sector_count; i++) {
                const DiskSector *sec = &tr->sectors[i];
                if (tr->data && sec->offset + sec->size <= tr->data_size) {
                    if (fwrite(tr->data + sec->offset, 1,
                               (size_t)sec->size, f) != (size_t)sec->size)
                        goto err;
                } else {
                    /* Sector advertises data we don't actually have
                     * — pad with the filler byte. */
                    uint8_t pad[256];
                    memset(pad, 0xE5, sizeof(pad));
                    int left = sec->size;
                    while (left > 0) {
                        int chunk = left > (int)sizeof(pad) ? (int)sizeof(pad) : left;
                        if (fwrite(pad, 1, (size_t)chunk, f) != (size_t)chunk)
                            goto err;
                        left -= chunk;
                    }
                }
                data_bytes += sec->size;
            }
            int pad_bytes = round_up_256(data_bytes) - data_bytes;
            if (pad_bytes > 0) {
                static const uint8_t zeros[256] = {0};
                if (fwrite(zeros, 1, (size_t)pad_bytes, f) != (size_t)pad_bytes)
                    goto err;
            }
        }
    }

    fclose(f);
    d->dirty = false;
    return 0;

err:
    fprintf(stderr, "disk: write error to %s\n", path);
    fclose(f);
    return -1;
}

int disk_create_blank(const char *path, DiskType type) {
    if (!path || !path[0]) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "disk: cannot create %s\n", path);
        return -1;
    }

    /* Pre-formatted PCW data disk; sector IDs in standard skew order,
     * filler 0xE5, disc-spec at track 0 side 0 sector R=1. OFF=0 (no
     * reserved tracks) so BIOS reads/writes match the recorded C
     * field (= physical track). See issue #114 for the analysis.
     *
     * CF2:   40 tracks × 1 side  × 9 sec/track × 512 bytes = 180k
     * CF2DD: 80 tracks × 2 sides × 9 sec/track × 512 bytes = 720k */

    const bool is_dd = (type == DISK_TYPE_CF2DD);
    const int TRACKS   = is_dd ? 80 : 40;
    const int SIDES    = is_dd ? 2  : 1;
    const int SPT      = 9;
    const int SEC_SIZE = 512;
    const int N_CODE   = 2;   /* 128 << 2 = 512 */
    static const uint8_t SKEW[9] = { 1, 6, 2, 7, 3, 8, 4, 9, 5 };
    const int TRACK_SIZE = 256 + SPT * SEC_SIZE;   /* 4864 bytes/track */

    uint8_t track_table[256 - 0x34];
    memset(track_table, 0, sizeof(track_table));
    for (int i = 0; i < TRACKS * SIDES; i++)
        track_table[i] = (uint8_t)(TRACK_SIZE / 256);
    uint8_t disk_info[256];
    build_disk_info(disk_info, TRACKS, SIDES, track_table);
    if (fwrite(disk_info, 1, 256, f) != 256) goto err;

    /* PCW disc specification, written into track 0 side 0 sector R=1.
     * Track 0 is RESERVED (OFF=1) so the directory does not overlap the
     * spec sector — matches every real PCW data disk and the boot-disk
     * format. Layout matches Joyce lib765 boot_pcw180[]:
     *   [0] format  [1] sided  [2] tracks  [3] spt  [4] psh  [5] OFF
     *   [6] BSH     [7] dirblk [8] GAP3 r/w [9] GAP3 fmt [10..15] zero
     * CF2 data: format=0, sided=0, OFF=1, BSH=3 (1k blocks), 2 dir blocks
     * CF2DD data: format=3, sided=1, OFF=1, BSH=4 (2k blocks), 4 dir blocks */
    uint8_t spec[16] = {
        (uint8_t)(is_dd ? 0x03 : 0x00),  /* [0] format */
        (uint8_t)(is_dd ? 0x81 : 0x00),  /* [1] sided (bit 0 = DS, bit 7 =
                                          * alternating-sides flag). PCW
                                          * XBIOS's BIOS WRITE slow path
                                          * (49F7 → 4BEA → RLA on this byte)
                                          * needs bit 7 set or writes abort
                                          * silently with no FDC commands.
                                          * Joyce/LibDsk reference CF2DD
                                          * boot sector also uses 0x81. */
        (uint8_t)TRACKS,                 /* [2] tracks per side */
        (uint8_t)SPT,                    /* [3] sectors per track */
        (uint8_t)N_CODE,                 /* [4] psh */
        0x01,                             /* [5] OFF (skip track 0) */
        (uint8_t)(is_dd ? 0x04 : 0x03),  /* [6] BSH */
        (uint8_t)(is_dd ? 0x04 : 0x02),  /* [7] directory blocks */
        0x2A,                             /* [8] GAP3 read/write */
        0x52,                             /* [9] GAP3 format */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    for (int t = 0; t < TRACKS; t++) {
        for (int side = 0; side < SIDES; side++) {
            uint8_t hdr[256];
            memset(hdr, 0, 256);
            memcpy(hdr, "Track-Info\r\n", 12);
            hdr[0x10] = (uint8_t)t;
            hdr[0x11] = (uint8_t)side;
            hdr[0x12] = 0x02;         /* data rate = 250 kbps */
            hdr[0x13] = 0x02;         /* recording mode = MFM */
            hdr[0x14] = (uint8_t)N_CODE;
            hdr[0x15] = (uint8_t)SPT;
            hdr[0x16] = 0x4E;         /* GAP3 */
            hdr[0x17] = 0xE5;         /* filler */
            for (int i = 0; i < SPT; i++) {
                uint8_t *si = hdr + 0x18 + i * 8;
                si[0] = (uint8_t)t;
                si[1] = (uint8_t)side;
                si[2] = SKEW[i];
                si[3] = (uint8_t)N_CODE;
                si[4] = 0;
                si[5] = 0;
                si[6] = (uint8_t)(SEC_SIZE & 0xFF);
                si[7] = (uint8_t)((SEC_SIZE >> 8) & 0xFF);
            }
            if (fwrite(hdr, 1, 256, f) != 256) goto err;

            for (int i = 0; i < SPT; i++) {
                uint8_t sec[SEC_SIZE];
                memset(sec, 0xE5, SEC_SIZE);
                if (t == 0 && side == 0 && SKEW[i] == 1)
                    memcpy(sec, spec, sizeof(spec));
                if (fwrite(sec, 1, SEC_SIZE, f) != SEC_SIZE) goto err;
            }
        }
    }

    fclose(f);
    return 0;

err:
    fprintf(stderr, "disk: write error creating %s\n", path);
    fclose(f);
    return -1;
}

DiskSector *disk_find_sector(Disk *d, int side, uint8_t C, uint8_t H,
                             uint8_t R, uint8_t N) {
    (void)C; (void)H;
    /* uPD765A datasheet: READ/WRITE DATA scans for sectors whose R
     * matches the command. C/H mismatches set status bits (BC/ND-OR)
     * but don't suppress the read. We follow the same model — match
     * by R (and N for size) and let the caller decide whether to
     * flag a C/H mismatch. This is what CP/M+ BIOSes expect when a
     * disk's sector-ID C differs from the BIOS's CP/M-relative track
     * (e.g. data-format disks with non-zero reserved-track offsets). */
    if (!d->inserted || side >= d->sides) return NULL;
    int t = disk_media_track(d);
    if (t >= d->track_count) return NULL;
    DiskTrack *tr = &d->track[t][side];
    for (int i = 0; i < tr->sector_count; i++) {
        DiskSector *s = &tr->sectors[i];
        if (s->R == R && s->N == N)
            return s;
    }
    return NULL;
}
