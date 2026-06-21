# PCW Boot Investigation Status (issue #12 — `ccp-load` branch)

## Summary

The original sticky-FDC-IRQ-latch bug is fixed and the boot has visibly advanced — CP/M+ now reaches the "Drive is A:" status line and a cursor block. **The investigation has now reframed: CCP IS RUNNING.** Trace shows code executing in slot 1 with bank 8 mapped (CCP bank, per systemed.net hardware map) at `pc=5D9D`, in a tight busy-wait that decrements byte `0x6D1D` from `0xFF` to `0x00`, then `pc=5DA0` reloads it to `0x1E`, and the cycle repeats hundreds of times. The earlier conclusion that "CCP never gets invoked" was wrong; the actual bug is that CCP is **stuck in a retry/timeout loop waiting on a device or flag we are not satisfying**.

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

## Reframed findings (this round)

- The 088B/085F dispatcher loop that constantly toggles `0x1014` between `0x80` and `0x00` is **steady-state BIOS scheduler idle behavior**, NOT a "queue collapse" bug. With ALL-writes-with-bank-context tracing turned on, both bytes show stable `slot=0 rb=00 wb=00 bf=F0` — no banking mismatch.
- `pc=F179` (boot ROM in slot 3 = bank 7) is writing real Z80 **opcode bytes** (e.g. `D3 F2 7D E1 C9` = `OUT (F2),A; LD A,L; POP HL; RET`) into low RAM at `0x0D00`, `0x1010`, `0x10A0`. This is the BIOS jumpblock relocator. Subsequent writes by `pc=C816` and `pc=C874` reinitialize those addresses as DATA after the code phase is done — expected.
- `pc=5D9D` writes byte `0x6D1D` with **`slot=1 rb=08 wb=08`** — bank 8 is mapped in. Per systemed bank map, bank 8 = "CCP, hash tables, data buffers". This code is executing as part of CCP/BDOS. The trace shows it counting down `0x6D1D` from `0xFF → 0x00`, then `pc=5DA0` reloads it to `0x1E`, repeating ~269+ times over the run.
- F4 lock bit ordering was wrong (slot mapping bug) — confirmed via MAME `pcw.cpp:295-318` AND ZEsarUX `pcw.c:251-272`. Both use slot 0→b6, slot 1→b4, slot 2→b5, slot 3→b7. Fixed.

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

## REFRAMED AGAIN — pc=5D9D is NOT CCP, it's the printer ISR

Disassembly of bank 8 offset 0x1D80 (= `pc=5D9D` in slot 1) reveals:

```
5D82: IN A,(FDh)         ; read printer status
5D84: AND 85h            ; mask BAIL / READY bits
5D87: LD HL,6D1Bh
5D8A: BIT 7,D            ; test BAIL
5D9C: DEC (HL)           ; decrement printer-state counter at (6D1D)
5D9D: RET NZ             ; <-- expected normal-idle return
5D9E: LD (HL),1Eh        ; reload counter to 30 every full cycle
5DA3: JP 0A1Ah           ; tail-call into BIOS bank 0
```

This is the **300Hz printer-poll timer ISR** — normal idle background activity. The "tight loop" we see is the BIOS scheduler ticking the printer counter on every timer interrupt, which is **expected** when the system is alive but idle.

IO trace confirms: F8=09 (motor on) happened twice during boot, F8=0A (motor off) once. Current motor state = ON. After the initial track-load sequence (~88 sector reads + RECALIBRATE + SENSE INT), the FDC stays idle for the rest of the run. CCP never issues another disk command.

## Working hypotheses for the remaining stall (re-reframed)

The system is alive, idle, and running its background ISRs. CCP printed the banner ("Drive is A:"). After that, CCP should issue a directory read on drive A: to show `A>`. It does not. So CCP either:

1. **Reached its command loop and is waiting for a keypress**, but our keyboard input is not being seen. The kbd matrix bytes in block 3 at 0x3FF0..0x3FFE are observed at `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 40` — heartbeat bit on, no keys pressed. If CCP polls BDOS function 6 (direct console) and BDOS reads the kbd window but the "key pressed" semantic differs subtly, no key would ever register.
2. **Is autoexec'ing PROFILE.SUB** and stuck waiting for the SUBMIT mechanism (which uses `$$$.SUB` files on the disk).
3. **Has reached `A>` internally but is still inside BDOS warm-boot finalization** — never gets a chance to actually print and prompt because some final init step deadlocks against an interrupt we mis-deliver.

## BDOS trace result (concrete stuck point)

Hook on `pc == 0x0005` (BDOS entry vector) logs every BDOS call with function number, DE, and return address. The CCP startup sequence captured:

```
bdos f=31 (Get DPB)          DE=0D6A  ret=0429
bdos f=31 (Get DPB)          DE=0392  ret=0458
bdos f=62 (Set/Get DR Vector)DE=0392  ret=045D
bdos f=98 (Parse Filename)   DE=0D6C  ret=0AF3
bdos f=1A (Set DMA Address)  DE=0DA2  ret=09A5
bdos f=0F (OPEN FILE)        DE=0DAD  ret=09B2   <-- never returns
```

CCP is trying to **OPEN FILE** (likely PROFILE.SUB or a startup script) at FCB=0x0DAD. BDOS goes into the open path, tries to read the directory from disk, and never returns. After this point: no more BDOS calls, no FDC commands issued, system enters its idle 300Hz printer-poll loop.

So the bug is: **BDOS OPEN FILE never issues a disk read that completes**. Either:
- BDOS is stuck in a tight loop polling a memory byte BIOS should be flipping
- BDOS issues the BIOS "read directory sector" call but BIOS is stuck in its own polling loop on FDC state
- A bank-switch in BDOS->BIOS->driver chain leaves an inconsistent paging state that traps execution

Visual confirmation done: screenshot at frame 5000 shows banner + cursor block + "Drive is A:", no `A>`. Enter-key inject has no effect (confirmed CCP is not waiting for user input — real PCW shows `A>` autonomously).

## Cross-emulator comparison (in progress)

Plan: read MAME (pcw.cpp + upd765.cpp + i8275.cpp + z80.cpp), ZEsarUX (machines/pcw.c + cpu_z80.c), Joyce 2.4.2 (JoyceAsic.cxx, PcwFdc.cxx, Z80core.cxx, JoycePcwKeyboard.cxx) and compare against our implementation, component by component. See bottom of file for findings as they accumulate.

## Cross-emulator divergence findings (running log)

### Z80 core (fork B1 done, /tmp/z80-compare.md)
No smoking gun. ALU flags, INT/NMI/RETI/RETN, EI delay, block-op interruptibility match MAME/Joyce. WZ/MEMPTR not implemented (low risk). Joyce comment flags "FDC first then timer" for INT source ordering — worth checking ASIC.

### ASIC (fork B2 done, /tmp/asic-compare.md)
**SMOKING GUN: IRQ delivery is edge-pulsed instead of level-triggered.** MAME (pcw.cpp:153-176) and Joyce (JoyceAsic.cxx:135-140) hold /INT asserted continuously while `fdc->irq` is high AND mode==IRQ. We edge-pulse. If BDOS arms its wait loop AFTER our edge has fired and been consumed, we lose the re-assertion.

Also: NMI hold semantic missing, F4 read doesn't re-evaluate IRQ, flyback phase inverted (same duty, opposite phase).

### Memory/Paging (fork B3 done, /tmp/mem-compare.md)
No bug. All four emulators agree on F0–F4 semantics. Minor LK1/LK3/shift-lock bits not populated in our kbd window (others set some of these); likely benign for OPEN FILE.

### FDC + Printer (forks B5/B6 done, /tmp/fdc-printer-compare.md)
- **Printer is NOT the cause** — ZEsarUX boots fine with port FD always returning 0x40; our 0xCC is a strict superset.
- **FDC IRQ assertion is synchronous** — MAME/Joyce schedule via countdown. Combined with edge-pulsed IRQ delivery in ASIC, BIOS misses SEEK-completion IRQs.
- **Spurious IRQ on invalid SENSE INT** — our code raised f->irq=true even with int_pending=false; real chip does not.

### Fixes applied this round
1. **asic.c — IRQ delivery now level-triggered** (NMI kept edge-triggered). Matches MAME/Joyce.
2. **fdc.c — invalid SENSE INT no longer raises spurious IRQ.** Returns ST0=0x80 via data register without going through enter_result().
3. **mem.c — F4 lock bit ordering fixed** (slot 0→b6, slot 1→b4, slot 2→b5, slot 3→b7) per MAME/ZEsarUX.

### Fix #3 applied: FDC READ/WRITE DATA R-increment in result phase
Per uPD765A datasheet AND MAME upd765.cpp:2039-2076, after reading one
sector the FDC result phase returns CHRN pointing to the NEXT sector
(R+1, or wrap with C++ if R==EOT). We were echoing the original R.
With the fix, the trace now correctly shows results like
`fdc res phase=RESULT 00 00 00 0A 00 01 02` after the last R=9 read of
track 9 — C wrapped to 10 and R reset to 1, matching MAME.

Empirically: did NOT change the hang — same 6 BDOS calls, same hot
loop at 5180-5193. But the fix is spec-correct and may matter for
other software.

### Fix #4 applied: BDOS-return trace + foreground PC histogram
- pcw.c remembers the pushed return PC per BDOS call and logs when
  control reaches it (`bdos return fn=X`). Confirms calls 1-5 return
  cleanly; call #6 (OPEN FILE) never returns.
- pcw.c samples foreground PC (when iff1=1 and PC not in 0x0770-0x08FF
  ISR range) once per 256 instructions and emits a histogram every
  64K samples. Identifies the hot foreground PCs.

### Foreground PC histogram (post-fixes, after OPEN FILE call)
```
5188:385  518F:380  5193:357  5191:348  518B:345  518A:345
5186:335  5185:327  518E:320  5189:299  5190:296  5187:285
5156:52   51A3:50   517B:49   ...
```
Hot zone is bank-8 offset 0x1180 — disassembly shows a coroutine-style
byte-processor with bit-reversal/hashing:
```
5180: CALL 4D3Ch    ; "get next byte" -- yields via 4D44's SP swap
5183-5189: 8x RL E; RRA; DEC D    ; rotate/reverse byte
518B: CALL 48DCh    ; emit byte
518E-5191: DEC HL; JR NZ, 5180  ; HL = remaining byte count
5193: CALL 519Fh    ; outer continuation
```
4D3C/4D44 implements a stack-swap coroutine via `LD (6C4Bh),SP` /
`LD SP,(6C49h)` -- the "get byte" yields to a producer coroutine
(probably the directory-buffer/FDC reader). This pattern is CP/M+
BDOS's banked-task model.

### After fixes — current behavior
- BDOS still hangs on f=0F OPEN FILE for `A:PROFILE.SUB`.
- **NEW: user reports drive B LED blinks now too** — FDC commands ARE reaching drive B with the level-IRQ fix. Progress is happening.
- BIOS trace shows the OPEN FILE chain reaching SELDSK → FE3E → FD70 → FD84 → CALL FDA8 → hang.
- `FDA8` is a "yield" routine: PUSH DE / EX AF,AF' / EXX / EI / RET — momentarily swaps to alt-register set and EI's so an ISR fires, then control returns and code DI's to restore.
- `FD84` (which calls FDA8) is a "wait for interrupt" idiom: it installs a custom ISR pointer at memory `(0x0039)` (the JP target after the IM 1 vector at `0x0038`), yields via FDA8, and on return restores `(0x0039) = 0xFDC7` (default ISR). The custom ISR is loaded from `(0xFE77)`.
- This is sophisticated coroutine + dynamic-IRQ-vector machinery. The bug is somewhere in this dance.

### Open questions for tomorrow
1. **Why isn't an FDC command being issued?** BIOS goes through the SELDSK chain but no `OUT (1),cmd` happens. The custom ISR at `(0xFE77)` might be supposed to issue the command on the first timer tick after yielding.
2. **Is the alt-register state being preserved across our interrupt acceptance?** Joyce uses alt-set exclusively in its ISRs; if our Z80 mishandles EXX/EX AF,AF' across IRQ entry, BDOS's swapped state could be corrupted.
3. **Could the level-IRQ fix have unmasked a different bug?** Now that FDC IRQ stays asserted, maybe BIOS sees an extra IRQ at the wrong time and takes a wrong branch. Worth checking what the dynamically-installed ISR at FE77 reads from F8 to distinguish FDC vs timer.




## Reference

Boot-process documentation referenced in this round:
- Seasip Joyce PDF `hardware.pdf` §2 (boot ROM), §5 (interrupts), §6.1 (printer ports), §10 (keyboard)
- Joyce 2.4.2 source: `JoyceAsic.cxx`, `JoycePcwKeyboard.cxx`, `PcwFdc.cxx`
- MAME `pcw.cpp` and `upd765.cpp`
- ZEsarUX `src/machines/pcw.c` (especially `pcw_handle_end_boot_disk` — PC=0x0607 is the documented "boot complete" address for CP/M+ on this disk family)
- ZEsarUX `src/machines/pcw.c` (`pcw_boot_cpm`, `pcw_handle_end_boot_disk`)
- Jacob Nevins boot disassembly: https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwboot.html
