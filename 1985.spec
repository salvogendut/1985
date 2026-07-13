Name:           1985
Version:        0.4.7
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
BuildRequires:  systemd-rpm-macros
%{?systemd_requires}
Requires(pre):  systemd

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

%pre
# Dedicated, shell-less system account the 1985-web.service unit runs as
# (shared with 1984's equivalent unit if both packages are installed) —
# never the interactive user who happens to enable the service.
%sysusers_create_inline u emulator - "1984/1985 Web Service" /var/lib/emulator /usr/sbin/nologin

%post
%systemd_post 1985-web.service

%preun
%systemd_preun 1985-web.service

%postun
%systemd_postun_with_restart 1985-web.service

%files
%license LICENSE
%doc README.md INSTALL.md USAGE.md
%{_bindir}/%{name}
%{_mandir}/man1/%{name}.1*
%{_unitdir}/1985-web.service
%{_datadir}/applications/io.github.salvogendut.Emulator1985.desktop
%{_datadir}/metainfo/io.github.salvogendut.Emulator1985.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.salvogendut.Emulator1985.png

%changelog
* Mon Jul 13 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.7-1
- Add PerryNet mode for PerryFi, including TCP, UDP, pull-based receive and
  firmware time support (#153, #155, #164).
- Add the browser-based Web GUI and isolated Web Service, with path-confined
  uploads, CSPRNG session tokens and a sandboxed systemd unit (#157, #159,
  #161).
- Add pilot/autopilot control for scripted testing and web audio streaming
  (#163, #165).
- Use the brighter reference green as the default monochrome monitor tint
  (#152).

* Thu Jul 09 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.6-1
- 1985-web.service is now a sandboxed system unit running as a dedicated,
  shell-less 'emulator' account (created via sysusers, shared with 1984's
  equivalent unit) instead of a systemd --user unit running as whoever
  enables it; heavy systemd sandboxing (ProtectSystem=strict,
  NoNewPrivileges, no capabilities, etc.) added on top. Session-config
  upload can no longer redirect disk/boot-ROM/printer/PTY paths to
  arbitrary host locations, and Web Service session cookies now come from
  the OS CSPRNG instead of seeded libc rand().

* Thu Jul 02 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.5-1
- F8 bit-4 polarity fix: CP/M+ now configures the full 256-line PAL screen
  instead of a 200-line NTSC one — fixes AMX Desk's clipped desktop and
  unreachable trash bin (#143).
- Function-key strip moved to its own band below the PCW image so the CP/M
  status line row is no longer covered (#143).
- New Advanced "Status line" toggle: hide the guest's bottom 8 scanlines the
  way real CRT overscan does (#148).
- Video render path ~2.5x faster: direct framebuffer row writes and cached
  Real CRT colour tables (#142).
- New --disk-event N:D:PATH CLI flag for scripted mid-run disk swap/eject
  (#144).

* Tue Jun 30 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.4-1
- 8 MHz Z80 Turbo toggle on the General tab (#137).
- Camera-shutter sound effect when taking an F4 screenshot (#134).
- Beeper preserves its audio sample rate across cold resets (#136).
- Flatpak now builds from main and ships as a release asset; the separate
  flatpak branch was retired (#139).

* Sun Jun 28 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.3-1
- On-screen toast notifications: disk-open failures, serial PTY readiness
  and PerryFi dial-outs now fade in/out over the PCW display. F9 ▸
  Advanced ▸ Notifications cycles screen / console / off (#128).
- LED hover labels: mousing over a status LED in the bottom bar now
  reveals what it represents (#132).
- Real CRT controls in Advanced: scanline opacity, brightness, contrast
  and per-channel RGB gain knobs for the lightweight CRT post-process
  (#132).
- Media tab overlay panel resized so the "Enter: select DSK / Del:
  eject" hint sits inside the dark panel instead of clipping over the
  PCW backdrop (#130).

* Fri Jun 26 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.2-1
- PCW built-in beeper (F8 cmd 0x0B/0x0C, 3.75 kHz) — audit M9.
  Boot ROM error tones, CP/M BEL, +/- and F7/F8 PCW keys all audible.
- Multilink probe-stub at ports 0xA6/0xA7 (audit M8) — prevents
  Multilink-aware software from hanging on probe. Gated on the
  PCW Backplane and a new "Multilink" Extensions toggle.
- Boot ROM search path: looks in $XDG_DATA_HOME/1985/roms,
  ~/.local/share/1985/roms, $XDG_CONFIG_HOME/1985/roms, and
  ~/.config/1985/roms before falling back to ./roms. New F9 →
  Advanced "Boot ROM" row shows the resolved path; Enter opens a
  folder picker, Del clears the override.
- F9 → Advanced "Version" row now also shows the git commit so
  users can paste an exact build id into a bug report.

* Fri Jun 26 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.4.1-1
- CF2DD blank disc spec byte 1 = 0x81 — drive-B writes now work on
  PCW 8512/9512 and on 8256 with the Second drive accessory (#114).
- Overlay file pickers (Drive A/B, blank create, snapshot Load/Save)
  remember the last directory used; persisted in ~/.config/1985 (#116).
- Windows build now persists config under %APPDATA%/1985 instead of
  next to the .exe (#118).

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
