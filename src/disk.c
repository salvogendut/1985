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
    memset(d, 0, sizeof(*d));
    d->cur_track = cur_track;   /* preserve head position across eject */
    d->is_dd_drive = dd;        /* drive type is a hardware property, not media */
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

int disk_create_blank(const char *path) {
    if (!path || !path[0]) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "disk: cannot create %s\n", path);
        return -1;
    }

    /* Pre-formatted PCW data-CF2 disk: 40 tracks × 1 side × 9 sectors
     * of 512 bytes, sector IDs in standard skew order, filler 0xE5,
     * disc-spec at track 0 sector R=1.
     *
     * Why pre-format here rather than relying on DISCKIT3 inside the
     * guest? DISCKIT3 only offers system-format CF2 with OFF=1; PCW
     * CP/M+ BIOS then derives `cp/m_track = physical - OFF` for drive
     * B and commands sector reads with C = cp/m_track, which never
     * matches the C = physical_track DISCKIT3 wrote into the sector
     * IDs. The result is "MAKE FILE NONRECOVERABLE" on any write —
     * see issue #114 for the full analysis (Joyce reproduces it too).
     *
     * The format here uses OFF=0 (no reserved tracks) and writes the
     * sector C field as the physical track. With OFF=0, BIOS commands
     * C = physical_track on every access; the IDs match; writes work.
     *
     * Tracks 0–39 in the standard PCW interleave (1, 6, 2, 7, 3, 8,
     * 4, 9, 5) so seek-stride behaviour matches a real PCW data disk. */

    const int TRACKS   = 40;
    const int SPT      = 9;
    const int SEC_SIZE = 512;
    const int N_CODE   = 2;   /* 128 << 2 = 512 */
    static const uint8_t SKEW[9] = { 1, 6, 2, 7, 3, 8, 4, 9, 5 };
    const int TRACK_SIZE = 256 + SPT * SEC_SIZE;   /* 4864 bytes/track */

    /* Disk-Info: 40 tracks × 1 side, all track sizes = TRACK_SIZE/256. */
    uint8_t track_table[256 - 0x34];
    memset(track_table, 0, sizeof(track_table));
    for (int t = 0; t < TRACKS; t++)
        track_table[t] = (uint8_t)(TRACK_SIZE / 256);
    uint8_t disk_info[256];
    build_disk_info(disk_info, TRACKS, 1, track_table);
    if (fwrite(disk_info, 1, 256, f) != 256) goto err;

    /* PCW disc specification, written into track 0 sector R=1's first
     * 16 bytes. Matches the format DISCKIT3 writes for CF2 *except*
     * byte 5 (reserved tracks) = 0 instead of 1. */
    uint8_t spec[16] = {
        0x00,       /* format byte: PCW SS DD CF2 */
        0x00,       /* sided: single-sided */
        (uint8_t)TRACKS,
        (uint8_t)SPT,
        (uint8_t)N_CODE,
        0x00,       /* reserved (system) tracks — DATA disk, no boot */
        0x03,       /* BSH = 3 → block size 1024 */
        0x02,       /* directory blocks */
        0x00, 0x00,
        0x2A,       /* GAP3 read/write */
        0x52,       /* GAP3 format */
        0x00, 0x00, 0x00, 0x00,
    };

    /* Each track: Track-Info header + SPT × SEC_SIZE bytes of filler. */
    for (int t = 0; t < TRACKS; t++) {
        uint8_t hdr[256];
        memset(hdr, 0, 256);
        memcpy(hdr, "Track-Info\r\n", 12);
        hdr[0x10] = (uint8_t)t;
        hdr[0x11] = 0;            /* side 0 */
        hdr[0x12] = 0x02;         /* data rate = 250 kbps */
        hdr[0x13] = 0x02;         /* recording mode = MFM */
        hdr[0x14] = (uint8_t)N_CODE;
        hdr[0x15] = (uint8_t)SPT;
        hdr[0x16] = 0x4E;         /* GAP3 */
        hdr[0x17] = 0xE5;         /* filler */
        for (int i = 0; i < SPT; i++) {
            uint8_t *si = hdr + 0x18 + i * 8;
            si[0] = (uint8_t)t;             /* C = physical track */
            si[1] = 0;                       /* H */
            si[2] = SKEW[i];                 /* R */
            si[3] = (uint8_t)N_CODE;
            si[4] = 0;                       /* ST1 */
            si[5] = 0;                       /* ST2 */
            si[6] = (uint8_t)(SEC_SIZE & 0xFF);
            si[7] = (uint8_t)((SEC_SIZE >> 8) & 0xFF);
        }
        if (fwrite(hdr, 1, 256, f) != 256) goto err;

        /* Sector data, in record order (R=1, R=6, R=2, ...). The
         * spec sector (track 0 R=1, which is record 0) gets the
         * 16-byte spec then fills with 0xE5; all other sectors are
         * pure filler. */
        for (int i = 0; i < SPT; i++) {
            uint8_t sec[SEC_SIZE];
            memset(sec, 0xE5, SEC_SIZE);
            if (t == 0 && SKEW[i] == 1)
                memcpy(sec, spec, sizeof(spec));
            if (fwrite(sec, 1, SEC_SIZE, f) != SEC_SIZE) goto err;
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
