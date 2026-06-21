# Development notes

1985 is the smallest possible PCW skeleton built in 1984's style.

## Source layout

```
src/
  z80.c, z80dis.c            Z80 core (lifted verbatim from 1984)
  monitor.c                  F8 monitor / disassembler
  symbols.c                  SDCC .map loader
  display.c                  SDL3 framebuffer + tint
  overlay.c                  F9 overlay + file picker
  config.c                   INI loader / saver
  main.c                     Entry, event loop, CLI
  kbd.c, kbd_pty.c           Keyboard matrix + PTY injection
  paste.c                    Clipboard paste
  leds.c                     Activity LEDs
  gifcap.c                   F6 GIF capture
  pcw.c                      PCW machine — wires Z80Bus to mem/io
  mem.c                      16 KB bank paging (ports 0xF0-0xF3)
  bootstrap.c                778-byte reset stream
  asic.c                     Port 0xF4/0xF8 system control
  crtc.c                     MC6845 wrapper (stub)
  roller.c                   Roller-RAM bitmap decode (stub)
  fdc.c                      uPD765A (ported from 1984, ports 0x00/0x01)
  disk.c                     .dsk loader
  printer.c                  Port 0xFC/0xFD stub
  snapshot.c                 .sna stub
```

## Reference

`/var/home/salvogendut/Dev/joyce-custom/` — John Elliott's Joyce, used
as a reference (not vendored). Most useful files:

- `Docs/hardware.txt` — authoritative port and memory map
- `bin/JoyceMemory.cxx` — 16 KB bank paging
- `bin/JoycePcwTerm.cxx` — roller-RAM decode (`do_roller`)
- `bin/JoyceAsic.cxx` — port 0xF8 system control
- `bin/JoycePcwKeyboard.cxx` — keyboard matrix at 0x3FF0-0x3FFF
- `bin/JoyceFdc.cxx` + `765/lib765.c` — uPD765A core
- `bin/JoyceZ80.cxx` — port I/O dispatch reference

## Build inside the container

```bash
distrobox enter my-distrobox
autoreconf -fiv && ./configure && make -s
./1985
```
