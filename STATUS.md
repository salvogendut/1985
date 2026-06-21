# PCW Boot Investigation Status (issue #12 — `ccp-load` branch)

## Summary

The original sticky-FDC-IRQ-latch bug is fixed and the boot has visibly advanced — CP/M+ now reaches the "Drive is A:" status line and a cursor block. The BIOS scheduler is now decoded too, but the final CCP handoff still does not happen.

## What changed in this round

- **`src/asic.{c,h}` — removed sticky FDC IRQ latch** (was the documented primary fix in the plan)
  - Cross-checked MAME (`pcw_fdc_interrupt`, pcw.cpp:196-206) and Joyce (`JoyceAsic::fdcIsr`, JoyceAsic.cxx:89-93). Both treat F8 bit 5 as a LIVE mirror of the FDC INTRQ line, not a sticky latch.
  - Our previous attempt latched the bit on every poll and never cleared it. After the first FDC command, bit 5 stayed high forever; the BIOS dispatcher at 0x077B repeatedly took the "FDC done" branch and never fell through to the timer/work path properly.
  - Fix: dropped `fdc_irq_latched` field. `sys_status()` now derives bit 5 directly from `fdc->irq`.

- **`src/mem.c` — kbd window backed by RAM** (joyce-faithful)
  - Removed the read overlay that returned matrix bytes for the keyboard window.
  - Reads now come from RAM; writes go to RAM (CPU scratch survives between scans).

- **`src/mem.c` — PCW paging reset / lock semantics aligned with MAME**
  - Reset now starts with `bank_force = 0xF0`, matching MAME’s PCW reset path.
  - CPC-style bank writes now use the 0-7 write-bank range the firmware expects.

- **`src/mem.c` — scheduler trace instrumentation expanded**
  - Added logging for reads and writes in the BIOS scheduler areas:
    - `0x0D00..0x0D0F`
    - `0x1010..0x1017`
    - `0x10A0..0x10AF`
    - `0x6D1B..0x6D1F`
  - This let us watch the queue state as the BIOS dispatcher runs, instead of only seeing the final screen stall.

- **`src/kbd.{c,h}` + `src/pcw.c` — periodic kbd scan into RAM**
  - New `kbd_scan_into_ram()` writes the 16-byte matrix into block 3 offset 0x3FF0 every 300 Hz tick.
  - Added `ticker` field + heartbeat bits on byte 0xBFFF (bit 7 toggles every tick, bit 6 every other), per Joyce JoycePcwKeyboard.cxx:475-478 and Seasip §10 ("3FFFh bit 6 toggles with each update from the keyboard to the PCW. 3FFFh bit 7 is 1 if the keyboard is currently transmitting its state to the PCW, 0 if it is scanning its keys").

- **`src/main.c` — `--dump-at N` diagnostic** (kept for future debugging)
  - Dumps Z80 registers, bank state, ASIC/FDC state, disassembly around PC, disassembly of the BIOS scheduler blocks (0x0770, 0x077B, 0x078B, 0x07A4, 0x07C3, 0x07E6, 0x0853, 0x0880, 0x08A0, 0x08AB, 0x08E9, 0x0AD0, 0x0A98, 0x07D4, 0x0030, 0x0B6A, 0x4734, 0x4E84), and key memory regions including raw block-3 keyboard data.

- **`src/fdc.c` — trace lines include phase name** (small diag improvement)

## What is still broken (current stall)

- Screen shows: `CP/M Plus  Amstrad Consumer Electronics plc / v 1.7, 61K TPA, 1 disc drive, 112K drive M:` + cursor block + `Drive is A:` (bottom right).
- Real-hardware behaviour we are NOT reproducing: between the banner and `A>`, the disk LED blinks briefly while CCP is paged in and executed.
- The BIOS scheduler path is alive but the handoff queue never reaches the final return path that switches into CCP.

## Evidence gathered

State dumps at frames 4000/6000/8000 (`tools` directory of a fresh checkout: run `./1985 --dump-at <N>` and read stderr) show:

- `bank R[00 01 03 07]` — OS in slot 0/1, keyboard window in slot 2, TPA bank (block 7) in slot 3. **Slot 0 NEVER switches to block 7**, so CCP at 0x0100 in TPA bank is never invoked.
- Over a 6000-frame I/O trace: **only `OUT (F0),0x80` and `OUT (F0),0x84` are observed** — never `0x87` (TPA mapping). All inter-bank trampolines (PC 0x08AB, 0x07DD) only switch F1 and F2.
- Dispatcher decode:
  - `0x0770` reads F8/F4 and steers between FDC-complete work and timer work.
  - `0x0853` is the idle/scheduler entry and resets `0x1014` to `0x80`.
  - `0x0880` walks `0x1010` and `0x0D00`; if both heads are empty it falls back to `0x085A`.
  - `0x07E6` walks the timer chain.
  - `0x08E9` inserts a record into the `0x0D00` queue.
- `(0x1010)` queue head is still collapsing back to the empty sentinel. The latest traces show `0x1012/0x1013` briefly seed `0x0D21`, but `0x1010/0x1011` return to `0x0021/0x0000` and the queue never settles.
- `(0x10A0)` timer chain head = `0x0E8D` — it is being processed. Counter bytes in the timer-chain table (0x10A8-0x10AF region) do advance over time. So the dispatcher is alive and timer interrupts work, just nothing schedules CCP load.
- With the new read trace enabled, the scheduler decision is now visible:
  - `mem_read 0x0D00 -> 0x00`
  - `mem_read 0x0D01 -> 0x00`
  - `mem_read 0x1010 -> 0x00`
  - `mem_read 0x1011 -> 0x00`
  - `mem_write 0x1014` flips between `0x80` and `0x00`
  - This shows the BIOS loop is polling the queue, but the seed record is not staying live long enough to trigger the `JP Z,07D4` handoff path.
- The first nonzero seed writes we captured now come from `pc=F179`:
  - `0x0D00..0x0D0F` is initialized there.
  - `0x1010..0x1017` is seeded there.
  - `0x10A0..0x10AF` is seeded there.
  - Later, the scheduler loop at `pc=085F` repeatedly rewrites `0x1014=80/00`, which matches the observed “queue clears before CCP handoff” behavior.
- ZEsarUX confirms the expected boot handoff shape:
  - `pcw_boot_cpm()` boots `pcw_8x_boot2.dsk` and expects the boot path to finish at `reg_pc == 0x607`.
  - The boot program is still copied to `0D000h` and run as documented in Joyce.
  - ZEsarUX did not show an extra post-boot fixup for the `pc=F179` seed routine, so the remaining mismatch still looks like our scheduler/queue handling.
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
- Bank-force reset semantics as the primary blocker (now aligned with MAME; no change in boot outcome).
- FDC command/result handling as the remaining blocker (the FDC goes idle cleanly after the load sequence).

## Working hypotheses for the remaining stall

1. **The OS is waiting for a printer MCU side-effect we don't produce.** The 8041AH printer MCU is a real co-processor; on a real PCW it may write status bytes back into shared RAM that the BIOS polls. Our printer is a port-stub only. Worth investigating: trace printer command writes from BIOS, then see whether ZEsarUX / Joyce / MAME do any RAM-level callbacks from their printer model.
2. **A specific bit on a kbd window byte (LK1/LK2/LK3 / shift-lock LED on 0xBFFD / 0xBFFE) is wrong.** Seasip §10.2 documents three option links that the BIOS may read. Our defaults (all zero) match Joyce's defaults, but it's worth setting LK2-present explicitly and seeing if anything changes.
3. **The scheduler queue at `(0x1010)` is being seeded from the boot code path at `pc=F179`, but it is not surviving the scheduler loop at `pc=085F`.** The comparison at `0x0880` against `0x1015`/`0x0D07` is the next concrete mismatch to chase.
4. **The remaining question is whether `pc=F179` is supposed to write one additional byte or flag that our model is missing.** The trace says the seed exists; the problem is that the BIOS does not retain it long enough to transition into CCP.

## Concrete next probe

Instrument every memory write to:
- `(0x1010..0x1017)` — runnable queue head and its adjacent control bytes
- `(0x0D00..0x0D0F)` — secondary queue / return record
- `(0x10A0..0x10AF)` — timer chain table
- `(0x6D1B..0x6D1F)` — printer countdown / boot-delay bytes

Log `PC` and the new value on each write. The immediate goal is to identify the first code path that should leave a nonzero `0x1010` record in place long enough for the BIOS scheduler to transition into CCP.

## Reference

Boot-process documentation referenced in this round:
- Seasip Joyce PDF `hardware.pdf` §2 (boot ROM), §5 (interrupts), §6.1 (printer ports), §10 (keyboard)
- Joyce 2.4.2 source: `JoyceAsic.cxx`, `JoycePcwKeyboard.cxx`, `PcwFdc.cxx`
- MAME `pcw.cpp` and `upd765.cpp`
- ZEsarUX `src/machines/pcw.c` (especially `pcw_handle_end_boot_disk` — PC=0x0607 is the documented "boot complete" address for CP/M+ on this disk family)
- ZEsarUX `src/machines/pcw.c` (`pcw_boot_cpm`, `pcw_handle_end_boot_disk`)
- Jacob Nevins boot disassembly: https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwboot.html
