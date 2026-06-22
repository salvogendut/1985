#include "bootstrap.h"
#include <string.h>

/* PCW boot ROM — verbatim copy of the real PCW8256/PCW8512 boot ROM
 * (275 bytes). This is what the real machine loads from the printer-
 * MCU ROM into low RAM at reset. We use this instead of a hand-crafted
 * stub because the LOADED CP/M+ BIOS depends on the precise state the
 * real ROM leaves the FDC and registers in.
 *
 * Source: identical bytes to ZEsarUX's pcw_boot.rom (and MAME's PCW
 * printer-MCU ROM dump). 275 bytes. The first instruction is
 * JP 0x0102, so the entry point is at offset 0x0102 in this image.
 */
static const u8 PCW_BOOT_ROM[] = {
    0xc3, 0x02, 0x01, 0xf3, 0x83, 0xed, 0x41, 0x0d, 0x78, 0x05, 0x87, 0x20, 0xf8, 0x31, 0xf0, 0xff,
    0x3e, 0x09, 0xd3, 0xf8, 0x11, 0x32, 0x07, 0x06, 0xc8, 0xdc, 0xb1, 0x00, 0xcd, 0x84, 0x00, 0x1d,
    0xf2, 0x17, 0x00, 0x3e, 0x80, 0xd3, 0xf7, 0x0e, 0x09, 0x0c, 0x79, 0xd3, 0xf8, 0x06, 0x21, 0xcd,
    0xb1, 0x00, 0xcb, 0x51, 0x20, 0xf1, 0x15, 0x20, 0xf0, 0x21, 0xf5, 0xff, 0x77, 0xcb, 0x7e, 0x28,
    0xfc, 0x3c, 0xd3, 0xf8, 0x1d, 0x1d, 0x3e, 0x06, 0xd3, 0xf8, 0xcd, 0xe4, 0x00, 0x09, 0x66, 0x00,
    0x00, 0x00, 0x01, 0x02, 0x01, 0x2a, 0xff, 0x21, 0x00, 0xf0, 0xdb, 0x00, 0x87, 0x30, 0xfb, 0x87,
    0xf2, 0x6b, 0x00, 0xed, 0xa2, 0x20, 0xf3, 0x7c, 0x1f, 0x38, 0xef, 0x3e, 0x05, 0xd3, 0xf8, 0xcd,
    0xc7, 0x00, 0xe6, 0xcb, 0xc0, 0x47, 0x21, 0x10, 0xf0, 0x24, 0x86, 0x25, 0x86, 0x2c, 0x10, 0xf9,
    0x3c, 0x20, 0xa0, 0xe9, 0x0e, 0x80, 0xcd, 0xdb, 0x00, 0x05, 0x03, 0x0f, 0xff, 0x07, 0x00, 0xcd,
    0xa5, 0x00, 0x06, 0xc8, 0x38, 0x1b, 0xcd, 0x44, 0x00, 0xcd, 0x44, 0x00, 0x0e, 0x00, 0xcd, 0xdb,
    0x00, 0x03, 0x0f, 0x00, 0x14, 0xcd, 0xbd, 0x00, 0x30, 0xfb, 0x17, 0x38, 0xf8, 0x17, 0xd8, 0x06,
    0x14, 0x3e, 0xb3, 0xe3, 0xe3, 0xe3, 0xe3, 0x3d, 0x20, 0xf9, 0x10, 0xf5, 0xc9, 0xdb, 0xf8, 0xe6,
    0x20, 0xc8, 0xcd, 0xe9, 0x00, 0x01, 0x08, 0x21, 0x02, 0x01, 0xdb, 0x00, 0x87, 0x30, 0xfb, 0x3a,
    0x02, 0x01, 0xf0, 0xed, 0xa2, 0xe3, 0xe3, 0xe3, 0xe3, 0x18, 0xef, 0xdb, 0xf8, 0xe6, 0x40, 0x28,
    0xfa, 0x79, 0xd3, 0xf7, 0xcd, 0xbd, 0x00, 0x38, 0xfb, 0xe3, 0x46, 0x23, 0xe3, 0x0e, 0x01, 0xe3,
    0xdb, 0x00, 0x87, 0x30, 0xfb, 0xfa, 0xfb, 0x00, 0x7e, 0xed, 0x79, 0x23, 0xe3, 0xe3, 0xe3, 0x10,
    0xee, 0xc9, 0xaf, 0xd3, 0xf0, 0x01, 0x00, 0x00, 0x3e, 0xd3, 0x02, 0x03, 0x3e, 0xf8, 0x02, 0xaf,
    0xc3, 0x00, 0x00
};

/*
 * PCW boot loader (hand-assembled Z80).
 *
 * Overlays low memory at reset so the CPU executes this instead of
 * empty RAM. Reads sector R=1 of track 0 from drive A into 0xF000,
 * disables the bootstrap, and jumps to 0xF010 (the first 16 bytes
 * of the loaded sector are a CP/M+ boot header, not code — see
 * https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwboot.html).
 *
 * Pseudocode:
 *
 *   DI
 *   LD SP, 0x7E00
 *   OUT (0xF8), 9   ; motor on
 *   SPECIFY (3 bytes)
 *   RECALIBRATE drive 0
 *   SENSE INT (drain seek-end result)
 *   READ DATA  C=0 H=0 R=1 N=2 EOT=1 GPL=2A DTL=FF
 *   loop: copy data port -> (HL++) while NDM set
 *   drain result bytes
 *   OUT (0xF8), 0   ; ASIC cmd 0 = bootstrap done
 *   JP 0xF000
 *
 * Helpers (subroutines): send_cmd, drain_result.
 *
 * MSR bits the loader watches:
 *   0x80 RQM   host may transfer a byte
 *   0x20 NDM   execution phase in progress
 *   0x10 BUSY  command in progress (clears at end of result phase)
 */

static u8  g_stream[1024];
static int g_stream_len;

#define EMIT(...) do {                                       \
    const u8 _b[] = { __VA_ARGS__ };                         \
    memcpy(&g_stream[g_stream_len], _b, sizeof(_b));         \
    g_stream_len += (int)sizeof(_b);                         \
} while (0)

static void build_stream(void) {
    g_stream_len = 0;
    memset(g_stream, 0, sizeof(g_stream));

    EMIT(0xF3);                         /* DI */
    EMIT(0x31, 0x00, 0x7E);             /* LD SP, 0x7E00 */
    EMIT(0x3E, 0x09, 0xD3, 0xF8);       /* OUT (0xF8), 9 = motor on */

    /* SPECIFY 03 D1 03 (SRT=D, HUT=1, HLT=1, ND=1). */
    EMIT(0x3E, 0x03, 0xD3, 0x01);
    EMIT(0x3E, 0xD1, 0xD3, 0x01);
    EMIT(0x3E, 0x03, 0xD3, 0x01);

    /* RECALIBRATE 07 00 (drive 0). */
    EMIT(0x3E, 0x07, 0xD3, 0x01);
    EMIT(0xAF, 0xD3, 0x01);

    /* SENSE INT 08, then drain result bytes. */
    EMIT(0x3E, 0x08, 0xD3, 0x01);
    int call_drain_1 = g_stream_len;
    EMIT(0xCD, 0x00, 0x00);             /* CALL drain_result (patched) */

    /* Send READ DATA (9 bytes). */
    int ld_hl_read = g_stream_len;
    EMIT(0x21, 0x00, 0x00);             /* LD HL, read_cmd (patched) */
    EMIT(0x06, 0x09);                   /* LD B, 9 */
    int call_send = g_stream_len;
    EMIT(0xCD, 0x00, 0x00);             /* CALL send_cmd (patched) */

    /* LD HL, 0xF000 (destination). */
    EMIT(0x21, 0x00, 0xF0);

    /* Copy loop. */
    int copy = g_stream_len;
    EMIT(0xDB, 0x00);                   /* IN A, (0)   ; MSR */
    EMIT(0xCB, 0x6F);                   /* BIT 5, A    ; NDM */
    int jr_done = g_stream_len + 1;
    EMIT(0x28, 0x00);                   /* JR Z, copy_done (patched) */
    EMIT(0xCB, 0x7F);                   /* BIT 7, A    ; RQM */
    EMIT(0x28, (u8)(copy - (g_stream_len + 2))); /* JR Z, copy */
    EMIT(0xDB, 0x01);                   /* IN A, (1) */
    EMIT(0x77);                         /* LD (HL), A */
    EMIT(0x23);                         /* INC HL */
    EMIT(0x18, (u8)(copy - (g_stream_len + 2))); /* JR copy */

    int copy_done = g_stream_len;
    g_stream[jr_done] = (u8)(copy_done - (jr_done + 1));

    /* Drain the result bytes. */
    int call_drain_2 = g_stream_len;
    EMIT(0xCD, 0x00, 0x00);             /* CALL drain_result (patched) */

    /* Disable bootstrap and jump to loaded sector. */
    EMIT(0xAF);                         /* XOR A */
    EMIT(0xD3, 0xF8);                   /* OUT (0xF8), A */
    /* Per the PCW boot spec (https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwboot.html):
     * boot sector loads to 0xF000..0xF1FF but execution starts at 0xF010 —
     * the first 16 bytes are a CP/M+ boot-sector header, not code. */
    EMIT(0xC3, 0x10, 0xF0);             /* JP 0xF010 */

    /* --- send_cmd: HL=cmd bytes, B=count. --- */
    int send_cmd = g_stream_len;
    EMIT(0xDB, 0x00);                   /* IN A, (0) */
    EMIT(0xE6, 0x80);                   /* AND 0x80 */
    EMIT(0x28, (u8)(send_cmd - (g_stream_len + 2))); /* JR Z, send_cmd */
    EMIT(0x7E);                         /* LD A, (HL) */
    EMIT(0xD3, 0x01);                   /* OUT (1), A */
    EMIT(0x23);                         /* INC HL */
    EMIT(0x10, (u8)(send_cmd - (g_stream_len + 2))); /* DJNZ send_cmd */
    EMIT(0xC9);                         /* RET */

    /* --- drain_result: read result bytes until BUSY clears. --- */
    int drain = g_stream_len;
    EMIT(0xDB, 0x00);                   /* IN A, (0) */
    EMIT(0xCB, 0x67);                   /* BIT 4, A    ; BUSY */
    EMIT(0xC8);                         /* RET Z */
    EMIT(0xE6, 0x80);                   /* AND 0x80    ; RQM */
    EMIT(0x28, (u8)(drain - (g_stream_len + 2))); /* JR Z, drain */
    EMIT(0xDB, 0x01);                   /* IN A, (1) */
    EMIT(0x18, (u8)(drain - (g_stream_len + 2))); /* JR drain */

    /* --- read_cmd: READ DATA parameter block. --- */
    int read_cmd_off = g_stream_len;
    EMIT(0x46, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x2A, 0xFF);

    /* Patch absolute addresses now that all labels are known. */
    g_stream[call_drain_1 + 1] = (u8)(drain & 0xFF);
    g_stream[call_drain_1 + 2] = (u8)(drain >> 8);
    g_stream[call_drain_2 + 1] = (u8)(drain & 0xFF);
    g_stream[call_drain_2 + 2] = (u8)(drain >> 8);
    g_stream[call_send    + 1] = (u8)(send_cmd & 0xFF);
    g_stream[call_send    + 2] = (u8)(send_cmd >> 8);
    g_stream[ld_hl_read   + 1] = (u8)(read_cmd_off & 0xFF);
    g_stream[ld_hl_read   + 2] = (u8)(read_cmd_off >> 8);
}

void bootstrap_init(Bootstrap *b) {
    memset(b, 0, sizeof(*b));
    bootstrap_reset(b);
}

void bootstrap_reset(Bootstrap *b) {
    /* Use the real PCW boot ROM (275 bytes), not our hand-crafted stub.
     * The hand-crafted version reads only sector 1 of track 0 and jumps
     * to 0xF010, but the loaded CP/M+ BIOS depends on the precise FDC
     * state the real ROM produces (multiple sectors loaded with SENSE_INT
     * between each, specific bank programming, etc.). Using the real ROM
     * matches ZEsarUX's behavior, which boots the same disks to A>. */
    b->len    = (int)sizeof(PCW_BOOT_ROM);
    b->active = true;
    memcpy(b->stream, PCW_BOOT_ROM, sizeof(PCW_BOOT_ROM));
    (void)build_stream;  /* keep the old function around for reference */
}

bool bootstrap_active(const Bootstrap *b) {
    return b->active;
}

u8 bootstrap_read(const Bootstrap *b, u16 addr) {
    if (addr < b->len) return b->stream[addr];
    return 0x00;
}

void bootstrap_finish(Bootstrap *b) {
    b->active = false;
}
