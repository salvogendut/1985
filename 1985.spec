Name:           1985
Version:        0.1.0
Release:        1%{?dist}
Summary:        Amstrad PCW 8256 emulator

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
1985 is an Amstrad PCW 8256 emulator written in C with SDL3, a
sibling project to 1984 (CPC). The PCW family ran CP/M Plus on
a Z80 with a Motorola 6845 CRTC driving a 720x256 monochrome
green-phosphor display, an NEC uPD765A floppy controller, and
256 KB of banked RAM addressed in 16 KB pages.

The PCW has no boot ROM — a 778-byte bootstrap stream is
injected at reset, builds a 256-byte loader in RAM, and that
loader pulls the system from drive A. 1985 reproduces this
behaviour and ships no firmware ROMs.

This initial release ships scaffolding only: SDL3 window, F9
overlay framework, Z80 core wired against PCW memory and I/O.
Roller-RAM rendering and the real FDC drive backend are stubbed
for follow-up releases.

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
* Sun Jun 21 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.1.0-1
- Initial scaffolding for the Amstrad PCW 8256 emulator
