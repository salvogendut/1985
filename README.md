# 1985 — Amstrad PCW 8256 emulator

A small Amstrad PCW emulator written in C with SDL3. Sibling project
to [1984](https://github.com/salvogendut/1984) (the Amstrad CPC
emulator); the two share build system, overlay framework, and the Z80
core.

## Status

**Scaffolding only.** The build produces a working binary that opens an
SDL3 window and runs the Z80 against PCW memory and I/O, but the
roller-RAM video decoder and the FDC drive backend are stubbed, so it
does not yet boot CP/M Plus. The framework, configuration file, F9
overlay, and headless-capture flags are all wired up and ready to drive
the real hardware as it lands in follow-up releases.

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

## References

- [Joyce](https://www.seasip.info/Unix/Joyce/) — John Elliott's
  long-running PCW emulator. `joyce-custom` next to this checkout is
  the reference used for hardware shape (CRTC roller-RAM, port map,
  bootstrap stream); 1985's own code is fresh.
- [PCW hardware documentation](https://www.seasip.info/AmstradXT/index.html)
