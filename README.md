# 1985 — Amstrad PCW 8256 emulator

A small Amstrad PCW emulator written in C with SDL3. Sibling project
to [1984](https://github.com/salvogendut/1984) (the Amstrad CPC
emulator); the two share build system, overlay framework, and the Z80
core.

## Status

**Boots CP/M Plus.** All three reference boot disks (J11/J17/J29 CP/M
3) reach the `A>` prompt with keyboard input. The Z80 core, ASIC, uPD765A
FDC, roller-RAM video decoder, keyboard matrix, and F9 overlay are all
wired up. Printer, RAM-disc M:, and snapshot save are still stubs.

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
boot without two existing emulators to cross-check against:

- **[Joyce](https://www.seasip.info/Unix/Joyce/)** — John Elliott's
  long-running PCW emulator. The reference for ASIC port map,
  roller-RAM video, the keyboard matrix, and (crucially) the uPD765A
  FDC IRQ arm-delay logic from its `lib765` core. Also the source of
  the authoritative `Docs/hardware.txt` and the boot-ROM bytes.
- **[ZEsarUX](https://github.com/chernandezba/zesarux)** — César
  Hernández Bañó's multi-machine Z80 emulator. Cross-checked for FDC
  IRQ delivery, printer port 0xFD semantics, F4 lock bits, and the
  expansion-port range handler.

Additional documentation:

- [PCW hardware reference](https://www.seasip.info/AmstradXT/index.html)
- [systemed.net PCW pages](https://www.systemed.net/pcw/hardware.html)
