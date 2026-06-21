#pragma once
#include "types.h"
#include "disk.h"

/*
 * NEC uPD765A floppy controller — PCW port mapping.
 *
 * The PCW exposes the FDC on Z80 ports:
 *   0x00 R/W   Main Status Register / Data Register
 *   0x01 R/W   Data Register
 *
 * (On the CPC the same chip lives at 0xFB7E/0xFB7F. Only the port
 * decode differs.) The PCW also has a "terminal count" toggle that
 * is driven by the ASIC at port 0xF8 commands 0x05/0x06; see
 * fdc_set_terminal_count below.
 *
 * Today this is a STUB: it presents a "ready, no operation in
 * progress" main status register so the firmware's polling loops
 * don't hang, but no actual disk I/O is performed. Real command
 * dispatch + disk-image backing is wired in a follow-up.
 */

typedef struct Fdc {
    Disk drive[2];      /* A and B */
    bool tc;            /* terminal-count line (driven by ASIC port 0xF8) */
} Fdc;

void fdc_init (Fdc *f);
void fdc_reset(Fdc *f);

/* Port-mapped I/O — called from pcw.c io_read/io_write. */
u8   fdc_read (Fdc *f, u8 port);
void fdc_write(Fdc *f, u8 port, u8 val);

/* Driven by the ASIC: TC=1 aborts the current transfer. */
void fdc_set_terminal_count(Fdc *f, bool on);
