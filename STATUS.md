# PCW Boot Investigation Status (issue #12 — `ccp-load` branch)

## Summary

The original sticky-FDC-IRQ-latch bug is fixed and the boot has visibly advanced — CP/M+ now reaches the "Drive is A:" status line and a cursor block. A second, deeper stall remains: CCP never gets invoked.

## What changed in this round

- **`src/asic.{c,h}` — removed sticky FDC IRQ latch** (was the documented primary fix in the plan)
  - Cross-checked MAME (`pcw_fdc_interrupt`, pcw.cpp:196-206) and Joyce (`JoyceAsic::fdcIsr`, JoyceAsic.cxx:89-93). Both treat F8 bit 5 as a LIVE mirror of the FDC INTRQ line, not a sticky latch.
  - Our previous attempt latched the bit on every poll and never cleared it. After the first FDC command, bit 5 stayed high forever; the BIOS dispatcher at 0x077B repeatedly took the "FDC done" branch and never fell through to the timer/work path properly.
  - Fix: dropped `fdc_irq_latched` field. `sys_status()` now derives bit 5 directly from `fdc->irq`.

- **`src/mem.c` — kbd window backed by RAM** (joyce-faithful)
  - Removed the read overlay that returned matrix bytes for the keyboard window.
  - Reads now come from RAM; writes go to RAM (CPU scratch survives between scans).

- **`src/kbd.{c,h}` + `src/pcw.c` — periodic kbd scan into RAM**
  - New `kbd_scan_into_ram()` writes the 16-byte matrix into block 3 offset 0x3FF0 every 300 Hz tick.
  - Added `ticker` field + heartbeat bits on byte 0xBFFF (bit 7 toggles every tick, bit 6 every other), per Joyce JoycePcwKeyboard.cxx:475-478 and Seasip §10 ("3FFFh bit 6 toggles with each update from the keyboard to the PCW. 3FFFh bit 7 is 1 if the keyboard is currently transmitting its state to the PCW, 0 if it is scanning its keys").

- **`src/main.c` — `--dump-at N` diagnostic** (kept for future debugging)
  - Dumps Z80 registers, bank state, ASIC/FDC state, disassembly around PC, disassembly of fixed dispatcher addresses (0x0770, 0x078B, 0x07A4, 0x07E6, 0x0880, 0x08A0, 0x08E9, 0x0AD0, 0x0A98, 0x07D4, 0x0030, 0x0B6A, 0x4734, 0x4E84), and key memory regions including raw block-3 keyboard data.

- **`src/fdc.c` — trace lines include phase name** (small diag improvement)

## What is still broken (current stall)

- Screen shows: `CP/M Plus  Amstrad Consumer Electronics plc / v 1.7, 61K TPA, 1 disc drive, 112K drive M:` + cursor block + `Drive is A:` (bottom right).
- Real-hardware behaviour we are NOT reproducing: between the banner and `A>`, the disk LED blinks briefly while CCP is paged in and executed.

## Evidence gathered

State dumps at frames 4000/6000/8000 (`tools` directory of a fresh checkout: run `./1985 --dump-at <N>` and read stderr) show:

- `bank R[00 01 03 07]` — OS in slot 0/1, keyboard window in slot 2, TPA bank (block 7) in slot 3. **Slot 0 NEVER switches to block 7**, so CCP at 0x0100 in TPA bank is never invoked.
- Over a 6000-frame I/O trace: **only `OUT (F0),0x80` and `OUT (F0),0x84` are observed** — never `0x87` (TPA mapping). All inter-bank trampolines (PC 0x08AB, 0x07DD) only switch F1 and F2.
- `(0x1010)` chain head = 0x0021, `(0x0D00)` chain head = 0x0021 (occasionally 0x002F). Dispatcher logic at PC 0x0880-0x0890 treats any pointer with HIGH byte = 0 as an empty queue → branch to 0x085A "idle marker" path forever.
- `(0x10A0)` timer chain head = 0x0E8D — IS being processed. Counter bytes in the timer-chain table (0x10A8-0x10AD region) do advance over time. So the dispatcher is alive and timer interrupts work, just nothing schedules CCP load.
- FDC reads complete cleanly: tracks 1-10 (full system load), then track 39 sectors 5-6 (M: drive metadata), then RECALIBRATE, then FDC stays idle for the rest of the run.
- Printer port FDh returns 0xCC = `BAIL | FINISHED | FEEDER | PAPER | READY | no-fault` — bit pattern correct per Seasip §6.1.1.
- Keyboard window at 0xBFF0..0xBFFE = all zero (no keys pressed), 0xBFFF cycles 0x40/0x80/0xC0 (ticker bits). Per Seasip §10 this is the "kbd present, scanning" state.

## What was ruled out

- Sticky FDC IRQ bit 5 (fixed; was the primary cause of the previous stall, now resolved).
- Missing kbd MCU clock heartbeat on 0xBFFF (added; no effect).
- CPU writes to the kbd window being dropped (mem.c now backs the window with RAM; no effect).
- Printer FDh status bit semantics (already match Seasip §6.1.1).
- Alternate disk images (CPM3 2-09 is worse — garbled).
- Keyboard input (typing Enter does not unstick).

## Working hypotheses for the remaining stall

1. **The OS is waiting for a printer MCU side-effect we don't produce.** The 8041AH printer MCU is a real co-processor; on a real PCW it may write status bytes back into shared RAM that the BIOS polls. Our printer is a port-stub only. Worth investigating: trace printer command writes from BIOS, then see whether ZEsarUX / Joyce / MAME do any RAM-level callbacks from their printer model.
2. **A specific bit on a kbd window byte (LK1/LK2/LK3 / shift-lock LED on 0xBFFD / 0xBFFE) is wrong.** Seasip §10.2 documents three option links that the BIOS may read. Our defaults (all zero) match Joyce's defaults, but it's worth setting LK2-present explicitly and seeing if anything changes.
3. **The dispatcher chain at `(0x10A0)` is missing an entry that should be enqueued by an interrupt we aren't firing or are firing wrong.** The chain's counter table at 0x10B1+ has 16 slots, of which only `(0x10B1) = 0x0D43` is non-zero. Maybe more slots should be populated by the BIOS init, and they aren't because some earlier ISR didn't run.
4. **Possible bank-force / F4-write semantics bug.** F4 is currently handled as "bank force register" but its `bank_force` byte isn't honoured anywhere in `mem_read` / `mem_write`. If the BIOS uses CPC paging with bank-force at any point, our reads/writes diverge from real hardware. (Not seen in this boot's trace, but a documented loose end.)

## Concrete next probe

Instrument every memory write to:
- `(0x1010)` — main work-queue head
- `(0x0D00)` — secondary work-queue head
- `(0x10A0)` and the table at `(0x10B1..0x10CE)` — timer chain table
- byte at `(0x6D1B)` — printer countdown

Log `PC` and the new value on each write. This should reveal which code paths advance the BIOS state machine, and which paths never run because some upstream condition never fires.

## Reference

Boot-process documentation referenced in this round:
- Seasip Joyce PDF `hardware.pdf` §2 (boot ROM), §5 (interrupts), §6.1 (printer ports), §10 (keyboard)
- Joyce 2.4.2 source: `JoyceAsic.cxx`, `JoycePcwKeyboard.cxx`, `PcwFdc.cxx`
- MAME `pcw.cpp` and `upd765.cpp`
- ZEsarUX `src/machines/pcw.c` (especially `pcw_handle_end_boot_disk` — PC=0x0607 is the documented "boot complete" address for CP/M+ on this disk family)
- Jacob Nevins boot disassembly: https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwboot.html
