# 1985 — Amstrad PCW 8256 emulator

![1985](1985.png)

A small Amstrad PCW emulator written in C with SDL3. Sibling project
to [1984](https://github.com/salvogendut/1984) (the Amstrad CPC
emulator); the two share build system, overlay framework, and the Z80
core.

## Status

**Boots CP/M Plus.** All three reference boot disks (J11/J17/J29 CP/M
3) reach the `A>` prompt with keyboard input. The Z80 core, ASIC, uPD765A
FDC, roller-RAM video decoder, keyboard matrix, and F9 overlay are all
wired up. The CP/M+ banner correctly reports model-specific RAM size
(8256/8512/9512), drive count, and — when the PCW Backplane is plugged
in — the "SIO/Centronics add-on" too.

Models supported: **PCW 8256** (256 KB, single floppy, green monitor),
**PCW 8512** (512 KB, two floppies, green), **PCW 9512** (512 KB, two
floppies, white). Changing model or RAM in the overlay triggers a full
cold boot.

Extensions available (Extensions tab):
- **Second drive** — bolt-on drive B for the 8256 (8512/9512 always have two).
- **PCW Backplane** — SanPollo 50-pin edge-connector hub. Acts as a
  gate for the items that physically plug into it.
- **Serial port** — Amstrad CPS8256 (Z80-DART + 8253 baud generator +
  Centronics) at I/O ports `0xE0-0xE8`. Built into the 9512; needs the
  backplane on the 8256/8512. Host-side terminates in either a PTY
  (`/dev/pts/N`) or a TCP listener on `localhost:4002` — flip mode
  under Advanced. A split RX/TX LED next to the floppies lights when
  bytes move.
- **PerryFi** — SanPollo's Wemos D1 (ESP8266) AT modem that plugs onto
  the serial line. 1985 implements the AT command set in software
  (`AT`, `ATDT host:port`, `+++ATH`, `AT&W`, `AT$SSID=`, …) and
  forwards dial-out to a real host TCP socket — no WiFi needed.

Still stubbed: internal dot-matrix printer, RAM-disc M:, snapshot
save, and most game-side hardware extensions.

<p align="center">
  <img src="screenshots/cpm.png" alt="CP/M+ boot banner and A&gt; prompt" width="380">
  &nbsp;&nbsp;
  <img src="screenshots/alien8.png" alt="Alien 8 running on the PCW" width="380">
</p>

<p align="center">
  <img src="screenshots/emu_perryfi.png" alt="Aardwolf MUD over PerryFi AT-modem" width="480"><br>
  <sub><b>PerryFi AT modem — CP/M terminal software connects to a real MUD over host TCP/IP</b></sub>
</p>

## Build (Fedora)

```bash
sudo dnf install gcc make autoconf automake pkgconf-pkg-config sdl3-devel
autoreconf -fiv
./configure
make
./1985
```

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| F4  | Save PPM screenshot |
| F5  | Reset |
| F8  | Memory monitor / disassembler |
| F9  | Options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Ctrl+V | Paste clipboard |

## Configuration

`~/.config/1985/1985.conf` is created on first run. The Advanced tab
in the overlay is hidden by default; set `tinker=true` in `[advanced]`
to expose it.

## License

GPL-2.0-only. See `LICENSE`.

## Acknowledgments

1985 is its own code, but it would not have reached a working CP/M+
boot without two existing emulators and a couple of community projects
to cross-check against:

- **[Joyce](https://www.seasip.info/Unix/Joyce/)** — John Elliott's
  long-running PCW emulator. The reference for ASIC port map,
  roller-RAM video, the keyboard matrix, the uPD765A FDC IRQ arm-delay
  logic (from its `lib765` core), and the Z80-DART / 8253 / Centronics
  model for the CPS8256 SIO add-on. Also the source of the
  authoritative `Docs/hardware.txt` and the boot-ROM bytes.
- **[ZEsarUX](https://github.com/chernandezba/zesarux)** — César
  Hernández Bañó's multi-machine Z80 emulator. Cross-checked for FDC
  IRQ delivery, printer port 0xFD semantics, F4 lock bits, and the
  expansion-port range handler.
- **[SanPollo / PCWBackplane](https://github.com/SanPollo/PCWBackplane)**
  — modern 50-pin edge-connector hub for the original PCW range,
  modelled in 1985 as the "PCW Backplane" toggle.
- **[SanPollo / PerryFi](https://github.com/SanPollo/PerryFi)** and the
  [`PerryFiFW`](https://github.com/SanPollo/PerryFiFW) Wemos D1 firmware
  — the AT-modem command set and the dial-out semantics used by 1985's
  PerryFi extension. The firmware itself is derived from
  [mecparts/RetroWiFiModem](https://github.com/mecparts/RetroWiFiModem).

Additional documentation:

- [PCW hardware reference](https://www.seasip.info/AmstradXT/index.html)
- [systemed.net PCW pages](https://www.systemed.net/pcw/hardware.html)
- [chiark PCW I/O ports](https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwports.html)
