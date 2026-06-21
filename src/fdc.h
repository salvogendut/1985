#pragma once
#include "types.h"
#include "disk.h"

/*
 * NEC uPD765A floppy controller — PCW port mapping.
 *
 * Z80 ports (A0 selects, A1..A6 don't-care, A7=0):
 *   even (A0=0)  R   Main Status Register
 *   even (A0=0)  W   (ignored on real silicon)
 *   odd  (A0=1)  R/W Data Register
 *
 * The ASIC drives the terminal-count line via port 0xF8 commands
 * 0x05 (TC=1) and 0x06 (TC=0); see fdc_set_terminal_count.
 *
 * This is a non-DMA, polled implementation matching what the PCW
 * CP/M+ BIOS expects. The chip runs through three phases:
 *
 *   COMMAND   host writes opcode + 0..8 parameter bytes
 *   EXECUTION host streams data bytes through the Data Register
 *             (direction set by the command); ends when the byte
 *             count is exhausted or TC is asserted
 *   RESULT    host reads 0..7 status/CHRN bytes
 *
 * MSR bits the BIOS polls:
 *   RQM   request for master — host may transfer a byte
 *   DIO   1 = FDC -> host, 0 = host -> FDC
 *   NDM   non-DMA execution in progress
 *   BUSY  command in progress (clears at end of result phase)
 */

typedef enum {
    FDC_PHASE_IDLE = 0,
    FDC_PHASE_COMMAND,
    FDC_PHASE_EXEC_READ,    /* FDC -> host */
    FDC_PHASE_EXEC_WRITE,   /* host -> FDC */
    FDC_PHASE_RESULT,
} FdcPhase;

#define FDC_CMD_BUF_LEN     16
#define FDC_RESULT_BUF_LEN  16
#define FDC_EXEC_BUF_LEN    8192

typedef struct Fdc {
    Disk drive[2];          /* A and B */
    bool tc;                /* terminal-count line (driven by ASIC port 0xF8) */
    bool motor_on;          /* spindle motor (ASIC port 0xF8 cmds 9/10) */
    bool trace;             /* stderr command-and-result trace */

    FdcPhase phase;
    u8       msr;           /* main status register, recomputed per phase */

    u8       cmd_buf[FDC_CMD_BUF_LEN];
    int      cmd_len;       /* expected bytes for current opcode */
    int      cmd_pos;

    u8       exec_buf[FDC_EXEC_BUF_LEN];
    int      exec_len;      /* total bytes in execution phase */
    int      exec_pos;

    u8       result_buf[FDC_RESULT_BUF_LEN];
    int      result_len;
    int      result_pos;

    /* Status latches kept across commands for SENSE INTERRUPT STATUS. */
    u8       st0, st1, st2, st3;
    bool     int_pending;   /* SENSE INTERRUPT consumes this */

    /* Per-drive state. */
    int      cur_cyl[2];    /* head position on each drive */
    int      cur_unit;      /* unit selected by the most recent command */
    int      cur_head;
    int      last_id_read[2]; /* rotating sector index for READ ID */
} Fdc;

void fdc_init (Fdc *f);
void fdc_reset(Fdc *f);

u8   fdc_read (Fdc *f, u8 port);
void fdc_write(Fdc *f, u8 port, u8 val);

void fdc_set_terminal_count(Fdc *f, bool on);
void fdc_set_motor          (Fdc *f, bool on);
