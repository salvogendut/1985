/* snap_analyze.c — offline analyzer for 1985 snapshot files.
 *
 * Builds the 64 KB CPU-visible memory from snapshot + bank config, then
 * dumps registers, stack walk, key memory regions, and disassembles
 * around interesting PCs.
 *
 * Compile:
 *   gcc -std=c11 -I src -o tools/snap_analyze tools/snap_analyze.c src/z80dis.c
 * Run:
 *   ./tools/snap_analyze /tmp/vdu_hung.sna
 */

#include "../src/types.h"
#include "../src/z80dis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAGIC "1985SNA"

typedef struct {
    u16 af, bc, de, hl, ix, iy, sp, pc;
    u16 af_, bc_, de_, hl_;
    u8  i, r, im, iff1, iff2;
    u8  read_bank[4], write_bank[4], bank_force;
    u8  roller_base, scroll_y, display_ctrl, asic_flags1;
    u8  interrupt_counter, fdc_irq_mode, prev_fdc_irq;
    u8  model;
    u16 ram_kb;
} Snap;

static u16 le16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }

static int parse_header(const u8 *hdr, Snap *s) {
    if (memcmp(hdr, MAGIC, 7) != 0) return -1;
    if (hdr[8] != 1) return -1;
    s->af = le16(&hdr[0x10]);
    s->bc = le16(&hdr[0x12]);
    s->de = le16(&hdr[0x14]);
    s->hl = le16(&hdr[0x16]);
    s->ix = le16(&hdr[0x18]);
    s->iy = le16(&hdr[0x1A]);
    s->sp = le16(&hdr[0x1C]);
    s->pc = le16(&hdr[0x1E]);
    s->af_ = le16(&hdr[0x20]);
    s->bc_ = le16(&hdr[0x22]);
    s->de_ = le16(&hdr[0x24]);
    s->hl_ = le16(&hdr[0x26]);
    s->i = hdr[0x28]; s->r = hdr[0x29]; s->im = hdr[0x2A];
    s->iff1 = hdr[0x2B]; s->iff2 = hdr[0x2C];
    for (int i = 0; i < 4; i++) s->read_bank [i] = hdr[0x30 + i];
    for (int i = 0; i < 4; i++) s->write_bank[i] = hdr[0x34 + i];
    s->bank_force = hdr[0x38];
    s->roller_base = hdr[0x40]; s->scroll_y = hdr[0x41];
    s->display_ctrl = hdr[0x42]; s->asic_flags1 = hdr[0x43];
    s->interrupt_counter = hdr[0x44]; s->fdc_irq_mode = hdr[0x45];
    s->prev_fdc_irq = hdr[0x46];
    s->model = hdr[0x50];
    s->ram_kb = le16(&hdr[0x52]);
    return 0;
}

static void flag_str(u8 f, char *out) {
    out[0] = (f & 0x80) ? 'S' : '-';   /* sign */
    out[1] = (f & 0x40) ? 'Z' : '-';   /* zero */
    out[2] = '-';
    out[3] = (f & 0x10) ? 'H' : '-';
    out[4] = '-';
    out[5] = (f & 0x04) ? 'P' : '-';   /* parity / overflow */
    out[6] = (f & 0x02) ? 'N' : '-';
    out[7] = (f & 0x01) ? 'C' : '-';
    out[8] = '\0';
}

static void dump_regs(const Snap *s) {
    char f[9]; flag_str(s->af & 0xFF, f);
    printf("--- Z80 registers ---\n");
    printf("PC=%04X  SP=%04X  AF=%04X  F=[%s]  BC=%04X  DE=%04X  HL=%04X\n",
           s->pc, s->sp, s->af, f, s->bc, s->de, s->hl);
    printf("IX=%04X  IY=%04X  I=%02X  R=%02X  IM=%u  IFF1=%u  IFF2=%u\n",
           s->ix, s->iy, s->i, s->r, s->im, s->iff1, s->iff2);
    printf("alt AF'=%04X  BC'=%04X  DE'=%04X  HL'=%04X\n",
           s->af_, s->bc_, s->de_, s->hl_);
    printf("read_bank = %02X %02X %02X %02X    write_bank = %02X %02X %02X %02X    force=%02X\n",
           s->read_bank[0], s->read_bank[1], s->read_bank[2], s->read_bank[3],
           s->write_bank[0], s->write_bank[1], s->write_bank[2], s->write_bank[3],
           s->bank_force);
    printf("asic: roller=%02X scroll_y=%02X disp_ctrl=%02X flags=%02X irq_cnt=%02X "
           "fdc_irq_mode=%02X model=%u ram_kb=%u\n",
           s->roller_base, s->scroll_y, s->display_ctrl, s->asic_flags1,
           s->interrupt_counter, s->fdc_irq_mode, s->model, s->ram_kb);
    printf("\n");
}

/* Build 64 KB CPU view from physical RAM + bank config. */
static void build_view(const Snap *s, const u8 *ram, u8 view[65536]) {
    for (int slot = 0; slot < 4; slot++) {
        u8 blk = s->read_bank[slot];
        const u8 *src = ram + (size_t)blk * 16384;
        memcpy(view + slot * 16384, src, 16384);
    }
}

static void hex_dump(const u8 *mem, u16 base, int rows) {
    for (int r = 0; r < rows; r++) {
        u16 a = base + r * 16;
        printf("  %04X:", a);
        for (int c = 0; c < 16; c++) printf(" %02X", mem[(u16)(a + c)]);
        printf("  |");
        for (int c = 0; c < 16; c++) {
            u8 b = mem[(u16)(a + c)];
            putchar((b >= 0x20 && b < 0x7F) ? b : '.');
        }
        printf("|\n");
    }
}

static void disasm(const u8 *view, u16 pc, int lines, const char *tag) {
    printf("--- disasm %s @ %04X ---\n", tag, pc);
    char out[64];
    for (int i = 0; i < lines; i++) {
        printf("  %04X: ", pc);
        for (int j = 0; j < 5 && j < 5; j++)
            printf("%02X ", view[(u16)(pc + j)]);
        int n = z80dis(view, pc, out, sizeof(out));
        printf("  %s\n", out);
        if (n <= 0) break;
        pc += n;
    }
    printf("\n");
}

static void walk_stack(const u8 *view, u16 sp, int depth) {
    printf("--- stack walk (SP=%04X) ---\n", sp);
    for (int i = 0; i < depth; i++) {
        u16 a = (u16)(sp + i * 2);
        u16 v = view[a] | (view[(u16)(a + 1)] << 8);
        printf("  %04X: %04X", a, v);
        /* if the word looks like a return address, decode it */
        if (v >= 0x0100 && v < 0xFC00) {
            char out[64];
            z80dis(view, v, out, sizeof(out));
            printf("   <- %s", out);
        }
        printf("\n");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s SNAPSHOT [PC_HEX ...]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }

    u8 hdr[256];
    if (fread(hdr, 1, 256, f) != 256) { fprintf(stderr, "short header\n"); return 1; }
    Snap s;
    if (parse_header(hdr, &s) < 0) {
        fprintf(stderr, "bad magic / version\n"); return 1;
    }
    size_t ram_bytes = (size_t)s.ram_kb * 1024;
    u8 *ram = malloc(ram_bytes);
    if (!ram) { fprintf(stderr, "oom\n"); return 1; }
    if (fread(ram, 1, ram_bytes, f) != ram_bytes) {
        fprintf(stderr, "short ram\n"); return 1;
    }
    fclose(f);

    u8 view[65536];
    build_view(&s, ram, view);

    dump_regs(&s);

    disasm(view, s.pc, 10, "PC");
    walk_stack(view, s.sp, 16);

    /* Watched VDU memory regions. */
    printf("--- watched memory: 0E80 (semaphore area) ---\n");
    hex_dump(view, 0x0E80, 2);
    printf("\n--- watched memory: 1010 (ISR queue head/tail?) ---\n");
    hex_dump(view, 0x1010, 2);
    printf("\n--- watched memory: 6C30 (b8_setup buffer + coroutine SPs) ---\n");
    hex_dump(view, 0x6C30, 8);
    printf("\n--- watched memory: 0D00 (?) ---\n");
    hex_dump(view, 0x0D00, 2);
    printf("\n");

    /* Coroutine swap SP slots. */
    u16 sp_a = view[0x6C49] | (view[0x6C4A] << 8);
    u16 sp_b = view[0x6C4B] | (view[0x6C4C] << 8);
    printf("--- coroutine SPs ---\n");
    printf("  (6C49) = %04X    (6C4B) = %04X\n", sp_a, sp_b);
    if (sp_a) {
        printf("  stack at (6C49)=%04X:\n", sp_a);
        for (int i = 0; i < 8; i++) {
            u16 a = (u16)(sp_a + i * 2);
            u16 v = view[a] | (view[(u16)(a + 1)] << 8);
            printf("    %04X: %04X\n", a, v);
        }
    }
    if (sp_b) {
        printf("  stack at (6C4B)=%04X:\n", sp_b);
        for (int i = 0; i < 8; i++) {
            u16 a = (u16)(sp_b + i * 2);
            u16 v = view[a] | (view[(u16)(a + 1)] << 8);
            printf("    %04X: %04X\n", a, v);
        }
    }
    printf("\n");

    /* Any extra PCs the user passed on the cmdline. */
    for (int i = 2; i < argc; i++) {
        unsigned long pc = strtoul(argv[i], NULL, 16);
        if (pc <= 0xFFFF) disasm(view, (u16)pc, 12, argv[i]);
    }

    /* Key ISR/dispatch addresses we already know about. */
    disasm(view, 0x0038, 6, "IM1 vector (RST 38)");
    disasm(view, 0x0066, 6, "NMI vector");
    disasm(view, 0x0870, 12, "ISR top");
    disasm(view, 0x0A3A, 12, "semaphore wait");
    disasm(view, 0x51A8, 12, "queue dispatch");
    disasm(view, 0x4D30, 16, "main delay routine");
    disasm(view, 0x4D44, 8, "coroutine swap");

    free(ram);
    return 0;
}
