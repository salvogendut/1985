#include "fdc.h"
#include "leds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Main status register bits. */
#define MSR_RQM   0x80    /* request for master — host may transfer */
#define MSR_DIO   0x40    /* 1 = FDC -> host */
#define MSR_NDM   0x20    /* non-DMA execution */
#define MSR_BUSY  0x10    /* command in progress */

/* ST0 result bits. */
#define ST0_IC_NORMAL    0x00
#define ST0_IC_ABNORMAL  0x40
#define ST0_IC_INVALID   0x80
#define ST0_SE           0x20  /* seek end */
#define ST0_EC           0x10  /* equipment check */
#define ST0_NR           0x08  /* not ready */
#define ST0_HD           0x04  /* head address */
#define ST0_US_MASK      0x03

/* Per-opcode (low 5 bits) parameter-byte counts including the opcode itself.
 * 0 = invalid opcode.
 * Reference: lib765 765fdc.c. */
static const u8 bytes_in_cmd[32] = {
    [0x02] = 9,   /* READ TRACK */
    [0x03] = 3,   /* SPECIFY */
    [0x04] = 2,   /* SENSE DRIVE STATUS */
    [0x05] = 9,   /* WRITE DATA */
    [0x06] = 9,   /* READ DATA */
    [0x07] = 2,   /* RECALIBRATE */
    [0x08] = 1,   /* SENSE INTERRUPT STATUS */
    [0x09] = 9,   /* WRITE DELETED DATA */
    [0x0A] = 2,   /* READ ID */
    [0x0C] = 9,   /* READ DELETED DATA */
    [0x0D] = 6,   /* FORMAT TRACK */
    [0x0F] = 3,   /* SEEK */
    [0x11] = 9,   /* SCAN EQUAL */
    [0x19] = 9,   /* SCAN LOW-OR-EQUAL */
    [0x1D] = 9,   /* SCAN HIGH-OR-EQUAL */
};

/* PCW 8256-era firmware probes drive 2/3 as aliases of 0/1. We only
 * model two physical drives, so decode to the low bit before touching
 * the drive array. */
static int decode_unit(u8 raw_unit) {
    return raw_unit & 0x01;
}

/* MSR per phase. */
static void enter_idle(Fdc *f) {
    f->phase = FDC_PHASE_IDLE;
    f->msr   = MSR_RQM;
    f->cmd_pos = f->cmd_len = 0;
    f->exec_pos = f->exec_len = 0;
    f->result_pos = f->result_len = 0;
}

static void enter_command(Fdc *f) {
    f->phase = FDC_PHASE_COMMAND;
    f->msr   = MSR_RQM | MSR_BUSY;
}

/* Approximate lib765 SHORT_TIMEOUT — host should have time to arm its
 * NMI wait before IRQ fires. Counted in asic_poll calls, which run
 * once per z80_step (~ one Z80 instruction). 100 polls ≈ 100 insts ≈
 * 400 cycles ≈ 100us at 4 MHz; lib765 uses 1000 machine cycles which
 * corresponds to ~250 our polls if z80 avgs ~4 cycles per inst. */
#define FDC_IRQ_ARM_DELAY  64

static void enter_exec_read(Fdc *f) {
    f->phase = FDC_PHASE_EXEC_READ;
    f->msr   = MSR_RQM | MSR_DIO | MSR_NDM | MSR_BUSY;
    /* INTRQ rises after a short delay so the host has time to set
     * up its NMI/wait. asic_poll decrements irq_arm_ticks and sets
     * irq=true + bumps pulse_count when it reaches 0. */
    f->irq_arm_ticks = FDC_IRQ_ARM_DELAY;
}

static void enter_exec_write(Fdc *f) {
    f->phase = FDC_PHASE_EXEC_WRITE;
    f->msr   = MSR_RQM | MSR_NDM | MSR_BUSY;
    f->irq_arm_ticks = FDC_IRQ_ARM_DELAY;
}

static void trace_result(Fdc *f);

static void enter_result(Fdc *f) {
    f->phase = FDC_PHASE_RESULT;
    f->msr   = MSR_RQM | MSR_DIO | MSR_BUSY;
    f->result_pos = 0;
    /* If we came from EXEC (where irq was already armed/asserted) the
     * line stays high through the transition; the host drops it on
     * first RESULT byte read. If we came directly (e.g. simple
     * commands like SENSE INT), arm the delay so the host has time to
     * set up its wait. */
    if (!f->irq && f->irq_arm_ticks == 0) {
        f->irq_arm_ticks = FDC_IRQ_ARM_DELAY;
    }
    trace_result(f);
}

/* Push a single result byte (used by trivial command handlers). */
static void result_push(Fdc *f, u8 b) {
    if (f->result_len < FDC_RESULT_BUF_LEN)
        f->result_buf[f->result_len++] = b;
}

/* Build ST0 from current unit/head + an interrupt code. */
static u8 build_st0(Fdc *f, u8 ic) {
    return (u8)(ic
              | (f->cur_head ? ST0_HD : 0)
              | (f->cur_unit & ST0_US_MASK));
}

/* ---- Command handlers ---- */

/* SPECIFY: stash timing parameters (we ignore them — we don't model
 * step-rate, head-load/unload, or DMA mode). No result, no interrupt. */
static void cmd_specify(Fdc *f) {
    enter_idle(f);
}

/* SENSE DRIVE STATUS: 1-byte result (ST3). */
static void cmd_sense_drive_status(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);
    const Disk *d = &f->drive[f->cur_unit];
    bool ready = d->inserted && f->motor_on;

    u8 st3 = (u8)((f->cur_unit & 0x03)
                | (f->cur_head ? 0x04 : 0)
                | (d->sides > 1 ? 0x08 : 0)
                | (f->cur_cyl[f->cur_unit] == 0 ? 0x10 : 0)
                | (ready ? 0x20 : 0)
                | (d->write_protected ? 0x40 : 0));
    f->result_len = 0;
    result_push(f, st3);
    enter_result(f);
}

/* RECALIBRATE: seek head to track 0, raise seek-end interrupt. No result. */
static void cmd_recalibrate(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_cyl[f->cur_unit] = 0;
    f->drive[f->cur_unit].cur_track = 0;
    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);
    f->st0 = (u8)(ST0_IC_NORMAL | ST0_SE | (f->cur_unit & ST0_US_MASK));
    if (!f->drive[f->cur_unit].inserted) f->st0 |= ST0_NR;
    f->int_pending = true;
    /* SEEK end interrupt fires after a delay (real seek takes time).
     * lib765 uses LONGER_TIMEOUT here, but SHORT works for our 0-step
     * disk model. */
    f->irq_arm_ticks = FDC_IRQ_ARM_DELAY;
    enter_idle(f);
}

/* SEEK: move head to NCN, raise seek-end interrupt. No result. */
static void cmd_seek(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    int ncn = f->cmd_buf[2];
    if (ncn >= DISK_MAX_TRACKS) ncn = DISK_MAX_TRACKS - 1;
    f->cur_cyl[f->cur_unit] = ncn;
    f->drive[f->cur_unit].cur_track = ncn;
    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);
    f->st0 = (u8)(ST0_IC_NORMAL | ST0_SE
               | (f->cur_head ? ST0_HD : 0)
               | (f->cur_unit & ST0_US_MASK));
    if (!f->drive[f->cur_unit].inserted) f->st0 |= ST0_NR;
    f->int_pending = true;
    f->irq_arm_ticks = FDC_IRQ_ARM_DELAY;
    enter_idle(f);
}

/* SENSE INTERRUPT STATUS: consumes a pending seek-end interrupt and
 * returns ST0 + PCN. If no interrupt is pending, returns ST0 = 0x80
 * (invalid command) per the datasheet — and does NOT raise INTRQ. */
static void cmd_sense_interrupt(Fdc *f) {
    f->result_len = 0;
    if (!f->int_pending) {
        /* Invalid SENSE INT (no pending IRQ): per uPD765A datasheet,
         * return ST0=0x80 via the data register but do NOT assert
         * INTRQ. We must not go through enter_result() because that
         * raises f->irq, which would spuriously re-trigger the BIOS
         * IRQ handler when there's nothing to handle. */
        result_push(f, ST0_IC_INVALID);
        f->phase = FDC_PHASE_RESULT;
        f->msr   = MSR_RQM | MSR_DIO | MSR_BUSY;
        f->result_pos = 0;
        return;
    }
    f->int_pending = false;
    result_push(f, f->st0);
    result_push(f, (u8)f->cur_cyl[f->st0 & ST0_US_MASK]);
    enter_result(f);
}

/* Push the 7-byte standard read/write result: ST0, ST1, ST2, C, H, R, N. */
static void push_rw_result(Fdc *f, u8 st0, u8 st1, u8 st2,
                           u8 C, u8 H, u8 R, u8 N) {
    f->result_len = 0;
    result_push(f, st0);
    result_push(f, st1);
    result_push(f, st2);
    result_push(f, C);
    result_push(f, H);
    result_push(f, R);
    result_push(f, N);
}

/* READ ID: return the next sector's CHRN from the current track.
 * Rotates through the track's sector table on successive calls. */
static void cmd_read_id(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    Disk *d = &f->drive[f->cur_unit];

    if (!d->inserted || f->cur_head >= d->sides
        || d->cur_track >= d->track_count) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL) | ST0_NR,
                       0, 0, 0, 0, 0, 0);
        enter_result(f);
        leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);
        return;
    }

    DiskTrack *tr = &d->track[d->cur_track][f->cur_head];
    if (tr->sector_count == 0) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL),
                       0x01 /* ST1 MA = missing address mark */,
                       0, 0, 0, 0, 0);
        enter_result(f);
        leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);
        return;
    }

    int idx = f->last_id_read[f->cur_unit] % tr->sector_count;
    f->last_id_read[f->cur_unit] = idx + 1;
    DiskSector *s = &tr->sectors[idx];
    push_rw_result(f, build_st0(f, ST0_IC_NORMAL), 0, 0,
                   s->C, s->H, s->R, s->N);
    enter_result(f);
    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);
}

/* READ DATA: stream one sector's data through exec phase.
 * Multi-sector and multi-track are deferred — we stop after the first
 * matched sector or when TC is asserted. */
static void cmd_read_data(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    u8 C = f->cmd_buf[2];
    u8 H = f->cmd_buf[3];
    u8 R = f->cmd_buf[4];
    u8 N = f->cmd_buf[5];
    Disk *d = &f->drive[f->cur_unit];

    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);

    if (!d->inserted) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL) | ST0_NR,
                       0, 0, C, H, R, N);
        enter_result(f);
        return;
    }

    DiskSector *s = disk_find_sector(d, f->cur_head, C, H, R, N);
    if (!s) {
        /* ST1 ND = no data (sector not found). */
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL), 0x04, 0,
                       C, H, R, N);
        enter_result(f);
        return;
    }

    /* uPD765A datasheet: when the sector's on-disk C / H differ from
     * the C / H in the command, set ST2.WC (Wrong Cylinder, bit 4)
     * or ST2.BC (Bad Cylinder, bit 1) for special C=0xFF. PCW CP/M+
     * BIOS uses ST2.WC to know "we still read the sector but the
     * disk's C field disagrees"; without WC set, BIOS interprets a
     * successful read with mismatched C as "geometry weird, refuse
     * to write" and bails out with "Disc unsuitable" / "make file
     * non-recoverable". This happens for every data disk where the
     * sector C field stores the physical track number while BIOS
     * commands a CP/M-relative track. */
    /* No ST2.WC: PCW BIOS doesn't tolerate it. */

    DiskTrack *tr = &d->track[d->cur_track][f->cur_head];
    int size = s->size;
    if (size > FDC_EXEC_BUF_LEN) size = FDC_EXEC_BUF_LEN;
    memcpy(f->exec_buf, &tr->data[s->offset], (size_t)size);
    f->exec_len = size;
    f->exec_pos = 0;

    /* DIAG: dump first 16 bytes of sector data we're about to deliver. */
    if (f->trace) {
        fprintf(stderr, "fdc read C%02X H%X R%X size=%d data:",
                C, H, R, size);
        for (int i = 0; i < 16 && i < size; i++)
            fprintf(stderr, " %02X", f->exec_buf[i]);
        fputc('\n', stderr);
    }

    /* Don't overwrite C/H with the disk's values: BIOS expects the
     * next-sector CHRN in the result phase to progress relative to
     * the COMMANDED C/H (so e.g. a multi-sector read crossing a track
     * boundary increments BIOS-side C, not on-disk C). When commanded
     * C differs from on-disk C (data-format disks with OFF, where
     * DISCKIT3 records C=physical but BIOS commands C=cp/m_track),
     * echoing on-disk C breaks BIOS's progress tracking and CP/M
     * mis-reports the read as failed. */

    enter_exec_read(f);
}

/* When EXEC_READ drains (or TC fires), assemble the standard read result.
 *
 * Per uPD765A datasheet AND MAME upd765.cpp:2039-2076, the result phase
 * returns CHRN pointing to the NEXT sector after the one just read --
 * NOT the sector that was read. After delivering one sector with TC:
 *   - If R < EOT: R becomes R+1
 *   - If R == EOT: end-of-track: R = 1, C++
 * CP/M+ BIOS relies on this to track its multi-sector read progress;
 * if we echo the original R, BIOS thinks no progress was made and
 * loops forever processing the same sector. */
static void finish_read_data(Fdc *f) {
    u8 C   = f->cmd_buf[2];
    u8 H   = f->cmd_buf[3];
    u8 R   = f->cmd_buf[4];
    u8 N   = f->cmd_buf[5];
    u8 EOT = f->cmd_buf[6];
    /* PCW CP/M+ BIOS expects the C++/R=1 end-of-track behaviour
     * (not lib765's R++/no-wrap) — it uses the result-phase C to
     * decide whether to advance the head to a new track. */
    if (R == EOT) {
        R = 1;
        C++;
    } else {
        R++;
    }
    push_rw_result(f, build_st0(f, ST0_IC_NORMAL), 0, 0, C, H, R, N);
}

/* WRITE DATA: same parameter layout as READ DATA. Host streams one
 * sector's worth of bytes into exec_buf; on completion we copy them
 * into the disk image and mark it dirty. */
static void cmd_write_data(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    u8 C = f->cmd_buf[2];
    u8 H = f->cmd_buf[3];
    u8 R = f->cmd_buf[4];
    u8 N = f->cmd_buf[5];
    Disk *d = &f->drive[f->cur_unit];

    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);

    if (!d->inserted) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL) | ST0_NR,
                       0, 0, C, H, R, N);
        enter_result(f);
        return;
    }
    if (d->write_protected) {
        /* ST1 NW = not writable. */
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL), 0x02, 0,
                       C, H, R, N);
        enter_result(f);
        return;
    }

    DiskSector *s = disk_find_sector(d, f->cur_head, C, H, R, N);
    if (!s) {
        /* ST1 ND = no data. */
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL), 0x04, 0,
                       C, H, R, N);
        enter_result(f);
        return;
    }

    /* Same WC handling as cmd_read_data — set ST2.WC if the on-disk
     * sector C differs from the commanded C. Without this CP/M+ BIOS
     * sees writes succeed but on the "wrong" cylinder by its bookkeeping
     * and refuses subsequent writes. */
    /* No ST2.WC: PCW BIOS doesn't tolerate it. */

    int size = s->size;
    if (size > FDC_EXEC_BUF_LEN) size = FDC_EXEC_BUF_LEN;
    f->exec_len = size;
    f->exec_pos = 0;

    enter_exec_write(f);
}

/* When EXEC_WRITE fills (or TC fires), commit bytes back into the
 * sector and assemble the result. Partial writes (TC mid-sector) are
 * still committed up to exec_pos. */
static void finish_write_data(Fdc *f) {
    Disk *d = &f->drive[f->cur_unit];
    DiskSector *s = disk_find_sector(d, f->cur_head,
                                     f->cmd_buf[2], f->cmd_buf[3],
                                     f->cmd_buf[4], f->cmd_buf[5]);
    if (s) {
        DiskTrack *tr = &d->track[d->cur_track][f->cur_head];
        int n = f->exec_pos;
        if (n > s->size) n = s->size;
        memcpy(&tr->data[s->offset], f->exec_buf, (size_t)n);
        d->dirty = true;
    }
    /* Same R-increment semantics as finish_read_data — see comment there. */
    u8 C = f->cmd_buf[2], H = f->cmd_buf[3];
    u8 R = f->cmd_buf[4], N = f->cmd_buf[5], EOT = f->cmd_buf[6];
    if (R == EOT) { R = 1; C++; } else { R++; }
    push_rw_result(f, build_st0(f, ST0_IC_NORMAL), 0, 0, C, H, R, N);
}

/* FORMAT TRACK: rewrites the sector ID table for the current track.
 * Command bytes:
 *   [0] opcode 0x0D
 *   [1] MT_MF_SK_HD_US (we decode US, HD; the rest are ignored)
 *   [2] N    bytes-per-sector code (size = 128 << N)
 *   [3] SC   sectors per track
 *   [4] GPL  gap length (ignored)
 *   [5] D    filler byte for every formatted sector
 * Execution phase: host streams SC * 4 bytes (C, H, R, N per sector).
 * On EXEC end (finish_format_track): rebuild the track's sector
 * table, fill the data block with D, mark the disk dirty. */
static void cmd_format_track(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    u8 N  = f->cmd_buf[2];
    u8 SC = f->cmd_buf[3];
    Disk *d = &f->drive[f->cur_unit];

    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);

    if (!d->inserted) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL) | ST0_NR,
                       0, 0, 0, 0, 0, N);
        enter_result(f);
        return;
    }
    if (d->write_protected) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL), 0x02, 0,
                       0, 0, 0, N);
        enter_result(f);
        return;
    }
    if (SC == 0 || SC > DISK_MAX_SECTORS) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL), 0, 0,
                       0, 0, 0, N);
        enter_result(f);
        return;
    }

    /* EXEC_WRITE receives SC * 4 bytes (C, H, R, N per sector). */
    f->exec_len = (int)SC * 4;
    f->exec_pos = 0;
    enter_exec_write(f);
}

static void finish_format_track(Fdc *f) {
    Disk *d = &f->drive[f->cur_unit];
    u8 N  = f->cmd_buf[2];
    u8 SC = f->cmd_buf[3];
    u8 D  = f->cmd_buf[5];
    int sector_size = 128 << (N & 0x07);

    int t = d->cur_track;
    if (t < DISK_MAX_TRACKS && f->cur_head < DISK_MAX_SIDES) {
        DiskTrack *tr = &d->track[t][f->cur_head];
        int total = (int)SC * sector_size;

        free(tr->data);
        tr->data = malloc((size_t)total);
        if (tr->data) {
            memset(tr->data, D, (size_t)total);
            tr->data_size   = total;
            tr->sector_count = SC;

            int got_sectors = f->exec_pos / 4;
            for (int i = 0; i < SC; i++) {
                DiskSector *s = &tr->sectors[i];
                if (i < got_sectors) {
                    u8 *p = &f->exec_buf[i * 4];
                    s->C = p[0]; s->H = p[1]; s->R = p[2]; s->N = p[3];
                } else {
                    /* Host short-changed the format burst (TC fired
                     * early); fall back to a synthetic ID so the
                     * track is at least internally consistent. */
                    s->C = (u8)d->cur_track;
                    s->H = (u8)f->cur_head;
                    s->R = (u8)(i + 1);
                    s->N = N;
                }
                s->st1 = 0;
                s->st2 = 0;
                s->size   = sector_size;
                s->offset = i * sector_size;
            }

            d->dirty = true;
            /* Grow the disk to include this track if FORMAT TRACK has
             * just allocated a track beyond the current geometry. */
            if (t + 1 > d->track_count) d->track_count = t + 1;
            if (f->cur_head + 1 > d->sides) d->sides = f->cur_head + 1;
        }
    }

    /* Result CHRN reflects the last sector formatted, per datasheet. */
    u8 C = (u8)d->cur_track, H = (u8)f->cur_head, R = SC;
    if (f->exec_pos >= 4) {
        u8 *last = &f->exec_buf[(f->exec_pos & ~3) - 4];
        C = last[0]; H = last[1]; R = last[2];
    }
    push_rw_result(f, build_st0(f, ST0_IC_NORMAL), 0, 0, C, H, R, N);
}

/* SCAN EQUAL (0x11), SCAN LOW-OR-EQUAL (0x19), SCAN HIGH-OR-EQUAL (0x1D)
 * share the same command shape and exec-phase data flow. Real chips
 * read each sector and compare bytes against a host-streamed pattern;
 * the result-phase ST2 bits SH (0x08, scan satisfied) and SN (0x04,
 * scan not satisfied) report the outcome.
 *
 * DISCKIT3 uses SCAN EQ to probe the disk before formatting — if the
 * target sector doesn't exist (blank disk) it interprets that as
 * "format from scratch". Without this command at all we return
 * IC=INVALID and DISCKIT3 aborts with "track N sector X — unknown".
 *
 * We don't model byte-level scan-comparison: when the sector exists
 * we still drain the host's data burst (so the protocol stays in
 * sync) and then declare "scan satisfied". No known PCW software
 * relies on the comparison semantics. */
static void cmd_scan_any(Fdc *f) {
    f->cur_unit = decode_unit(f->cmd_buf[1]);
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    u8 C = f->cmd_buf[2];
    u8 H = f->cmd_buf[3];
    u8 R = f->cmd_buf[4];
    u8 N = f->cmd_buf[5];
    Disk *d = &f->drive[f->cur_unit];

    leds_ping(f->cur_unit ? LED_FDC_B : LED_FDC_A);

    if (!d->inserted) {
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL) | ST0_NR,
                       0, 0, C, H, R, N);
        enter_result(f);
        return;
    }

    DiskSector *s = disk_find_sector(d, f->cur_head, C, H, R, N);
    if (!s) {
        /* No matching sector — ST1 ND, ST2 = 0 (no scan flags).
         * DISCKIT3 maps this to "blank track, format anew". */
        push_rw_result(f, build_st0(f, ST0_IC_ABNORMAL), 0x04, 0,
                       C, H, R, N);
        enter_result(f);
        return;
    }

    /* Sector exists — drain the data stream the host wants to compare
     * against, then return "scan satisfied" in finish_scan_any. */
    int size = s->size;
    if (size > FDC_EXEC_BUF_LEN) size = FDC_EXEC_BUF_LEN;
    f->exec_len = size;
    f->exec_pos = 0;
    enter_exec_write(f);
}

static void finish_scan_any(Fdc *f) {
    u8 C = f->cmd_buf[2], H = f->cmd_buf[3];
    u8 R = f->cmd_buf[4], N = f->cmd_buf[5];
    /* ST2 bit 3 = SH (scan satisfied). We always declare a hit when
     * the sector exists since we don't actually compare bytes. */
    push_rw_result(f, build_st0(f, ST0_IC_NORMAL), 0, 0x08,
                   C, H, R, N);
}

static void cmd_invalid(Fdc *f) {
    /* Invalid opcode: a single ST0 result byte with IC=INVALID, no state change. */
    f->result_len = 0;
    result_push(f, ST0_IC_INVALID);
    enter_result(f);
}

static const char *opcode_name(u8 op) {
    switch (op & 0x1F) {
        case 0x02: return "READ TRACK";
        case 0x03: return "SPECIFY";
        case 0x04: return "SENSE DRIVE";
        case 0x05: return "WRITE DATA";
        case 0x06: return "READ DATA";
        case 0x07: return "RECALIBRATE";
        case 0x08: return "SENSE INT";
        case 0x09: return "WRITE DEL";
        case 0x0A: return "READ ID";
        case 0x0C: return "READ DEL";
        case 0x0D: return "FORMAT";
        case 0x0F: return "SEEK";
        case 0x11: return "SCAN EQ";
        case 0x19: return "SCAN LEQ";
        case 0x1D: return "SCAN HEQ";
        default:   return "?";
    }
}

static const char *phase_name(FdcPhase p) {
    switch (p) {
        case FDC_PHASE_IDLE:       return "IDLE";
        case FDC_PHASE_COMMAND:    return "COMMAND";
        case FDC_PHASE_EXEC_READ:  return "EXEC_READ";
        case FDC_PHASE_EXEC_WRITE: return "EXEC_WRITE";
        case FDC_PHASE_RESULT:     return "RESULT";
        default:                   return "?";
    }
}

static void trace_cmd(Fdc *f) {
    if (!f->trace) return;
    fprintf(stderr, "fdc cmd %02X (%s) phase=%s", f->cmd_buf[0], opcode_name(f->cmd_buf[0]),
            phase_name(f->phase));
    for (int i = 1; i < f->cmd_len; i++) fprintf(stderr, " %02X", f->cmd_buf[i]);
    fputc('\n', stderr);
}

static void trace_result(Fdc *f) {
    if (!f->trace || f->result_len == 0) return;
    fprintf(stderr, "fdc res phase=%s", phase_name(f->phase));
    for (int i = 0; i < f->result_len; i++) fprintf(stderr, " %02X", f->result_buf[i]);
    fputc('\n', stderr);
}

static void dispatch_command(Fdc *f) {
    u8 op = f->cmd_buf[0] & 0x1F;
    f->irq = false;
    trace_cmd(f);
    switch (op) {
        case 0x03: cmd_specify           (f); break;
        case 0x04: cmd_sense_drive_status(f); break;
        case 0x07: cmd_recalibrate       (f); break;
        case 0x08: cmd_sense_interrupt   (f); break;
        case 0x0F: cmd_seek              (f); break;

        case 0x05: cmd_write_data  (f); break;
        case 0x06: cmd_read_data   (f); break;
        case 0x0A: cmd_read_id     (f); break;
        case 0x0D: cmd_format_track(f); break;
        case 0x11: cmd_scan_any    (f); break;   /* SCAN EQUAL */
        case 0x19: cmd_scan_any    (f); break;   /* SCAN LOW-OR-EQUAL */
        case 0x1D: cmd_scan_any    (f); break;   /* SCAN HIGH-OR-EQUAL */

        default:
            cmd_invalid(f);
            break;
    }
}

/* ---- Public API ---- */

void fdc_init(Fdc *f) {
    memset(f, 0, sizeof(*f));
    disk_init(&f->drive[0]);
    disk_init(&f->drive[1]);
    enter_idle(f);
}

void fdc_reset(Fdc *f) {
    f->tc = false;
    f->int_pending = false;
    f->st0 = f->st1 = f->st2 = f->st3 = 0;
    enter_idle(f);
}

/* Dispatch the per-command result-assembly when execution ends. */
static void finish_execution(Fdc *f) {
    u8 op = f->cmd_buf[0] & 0x1F;
    switch (op) {
        case 0x05: finish_write_data  (f); break;
        case 0x06: finish_read_data   (f); break;
        case 0x0D: finish_format_track(f); break;
        case 0x11:
        case 0x19:
        case 0x1D: finish_scan_any    (f); break;
        default:   f->result_len = 0;      break;
    }
    enter_result(f);
}

u8 fdc_read(Fdc *f, u8 port) {
    if (port == 0) {
        if (f->trace)
            fprintf(stderr, "fdc msr phase=%s -> %02X\n", phase_name(f->phase), f->msr);
        return f->msr;
    }

    /* Data register read. */
    switch (f->phase) {
        case FDC_PHASE_RESULT: {
            u8 b = (f->result_pos < f->result_len) ? f->result_buf[f->result_pos++] : 0xFF;
            if (f->trace)
                fprintf(stderr, "fdc data phase=RESULT -> %02X\n", b);
            f->irq           = false;   /* first result byte read drops the IRQ line */
            f->irq_arm_ticks = 0;       /* cancel any pending arm-delay so it can't
                                         * spuriously re-raise IRQ after the host
                                         * already drained the byte (race that
                                         * broke level-IRQ boot disks: enter_result
                                         * armed 64 polls, host read the byte
                                         * before they expired, then asic_poll
                                         * fired irq=true with nothing pending). */
            if (f->result_pos >= f->result_len) enter_idle(f);
            return b;
        }
        case FDC_PHASE_EXEC_READ: {
            u8 b = (f->exec_pos < f->exec_len) ? f->exec_buf[f->exec_pos++] : 0xFF;
            if (f->trace)
                fprintf(stderr, "fdc data phase=EXEC_READ -> %02X\n", b);
            /* INTRQ stays HIGH throughout EXEC -- the PCW BIOS NMI
             * handler drains the whole sector in a tight loop on RQM
             * after the single NMI that fired at EXEC entry. Real
             * uPD765A behavior in non-DMA mode: INTRQ stays high
             * until the host reads the first RESULT byte. */
            if (f->exec_pos >= f->exec_len) finish_execution(f);
            return b;
        }
        default:
            if (f->trace)
                fprintf(stderr, "fdc data phase=%s -> FF\n", phase_name(f->phase));
            return 0xFF;
    }
}

void fdc_write(Fdc *f, u8 port, u8 val) {
    if (port == 0) return;   /* MSR is read-only */

    switch (f->phase) {
        case FDC_PHASE_IDLE: {
            u8 op = val & 0x1F;
            f->cmd_len = bytes_in_cmd[op];
            if (f->cmd_len == 0) {
                f->cmd_buf[0] = val;
                cmd_invalid(f);
                return;
            }
            f->cmd_buf[0] = val;
            f->cmd_pos    = 1;
            if (f->cmd_pos >= f->cmd_len) {
                enter_command(f);
                dispatch_command(f);
            } else {
                enter_command(f);
            }
            break;
        }
        case FDC_PHASE_COMMAND: {
            if (f->cmd_pos < FDC_CMD_BUF_LEN)
                f->cmd_buf[f->cmd_pos++] = val;
            if (f->cmd_pos >= f->cmd_len)
                dispatch_command(f);
            break;
        }
        case FDC_PHASE_EXEC_WRITE: {
            if (f->exec_pos < FDC_EXEC_BUF_LEN)
                f->exec_buf[f->exec_pos++] = val;
            /* INTRQ stays HIGH throughout EXEC, same as EXEC_READ. */
            if (f->exec_pos >= f->exec_len) finish_execution(f);
            break;
        }
        default:
            /* Writes during EXEC_READ / RESULT / unexpected phases — drop. */
            break;
    }

}

void fdc_set_motor(Fdc *f, bool on) {
    f->motor_on = on;
}

void fdc_set_terminal_count(Fdc *f, bool on) {
    f->tc = on;
    /* Rising edge during execution ends the transfer early. */
    if (on && (f->phase == FDC_PHASE_EXEC_READ || f->phase == FDC_PHASE_EXEC_WRITE))
        finish_execution(f);
}
