#include "fdc.h"
#include "leds.h"
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

__attribute__((unused))
static void enter_exec_read(Fdc *f) {
    f->phase = FDC_PHASE_EXEC_READ;
    f->msr   = MSR_RQM | MSR_DIO | MSR_NDM | MSR_BUSY;
}

__attribute__((unused))
static void enter_exec_write(Fdc *f) {
    f->phase = FDC_PHASE_EXEC_WRITE;
    f->msr   = MSR_RQM | MSR_NDM | MSR_BUSY;
}

static void enter_result(Fdc *f) {
    f->phase = FDC_PHASE_RESULT;
    f->msr   = MSR_RQM | MSR_DIO | MSR_BUSY;
    f->result_pos = 0;
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

/* ---- Command dispatch — all stubs in milestone 1 ---- */

static void cmd_invalid(Fdc *f) {
    /* Invalid opcode: a single ST0 result byte with IC=INVALID, no state change. */
    f->result_len = 0;
    result_push(f, ST0_IC_INVALID);
    enter_result(f);
}

static void cmd_stub_error(Fdc *f) {
    /* Stub: report abnormal termination. Real handler lands in later milestones. */
    f->cur_unit = f->cmd_buf[1] & 0x03;
    f->cur_head = (f->cmd_buf[1] >> 2) & 0x01;
    f->result_len = 0;
    result_push(f, build_st0(f, ST0_IC_ABNORMAL));
    result_push(f, 0);                  /* ST1 */
    result_push(f, 0);                  /* ST2 */
    result_push(f, f->cmd_buf[2]);      /* C */
    result_push(f, f->cmd_buf[3]);      /* H */
    result_push(f, f->cmd_buf[4]);      /* R */
    result_push(f, f->cmd_buf[5]);      /* N */
    enter_result(f);
}

static void dispatch_command(Fdc *f) {
    u8 op = f->cmd_buf[0] & 0x1F;
    switch (op) {
        /* Phase 2 milestones — handlers land in commits 2..5. */
        case 0x03: /* SPECIFY              */
        case 0x04: /* SENSE DRIVE STATUS   */
        case 0x05: /* WRITE DATA           */
        case 0x06: /* READ DATA            */
        case 0x07: /* RECALIBRATE          */
        case 0x08: /* SENSE INTERRUPT      */
        case 0x0A: /* READ ID              */
        case 0x0F: /* SEEK                 */
            cmd_stub_error(f);
            break;
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

u8 fdc_read(Fdc *f, u8 port) {
    if (port == 0) return f->msr;

    /* Data register read. */
    switch (f->phase) {
        case FDC_PHASE_RESULT: {
            u8 b = (f->result_pos < f->result_len) ? f->result_buf[f->result_pos++] : 0xFF;
            if (f->result_pos >= f->result_len) enter_idle(f);
            return b;
        }
        case FDC_PHASE_EXEC_READ: {
            u8 b = (f->exec_pos < f->exec_len) ? f->exec_buf[f->exec_pos++] : 0xFF;
            if (f->exec_pos >= f->exec_len) enter_result(f);
            return b;
        }
        default:
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
            if (f->exec_pos >= f->exec_len)
                enter_result(f);
            break;
        }
        default:
            /* Writes during EXEC_READ / RESULT / unexpected phases — drop. */
            break;
    }

    /* Pulse the activity LED on any data-port traffic from the host. */
    if (port == 1) leds_ping(LED_FDC_A);
}

void fdc_set_terminal_count(Fdc *f, bool on) {
    f->tc = on;
    /* Rising edge during execution ends the transfer early. */
    if (on && (f->phase == FDC_PHASE_EXEC_READ || f->phase == FDC_PHASE_EXEC_WRITE))
        enter_result(f);
}
