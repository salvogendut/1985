# Usage

## CLI flags

| Flag | Purpose |
|------|---------|
| `--config PATH` | Read config from PATH instead of `~/.config/1985/1985.conf` |
| `--memory KB` | RAM size: 256, 512 or 2048 |
| `--disk-a PATH` | Mount `.dsk` image in drive A |
| `--disk-b PATH` | Mount `.dsk` image in drive B |
| `--paste TEXT` | Type TEXT after boot (`\n` becomes Enter) |
| `--paste-at N` | Delay `--paste` until frame N |
| `--paste-event N:TEXT` | Inject TEXT at frame N; repeat for scripted input |
| `--load-sna PATH` | Load a `.sna` snapshot at init (stubbed) |
| `--save-sna-at N:PATH` | Save snapshot at frame N (stubbed) |
| `--screenshot-at N:PATH` | Save PPM at frame N and exit |
| `--gif-out PATH` | Record GIF from boot until exit |
| `--exit-after N` | Quit after frame N (headless capture) |
| `--dump-at N` | Dump CPU, memory and FDC state at frame N |
| `--unthrottled` | Disable 50 Hz host pacing for diagnostics |
| `--monitor-pty` | Open a PTY for the F8 monitor |
| `--kbd-pty` | Open a PTY that injects keystrokes |
| `--symbols PATH` | Load SDCC `.map` for the F8 disassembler |
| `-h`, `--help` | Show help |

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| Click in window | Capture host mouse input when Input Device is Mouse |
| Ctrl+Enter | Release captured mouse input |
| F4  | PPM screenshot |
| F5  | Warm reset |
| F8  | Memory monitor / disassembler |
| F9  | Options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Ctrl+V | Paste clipboard |

## Overlay tabs

- **General** — model (8256 / 8512 / 9512), RAM, Second drive (8256
  only), PCW Backplane, Tinker toggle.
- **Media** — drive A and B file pickers.
- **Extensions** (only when the PCW Backplane is enabled) — serial port,
  PerryFi, DK'tronics sound, and Input Device (Mouse / Joystick).
- **Advanced** (hidden unless `tinker=true`) — smoothing, Real CRT
  scanlines/brightness/contrast/RGB controls, tint (green / amber /
  white), tint mode (normal / glow), video mode, Mouse type (AMX /
  Kempston), Joystick type, printer mode/model, debug toggles, serial
  mode, snapshot load/save, Notifications (screen / console / off).

## Notifications

Disk-open failures, serial PTY readiness and PerryFi dial-outs are
surfaced through the notification system. F9 ▸ Advanced ▸
**Notifications** cycles three modes:

- **screen** (default) — a fading bottom-left toast over the PCW display.
- **console** — one stderr line, no on-screen overlay (the pre-0.4.3 behaviour).
- **off** — silent.

Hovering over any status LED in the bottom bar reveals its label
(floppy A/B, printer, serial RX/TX, …).
