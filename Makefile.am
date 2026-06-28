bin_PROGRAMS = 1985

dist_man1_MANS = 1985.1

1985_SOURCES = \
	src/asic.c \
	src/aysound.c \
	src/beeper.c \
	src/bootstrap.c \
	src/config.c \
	src/cps.c \
	src/crtc.c \
	src/disk.c \
	src/display.c \
	src/fdc.c \
	src/gifcap.c \
	src/kbd.c \
	src/leds.c \
	src/main.c \
	src/mem.c \
	src/monitor.c \
	src/multilink.c \
	src/notify.c \
	src/overlay.c \
	src/paste.c \
	src/pcw.c \
	src/perryfi.c \
	src/pcwmouse.c \
	src/printer.c \
	src/roller.c \
	src/serial.c \
	src/snapshot.c \
	src/symbols.c \
	src/z80.c \
	src/z80dis.c

noinst_HEADERS = \
	src/asic.h \
	src/aysound.h \
	src/beeper.h \
	src/bootstrap.h \
	src/compat_win.h \
	src/config.h \
	src/cps.h \
	src/crtc.h \
	src/disk.h \
	src/display.h \
	src/fdc.h \
	src/gifcap.h \
	src/kbd.h \
	src/leds.h \
	src/mem.h \
	src/monitor.h \
	src/multilink.h \
	src/notify.h \
	src/overlay.h \
	src/paste.h \
	src/pcw.h \
	src/perryfi.h \
	src/pcwmouse.h \
	src/printer.h \
	src/roller.h \
	src/serial.h \
	src/shutter_wav.h \
	src/snapshot.h \
	src/startup_debug.h \
	src/symbols.h \
	src/input_types.h \
	src/types.h \
	src/z80.h \
	src/z80dis.h

if WITH_CAIRO
CAIRO_DEFINE = -DHAVE_CAIRO=1
else
CAIRO_DEFINE =
endif

1985_CFLAGS  = $(AM_CFLAGS) $(SDL3_CFLAGS) $(CAIRO_CFLAGS) $(CAIRO_DEFINE) \
               -Wall -Wextra -DPROG_GIT_COMMIT=\"$(PROG_GIT_COMMIT)\"
1985_LDADD        = $(SDL3_LIBS) $(CAIRO_LIBS) -lm $(WIN_LIBS) $(WIN_RC_OBJ)
1985_DEPENDENCIES = $(WIN_RC_OBJ)

icons/1985-rc.o: $(srcdir)/icons/1985.rc $(srcdir)/icons/1985.ico
	@mkdir -p icons
	$(WINDRES) -I$(srcdir)/icons -i $< -O coff -o $@

CLEANFILES = icons/1985-rc.o

desktopdir         = $(datadir)/applications
dist_desktop_DATA  = io.github.salvogendut.Emulator1985.desktop

metainfodir        = $(datadir)/metainfo
dist_metainfo_DATA = io.github.salvogendut.Emulator1985.metainfo.xml

icon16dir          = $(datadir)/icons/hicolor/16x16/apps
dist_icon16_DATA   = icons/16x16/apps/io.github.salvogendut.Emulator1985.png
icon32dir          = $(datadir)/icons/hicolor/32x32/apps
dist_icon32_DATA   = icons/32x32/apps/io.github.salvogendut.Emulator1985.png
icon48dir          = $(datadir)/icons/hicolor/48x48/apps
dist_icon48_DATA   = icons/48x48/apps/io.github.salvogendut.Emulator1985.png
icon64dir          = $(datadir)/icons/hicolor/64x64/apps
dist_icon64_DATA   = icons/64x64/apps/io.github.salvogendut.Emulator1985.png
icon128dir         = $(datadir)/icons/hicolor/128x128/apps
dist_icon128_DATA  = icons/128x128/apps/io.github.salvogendut.Emulator1985.png
icon256dir         = $(datadir)/icons/hicolor/256x256/apps
dist_icon256_DATA  = icons/256x256/apps/io.github.salvogendut.Emulator1985.png
icon512dir         = $(datadir)/icons/hicolor/512x512/apps
dist_icon512_DATA  = icons/512x512/apps/io.github.salvogendut.Emulator1985.png

EXTRA_DIST = LICENSE README.md INSTALL.md USAGE.md Development.md \
             1985.conf.example icons/1985.rc icons/1985.ico
