# 1985 - Amstrad PCW 8256 / 8512 / 9512 emulator

![1985](1985.png)

1985 is an Amstrad PCW emulator written in C using SDL3. It is the sibling
project of [1984](https://github.com/salvogendut/1984), the Amstrad CPC
emulator; the projects share their Z80 core, build system, display framework,
and development tools.

## Current status

1985 boots CP/M Plus on the PCW 8256, 8512, and 9512. The reference J11, J17,
and J29 system disks reach the CP/M prompt with working keyboard, display,
floppy, printer, and expansion I/O.

| Model | Default RAM | Drives | Default display |
|-------|-------------|--------|-----------------|
| PCW 8256 | 256 KB | One, with an optional second drive | Green |
| PCW 8512 | 512 KB | Two | Green |
| PCW 9512 | 512 KB | Two | White |

RAM can be set to 256 KB, 512 KB, or 2 MB. Changing the model or RAM size from
the F9 overlay performs a cold boot. The bootstrap ROM is embedded in the
executable, so a separate ROM file is not required; a CP/M system disk image
is still needed.

The emulated machine includes the Z80 CPU, PCW ASIC and memory paging,
roller-RAM video, keyboard matrix, uPD765A floppy controller, built-in beeper,
printer ports, and model-specific hardware defaults. PAL 50 Hz and NTSC 60 Hz
timing are available, together with an optional 8 MHz Turbo mode.

## Hardware controls

The **PCW Backplane** setting controls the expansion hardware shown in the
Extensions tab. It is not required by the built-in printer or by the optional
second drive on an 8256; both of those controls live in the General tab.

| Device | Location | Backplane requirement |
|--------|----------|-----------------------|
| Second drive | General | No; available on the 8256 |
| Printer capture | General | No; the built-in printer port exists on every model |
| CPS8256 serial/Centronics | Extensions | Required on 8256/8512; serial hardware is built into the 9512 |
| PerryFi | Extensions | Required; also needs the serial interface |
| DK'TRONICS Sound & Joystick | Extensions | Required |
| Multilink | Extensions | Required; probe compatibility only |

The Advanced tab is hidden until **Tinker** is enabled in General. It contains
the detailed display, GIF capture, input, serial, printer, networking,
snapshot, web, and diagnostic settings.

## Floppy media

1985 reads standard and extended DSK images and supports the PCW's 40-track,
single-sided CF2 and 80-track, double-sided CF2DD layouts. Drive B performs
double stepping when 40-track media is inserted. Guest writes and FORMAT
TRACK operations update the mounted image; dirty disks are saved on eject,
reset, and exit.

In F9 > Media:

- **Enter** opens the platform file picker.
- **Shift+Enter** opens the built-in keyboard-driven DSK browser.
- If the platform picker is unavailable, 1985 falls back to the built-in
  browser automatically. `--sdl-fm` forces the built-in browser.
- **Delete** or **Backspace** ejects the selected disk.
- **N** creates a blank image for that drive; **Shift+N** selects the other
  geometry.

## Display, audio, and input

The native display is the PCW's 720 x 256 monochrome image with green, amber,
white, or disabled phosphor tint. Optional tint glow and Real CRT processing
provide scanlines, brightness, contrast, and per-channel gain controls. The
bottom eight guest scanlines, normally hidden by CRT overscan, can be shown or
hidden with **Status line**.

CGA1, CGA2, and EGA modes reinterpret the monochrome framebuffer with period
colour palettes. These modes are host-side visual experiments; they do not
represent colour hardware that PCW software can program.

F6 records an animated GIF using the resolution, frame-rate, and encoder
profile under Advanced. The built-in encoder has no external dependencies;
when available, optional FFmpeg optimization can reduce the completed file.
The same profile applies to `--gif-out`.

The stock 3.75 kHz PCW beeper is emulated. The DK'TRONICS extension adds an
AY-3-8912 with tone, noise, and envelope output through SDL audio.

With the DK'TRONICS input extension enabled, host input can be exposed as:

- AMX, Kempston, or Creative Technology Keymouse mouse hardware.
- DKsound, Kempston, Cascade, or Spectravideo joystick hardware.
- A configurable keyboard chord acting as a joystick.

Click the emulator window to capture relative host-mouse input when the input
device is set to Mouse. Press **Ctrl+Enter** to release it. SDL gamepads use the
selected guest joystick protocol.

## Printer

The emulated printer is built-in PCW hardware and **does not require the PCW
Backplane**. The **Printer** control is in the General tab and only determines
whether guest output is captured by the host; disabling capture does not make
the printer disappear from CP/M.

With Cairo enabled, captured jobs are written as timestamped
`1985-print-YYYYMMDD-HHMMSS.pdf` files in the selected directory. A job is
finalised after roughly two seconds without printer activity. Advanced >
**Printer mode** selects either PDF output or the host's default printer. The
Real printer mode sends the completed PDF to `lp` on Linux and macOS. The
printer LED lights whenever guest bytes are received.

The 8256 and 8512 built-in dot-matrix command stream is rendered as dots.
CPS8256 Centronics text is sent through the same host sink when that interface
is enabled. The 9512 is identified as a daisywheel model, but faithful
daisywheel mechanics are not yet emulated; its LST output is still accepted by
the current renderer so that captured text remains usable.

The official Windows build is compiled without Cairo. Its guest printer ports
still report ready and the printer LED still works, but it does not produce a
host PDF or spool to a real printer.

## Serial and networking

The CPS8256 implementation provides a Z80-DART, Intel 8253 baud generator, and
Centronics interface at ports `0xE0` through `0xE8`. On POSIX hosts its serial
line can terminate at a PTY (with `/tmp/1985-serial` as the default stable
alias) or at a TCP listener on `localhost:4002`.

PerryFi attaches to that serial line and has two selectable models:

- **AT Hayes** is the default. It simulates the PerryFi/PerryFiFW modem command
  interface and lets existing terminal programs dial TCP endpoints with
  commands such as `ATDT host:port`.
- **PerryNet TCP/IP** implements the framed SLIP/CRC protocol used by the
  PerryNet firmware. It provides device discovery, Wi-Fi status commands,
  DNS, TCP clients, UDP datagrams, UART settings, ping, and device time for
  software written against that API. TCP listener commands are not currently
  implemented.

Both modes are firmware simulations that use the host network; the emulator
does not model an ESP8266 radio. AT settings are held for the current emulation
session rather than persisted by `AT&W`. Host serial and PerryFi socket
backends are not currently available in the Windows build.

The Multilink option implements the probe ports and returns the standard
"ring broken" response so Multilink-aware software can finish detection. It
does not emulate a Multilink network.

<p align="center">
  <img src="screenshots/cpm.png" alt="CP/M Plus boot banner and A prompt" width="380">
  &nbsp;&nbsp;
  <img src="screenshots/alien8.png" alt="Alien 8 running on the PCW" width="380">
</p>

<p align="center">
  <img src="screenshots/emu_perryfi.png" alt="Aardwolf MUD over PerryFi AT modem" width="480"><br>
  <sub><b>PerryFi AT modem: CP/M terminal software connected to a real MUD over host TCP/IP</b></sub>
</p>

## Snapshots and automation

F9 > Advanced provides snapshot load and save. A snapshot stores RAM, Z80
registers, and ASIC and memory-paging state. Loading a snapshot made for a
different model cold-boots the required machine before restoring it. FDC
command state, AY synthesis state, and serial and printer buffers are not
preserved, so saving from an idle CP/M prompt is the safest choice.

The command line also supports snapshot loading, scheduled screenshots, GIF
recording, text and disk-change events, state dumps, unthrottled execution,
and a PTY-based `--pilot` interface for automated keyboard, mouse, joystick,
snapshot, hash, crop, and wait commands. Run `./1985 --help` for the current
option list.

## Web access

1985 includes two browser interfaces:

- **Web GUI** mirrors and controls the one machine already running in the SDL
  application. It streams the display and browser-started mono audio, and
  accepts keyboard, relative mouse, paste, and reset input. Enable it from
  Advanced or with `web_gui=true` in the configuration file.
- **Web Service**, started with `--web[=PORT]`, is a headless multi-session
  server. Each browser cookie jar receives an isolated PCW with its own
  configuration and uploaded disks. It supports up to four sessions and
  removes a detached session after ten minutes. Packages install the
  `1985-web.service` systemd unit for a permanent service.

Both modes bind to `0.0.0.0` and have no authentication. Anyone who can reach
the port can view and control a machine, so only enable them on a trusted
network. The default port is `1985`.

## Build

Fedora dependencies and build commands:

```bash
sudo dnf install gcc make autoconf automake pkgconf-pkg-config sdl3-devel cairo-devel
autoreconf -fiv
./configure
make -j"$(nproc)"
./1985 --disk-a /path/to/cpm.dsk
```

Use `./configure --without-cairo` to build without host PDF capture. See
[INSTALL.md](INSTALL.md) for Debian, Ubuntu, macOS, and installation details,
and [FLATPAK.md](FLATPAK.md) for Flatpak builds.

## Downloads

Tagged `v*` builds are published on the
[GitHub Releases page](https://github.com/salvogendut/1985/releases) with a
Linux x86_64 binary, Fedora RPM, Windows x86_64 zip, and Flatpak bundle. Pushes
to `main` also produce workflow artifacts; pull requests run the Linux and
Windows builds, while the slower Flatpak job runs for `main`, tags, and manual
workflow dispatches.

The Windows zip includes `1985.exe`, SDL3 and runtime DLLs, and the optional
ROM directory. The embedded bootstrap remains the fallback if no external ROM
is found. First-run diagnostics are written to `1985.log` beside the
executable.

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| F4 | Save a PPM screenshot |
| F5 | Cold reset |
| F6 | Start or stop GIF capture |
| F8 | Open the memory monitor and disassembler |
| F9 | Open or close the options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Shift+F1 through Shift+F8 | PCW f1 through f8 keys |
| Ctrl+= / Ctrl+- | Change window scale from 1x through 4x |
| Ctrl+V | Paste host clipboard text into the guest |
| Click in window | Capture the mouse when Input Device is Mouse |
| Ctrl+Enter | Release captured mouse input |

The complete PCW-to-host key map is available from Advanced > **Show keyboard
layout**.

## Configuration

Settings are stored in `~/.config/1985/1985.conf`. The file is created when
settings are first saved. Most settings can be changed from F9; enable
**Tinker** in General to expose Advanced. `--config PATH` loads an alternative
configuration for that run.

Additional project documentation is available in [INSTALL.md](INSTALL.md),
[Development.md](Development.md), and [FLATPAK.md](FLATPAK.md).

## Known limitations

- Faithful PCW 9512 daisywheel mechanics are not implemented.
- Multilink provides detection compatibility, not ring networking.
- Snapshots omit transient FDC, audio, serial, and printer state.
- The Windows build has no Cairo printer capture, host serial backend, or
  PerryFi socket backend.

## License

GPL-2.0-only. See `LICENSE`.

## Acknowledgments

1985 is its own code, but it would not have reached a working CP/M Plus boot
without existing emulators and community hardware projects to cross-check:

- **[Joyce](https://www.seasip.info/Unix/Joyce/)** - John Elliott's PCW
  emulator. It is the reference for the ASIC port map, roller-RAM video,
  keyboard matrix, uPD765A FDC IRQ behaviour, CPS8256 Z80-DART/8253/Centronics
  model, hardware documentation, and bootstrap ROM bytes.
- **[ZEsarUX](https://github.com/chernandezba/zesarux)** - Cesar Hernandez
  Bano's multi-machine Z80 emulator. It was cross-checked for FDC IRQ delivery,
  printer semantics, ASIC lock bits, and expansion I/O. The experimental PCW,
  CGA1, CGA2, and EGA host video modes are based on its PCW renderer.
- **[SanPollo / PCWBackplane](https://github.com/SanPollo/PCWBackplane)** -
  the modern 50-pin PCW expansion hub represented by the Backplane control.
- **[SanPollo / PerryFi](https://github.com/SanPollo/PerryFi)** and
  **[PerryFiFW](https://github.com/SanPollo/PerryFiFW)** - the ESP8266 modem
  hardware, AT command set, and dial-out semantics behind the default PerryFi
  model. PerryFiFW derives from
  [RetroWiFiModem](https://github.com/mecparts/RetroWiFiModem).
- **PerryNet** - the framed DNS, TCP, UDP, and time service implemented by the
  alternate PerryFi mode for software that does not use Hayes dialing.
- **[DK'TRONICS Sound & Joystick](https://www.habisoft.com/pcwwiki/doku.php?id=es:hardware:perifericos:dksound)**
  - schematic and clone documentation for the AY-3-8912 and DB9 board. The AY
  model is shared with 1984's `src/psg.c`.

Additional hardware references:

- [PCW hardware reference](https://www.seasip.info/AmstradXT/index.html)
- [systemed.net PCW pages](https://www.systemed.net/pcw/hardware.html)
- [chiark PCW I/O ports](https://www.chiark.greenend.org.uk/~jacobn/cpm/pcwports.html)
- [readerrorb.ro PCW I/O ports](https://wiki.readerrorb.ro/doku.php?id=tech:amstrad:pcw:ioports)
