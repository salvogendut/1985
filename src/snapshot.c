#include "snapshot.h"
#include "pcw.h"
#include "asic.h"
#include "mem.h"
#include "printer.h"
#include <stdio.h>
#include <string.h>

/*
 * 1985 snapshot format (v1)
 *
 * 256-byte header followed by RAM (ram_kb * 1024 bytes) and the
 * per-byte "written" bitmap (ram_kb * 128 bytes). Modelled on 1984's
 * "MV - SNA" v3 layout — there is no standard PCW SNA format, so we
 * roll our own.
 *
 *   0x00  8   magic     "1985SNA\0"
 *   0x08  1   version   1
 *   0x10  16  Z80 main  af bc de hl ix iy sp pc          (LE u16 each)
 *   0x20  8   Z80 alt   af' bc' de' hl'
 *   0x28  1   i
 *   0x29  1   r
 *   0x2A  1   im
 *   0x2B  1   iff1
 *   0x2C  1   iff2
 *   0x30  4   read_bank[4]
 *   0x34  4   write_bank[4]
 *   0x38  1   bank_force
 *   0x40  1   roller_base       (port F5)
 *   0x41  1   scroll_y          (port F6)
 *   0x42  1   display_ctrl      (port F7)
 *   0x43  1   asic flags1
 *                 bit0 display_enabled
 *                 bit1 screen_enabled
 *                 bit2 inverse_video
 *                 bit3 flyback
 *                 bit4 roller_programmed
 *   0x44  1   interrupt_counter
 *   0x45  1   fdc_irq_mode      (0/1/2)
 *   0x46  1   prev_fdc_irq
 *   0x50  1   model             (0=8256, 1=8512, 2=9512)
 *   0x52  2   ram_kb            (LE u16)
 *
 * Skipped intentionally (regenerated or transient): FDC command state,
 * AY synthesiser internals, serial buffers / fds, printer Cairo state,
 * disk image contents (re-mount via Media tab after load).
 */

#define SNAPSHOT_MAGIC   "1985SNA"
#define SNAPSHOT_VERSION 1

static void put16(u8 *p, u16 v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static u16  get16(const u8 *p)  { return (u16)p[0] | ((u16)p[1] << 8); }

int snapshot_save(struct PCW *pcw, const char *path) {
    if (!pcw || !path || !path[0]) return -1;

    u8 hdr[256] = {0};
    memcpy(hdr, SNAPSHOT_MAGIC, 8);
    hdr[8] = SNAPSHOT_VERSION;

    put16(&hdr[0x10], pcw->cpu.af);
    put16(&hdr[0x12], pcw->cpu.bc);
    put16(&hdr[0x14], pcw->cpu.de);
    put16(&hdr[0x16], pcw->cpu.hl);
    put16(&hdr[0x18], pcw->cpu.ix);
    put16(&hdr[0x1A], pcw->cpu.iy);
    put16(&hdr[0x1C], pcw->cpu.sp);
    put16(&hdr[0x1E], pcw->cpu.pc);

    put16(&hdr[0x20], pcw->cpu.af_);
    put16(&hdr[0x22], pcw->cpu.bc_);
    put16(&hdr[0x24], pcw->cpu.de_);
    put16(&hdr[0x26], pcw->cpu.hl_);

    hdr[0x28] = pcw->cpu.i;
    hdr[0x29] = pcw->cpu.r;
    hdr[0x2A] = pcw->cpu.im;
    hdr[0x2B] = pcw->cpu.iff1 ? 1 : 0;
    hdr[0x2C] = pcw->cpu.iff2 ? 1 : 0;

    for (int i = 0; i < 4; i++) hdr[0x30 + i] = pcw->mem.read_bank [i];
    for (int i = 0; i < 4; i++) hdr[0x34 + i] = pcw->mem.write_bank[i];
    hdr[0x38] = pcw->mem.bank_force;

    hdr[0x40] = pcw->asic.roller_base;
    hdr[0x41] = pcw->asic.scroll_y;
    hdr[0x42] = pcw->asic.display_ctrl;
    hdr[0x43] = (u8)((pcw->asic.display_enabled    ? 1 : 0)
                   | (pcw->asic.screen_enabled     ? 2 : 0)
                   | (pcw->asic.inverse_video      ? 4 : 0)
                   | (pcw->asic.flyback            ? 8 : 0)
                   | (pcw->asic.roller_programmed  ? 16 : 0));
    hdr[0x44] = pcw->asic.interrupt_counter;
    hdr[0x45] = (u8)(pcw->asic.fdc_irq_mode & 0xFF);
    hdr[0x46] = pcw->asic.prev_fdc_irq ? 1 : 0;

    hdr[0x50] = (u8)pcw->model;
    put16(&hdr[0x52], (u16)pcw->memory_kb);

    size_t ram_bytes  = (size_t)pcw->memory_kb * 1024;
    size_t bmap_bytes = ram_bytes / 8;

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "snapshot: cannot create '%s'\n", path);
        return -1;
    }
    if (fwrite(hdr, 1, 256, f) != 256
        || fwrite(pcw->mem.ram,         1, ram_bytes,  f) != ram_bytes
        || fwrite(pcw->mem.ram_written, 1, bmap_bytes, f) != bmap_bytes) {
        fprintf(stderr, "snapshot: write to '%s' failed\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    fprintf(stderr, "snapshot: saved '%s' (%d KB, PC=%04X SP=%04X)\n",
            path, pcw->memory_kb, pcw->cpu.pc, pcw->cpu.sp);
    return 0;
}

int snapshot_load(struct PCW *pcw, const char *path) {
    if (!pcw || !path || !path[0]) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "snapshot: cannot open '%s'\n", path);
        return -1;
    }

    u8 hdr[256];
    if (fread(hdr, 1, 256, f) != 256) {
        fprintf(stderr, "snapshot: '%s' header too short\n", path);
        fclose(f);
        return -1;
    }
    if (memcmp(hdr, SNAPSHOT_MAGIC, 8) != 0) {
        fprintf(stderr, "snapshot: '%s' not a 1985 snapshot\n", path);
        fclose(f);
        return -1;
    }
    u8 version = hdr[8];
    if (version != SNAPSHOT_VERSION) {
        fprintf(stderr, "snapshot: '%s' unsupported version %u\n", path, version);
        fclose(f);
        return -1;
    }

    PcwModel sna_model = (PcwModel)hdr[0x50];
    int      sna_kb    = (int)get16(&hdr[0x52]);
    if (sna_kb < 256 || sna_kb > 2048) {
        fprintf(stderr, "snapshot: '%s' bad ram_kb=%d\n", path, sna_kb);
        fclose(f);
        return -1;
    }

    /* If the snapshot was taken on a different model / RAM size, do a
     * cold-boot first so the receiving machine matches. This wipes
     * everything the snapshot is about to restore — exactly what we
     * want before pouring RAM and registers back in. */
    if (sna_model != pcw->model || sna_kb != pcw->memory_kb) {
        printer_shutdown(&pcw->printer);
        pcw_cold_boot(pcw, sna_model, sna_kb);
    }

    /* From here on, only restore fields. Don't reset anything else;
     * the snapshot fully describes the post-cold-boot delta. */
    pcw->cpu.af = get16(&hdr[0x10]);
    pcw->cpu.bc = get16(&hdr[0x12]);
    pcw->cpu.de = get16(&hdr[0x14]);
    pcw->cpu.hl = get16(&hdr[0x16]);
    pcw->cpu.ix = get16(&hdr[0x18]);
    pcw->cpu.iy = get16(&hdr[0x1A]);
    pcw->cpu.sp = get16(&hdr[0x1C]);
    pcw->cpu.pc = get16(&hdr[0x1E]);

    pcw->cpu.af_ = get16(&hdr[0x20]);
    pcw->cpu.bc_ = get16(&hdr[0x22]);
    pcw->cpu.de_ = get16(&hdr[0x24]);
    pcw->cpu.hl_ = get16(&hdr[0x26]);

    pcw->cpu.i   = hdr[0x28];
    pcw->cpu.r   = hdr[0x29];
    pcw->cpu.im  = hdr[0x2A];
    pcw->cpu.iff1 = hdr[0x2B] != 0;
    pcw->cpu.iff2 = hdr[0x2C] != 0;
    pcw->cpu.halted       = false;
    pcw->cpu.pending_irq  = false;
    pcw->cpu.int_accepted = false;
    pcw->cpu.ei_delay     = false;

    for (int i = 0; i < 4; i++) pcw->mem.read_bank [i] = hdr[0x30 + i];
    for (int i = 0; i < 4; i++) pcw->mem.write_bank[i] = hdr[0x34 + i];
    pcw->mem.bank_force = hdr[0x38];

    pcw->asic.roller_base       = hdr[0x40];
    pcw->asic.scroll_y          = hdr[0x41];
    pcw->asic.display_ctrl      = hdr[0x42];
    u8 fl = hdr[0x43];
    pcw->asic.display_enabled    = (fl & 1)  != 0;
    pcw->asic.screen_enabled     = (fl & 2)  != 0;
    pcw->asic.inverse_video      = (fl & 4)  != 0;
    pcw->asic.flyback            = (fl & 8)  != 0;
    pcw->asic.roller_programmed  = (fl & 16) != 0;
    pcw->asic.interrupt_counter  = hdr[0x44];
    pcw->asic.fdc_irq_mode       = (int)(int8_t)hdr[0x45];
    pcw->asic.prev_fdc_irq       = hdr[0x46] != 0;

    /* Past bootstrap by definition — the snapshot is post-boot guest
     * state. Disarming the boot stream keeps mem_read from masking
     * the real RAM contents we're about to write. */
    pcw->mem.bootstrap = NULL;

    size_t ram_bytes  = (size_t)sna_kb * 1024;
    size_t bmap_bytes = ram_bytes / 8;
    if (fread(pcw->mem.ram,         1, ram_bytes,  f) != ram_bytes
     || fread(pcw->mem.ram_written, 1, bmap_bytes, f) != bmap_bytes) {
        fprintf(stderr, "snapshot: '%s' truncated RAM\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Resume — overrides any prior F8-monitor pause. */
    pcw->paused    = false;
    pcw->step_once = false;

    fprintf(stderr, "snapshot: loaded '%s' (v%u, %d KB, PC=%04X SP=%04X)\n",
            path, version, sna_kb, pcw->cpu.pc, pcw->cpu.sp);
    return 0;
}
