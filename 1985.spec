Name:           1985
Version:        0.4.0
Release:        1%{?dist}
Summary:        Amstrad PCW 8256 / 8512 / 9512 emulator

License:        GPL-2.0-only
URL:            https://github.com/salvogendut/1985
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  pkgconfig(sdl3)
BuildRequires:  pkgconfig(cairo)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

%description
1985 is an Amstrad PCW emulator written in C with SDL3, a sibling
project to 1984 (CPC). The PCW family ran CP/M Plus on a Z80 with
a custom ASIC driving a roller-RAM bitmap display, an NEC uPD765A
floppy controller, and up to 2 MB of banked RAM addressed in
16 KB pages.

Models supported: PCW 8256 (256 KB, single floppy, green monitor),
PCW 8512 (512 KB, two floppies), and PCW 9512 (512 KB, two
floppies, white monitor). All three reach the CP/M+ A> prompt and
report the expected model-specific RAM and drive count.

Hardware extensions available through the F9 Extensions menu:

  * PDF printer — host-side capture of the built-in 9-pin
    matrix-printer protocol (ports FCh/FDh) and CPS8256 Centronics
    bytes, rendered to a timestamped Cairo PDF.
  * Second drive (8256 only — 8512/9512 already have two).
  * PCW Backplane (SanPollo 50-pin edge-connector hub).
  * Serial port — Amstrad CPS8256 (Z80-DART + 8253 + Centronics).
    Built into the 9512, otherwise needs the backplane. Routes to
    a host /dev/pts or a TCP listener on localhost:4002.
  * PerryFi — Wemos D1 AT-modem over the serial line, dialling
    out to real host TCP sockets without any WiFi config.
  * DK'tronics Sound & Joystick — AY-3-8912 PSG with selectable
    joystick/mouse input, DKsound/Atari gamepad mappings, and
    AMX/Kempston host-mouse protocols.

The F8 memory monitor / disassembler with PTY interface, F9
options overlay, F4 PPM screenshot, F6 GIF capture, and Ctrl+V
paste-from-clipboard round out the package.

The PCW has no boot ROM — the firmware lives in the printer MCU
(8041AH). 1985 ships that 275-byte ROM as roms/pcw_boot.rom with
an identical embedded fallback compiled in, so no external
firmware is needed at runtime.

%prep
%autosetup

%build
autoreconf -fiv
%configure
%make_build

%install
%make_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.salvogendut.Emulator1985.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/io.github.salvogendut.Emulator1985.metainfo.xml

%files
%license LICENSE
%doc README.md INSTALL.md USAGE.md
%{_bindir}/%{name}
%{_mandir}/man1/%{name}.1*
%{_datadir}/applications/io.github.salvogendut.Emulator1985.desktop
%{_datadir}/metainfo/io.github.salvogendut.Emulator1985.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.salvogendut.Emulator1985.png

%changelog
* Tue Jun 23 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.0-1
- F9 ▸ General reorganised: PCW Backplane now lives here and gates
  the whole Extensions tab; Second drive moved here too (8256 only)
  since it's a stock accessory and doesn't need the backplane (#75).
- Floppy LED B is hidden when the running model has no second drive,
  matching the real machine (#77).
- Printer LED hides when the backplane is unplugged.
- Man page refreshed for 0.4.0.

* Tue Jun 23 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.3.0-1
- Live tint update on General ▸ Model switch (#69).
- 9512 Printer model row in Advanced (Daisywheel / Centronics),
  informational for now (#70).
- Tint mode: Normal / Glow — drops the background to near-black
  for any phosphor tint to match a turned-up CRT (#73).
- README cleanups: M: stub line removed, ZEsarUX credited for
  the ported video modes.

* Tue Jun 23 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.2.0-1
- Fix silent crash on Windows (PCW struct moved to BSS — 2 MB RAM
  was blowing the 1 MB main-thread stack).
- Fix FreeBSD build (BSD-visible feature macros + MSG_NOSIGNAL).
- Ctrl + / Ctrl − step window scale 1× … 4×.
- Decorative video modes ported from ZEsarUX: PCW / CGA1 / CGA2
  / EGA.
- "Printer mode" toggle — PDF file (default) or real printer via
  CUPS lp.
- Snapshot save / load (.sna), with Save / Load entries in F9 ▸
  Advanced.
- Default window scale dropped from 2× to 1×.
- README refreshed.

* Tue Jun 23 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.1.0-1
- All three PCW models (8256/8512/9512) boot CP/M Plus.
- Extensions menu: PDF printer, second drive, PCW Backplane,
  CPS8256 serial port, PerryFi AT-modem, DK'tronics Sound &
  Joystick.
- F8 memory monitor with PTY interface; Ctrl+V clipboard paste;
  F6 GIF capture; F9 overlay with keyboard layout viewer.
