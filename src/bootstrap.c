#include "bootstrap.h"
#include <string.h>

/*
 * PCW boot loader (hand-assembled Z80).
 *
 * Overlays low memory at reset so the CPU executes this instead of
 * empty RAM. Reads sector R=1 of track 0 from drive A into 0xF000,
 * disables the bootstrap, and jumps to 0xF000.
 *
 * Pseudocode:
 *
 *   DI
 *   LD SP, 0x7E00
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
    EMIT(0xC3, 0x00, 0xF0);             /* JP 0xF000 */

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
    build_stream();
    b->len    = g_stream_len;
    b->active = true;
    memcpy(b->stream, g_stream, (size_t)g_stream_len);
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
