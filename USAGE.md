# Usage

## CLI flags

| Flag | Purpose |
|------|---------|
| `--config PATH` | Read config from PATH instead of `~/.config/1985/1985.conf` |
| `--memory KB` | RAM size: 256, 512 or 2048 |
| `--disk-a PATH` | Mount `.dsk` image in drive A |
| `--disk-b PATH` | Mount `.dsk` image in drive B |
| `--paste TEXT` | Type TEXT after boot (`\n` becomes Enter) |
| `--load-sna PATH` | Load a `.sna` snapshot at init (stubbed) |
| `--save-sna-at N:PATH` | Save snapshot at frame N (stubbed) |
| `--screenshot-at N:PATH` | Save PPM at frame N and exit |
| `--gif-out PATH` | Record GIF from boot until exit |
| `--exit-after N` | Quit after frame N (headless capture) |
| `--monitor-pty` | Open a PTY for the F8 monitor |
| `--kbd-pty` | Open a PTY that injects keystrokes |
| `--symbols PATH` | Load SDCC `.map` for the F8 disassembler |
| `-h`, `--help` | Show help |

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| F4  | PPM screenshot |
| F5  | Warm reset |
| F8  | Memory monitor / disassembler |
| F9  | Options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Ctrl+V | Paste clipboard |

## Overlay tabs

- **General** — model (8256 / 8512 / 9512), RAM, debug toggle.
- **Media** — drive A and B file pickers.
- **Extensions** — printer mode placeholder.
- **Advanced** (hidden unless `tinker=true`) — smoothing, monochrome
  tint (off / green / amber / white), FPS counter, trace toggles,
  snapshot load/save.
