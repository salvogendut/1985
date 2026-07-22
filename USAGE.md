# Usage

## CLI flags

| Flag | Purpose |
|------|---------|
| `--config PATH` | Read config from PATH instead of `~/.config/1985/1985.conf` |
| `--memory KB` | RAM size: 256, 512 or 2048 |
| `--disk-a PATH` | Mount `.dsk` image in drive A |
| `--disk-b PATH` | Mount `.dsk` image in drive B |
| `--sdl-fm` | Force the built-in SDL disk browser instead of the platform file dialog |
| `--paste TEXT` | Type TEXT after boot (`\n` becomes Enter) |
| `--paste-at N` | Delay `--paste` until frame N |
| `--paste-event N:TEXT` | Inject TEXT at frame N; repeat for scripted input |
| `--disk-event N:D:PATH` | At frame N put PATH in drive D (`a`/`b`); empty PATH ejects. Scripts the boot-then-swap flow multi-disk software needs |
| `--load-sna PATH` | Load a `.sna` snapshot at init (stubbed) |
| `--save-sna-at N:PATH` | Save snapshot at frame N (stubbed) |
| `--screenshot-at N:PATH` | Save PPM at frame N and exit |
| `--gif-out PATH` | Record GIF from boot until exit using the configured capture profile |
| `--exit-after N` | Quit after frame N (headless capture) |
| `--dump-at N` | Dump CPU, memory and FDC state at frame N |
| `--unthrottled` | Disable 50 Hz host pacing for diagnostics |
| `--pilot[=ARG]` | Open a PTY auto-pilot. `ARG` may be a symlink path or initial target `mouse`/`joystick` |
| `--pilot-replies-stderr` | Mirror pilot replies to stderr, useful in headless automation logs |
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
| F6  | Start or stop GIF capture |
| F8  | Memory monitor / disassembler |
| F9  | Options overlay |
| F11 | Toggle fullscreen |
| F12 | Quit |
| Ctrl+V | Paste clipboard |

## Overlay tabs

- **General** — model (8256 / 8512 / 9512), RAM, Second drive (8256
  only), PCW Backplane, Tinker toggle.
- **Media** — drive A and B file pickers. Enter uses the platform file
  dialog, while Shift+Enter opens the built-in keyboard-driven DSK browser.
  If the platform dialog is unavailable, 1985 falls back to the built-in
  browser automatically. Use `--sdl-fm` to force it for every disk selection.
- **Extensions** (only when the PCW Backplane is enabled) — serial port,
  PerryFi, DK'tronics sound, and Input Device (Mouse / Joystick).
- **Advanced** (hidden unless `tinker=true`) — smoothing, GIF resolution,
  frame rate and encoder, Real CRT scanlines/brightness/contrast/RGB
  controls, tint (green / amber /
  white), tint mode (normal / glow), video mode, Region, Status line
  (shown / hidden — a real CRT hides the guest's bottom 8 scanlines,
  where CP/M keeps its status row, in overscan), Mouse type (AMX /
  Kempston), Joystick type, printer mode/model, debug toggles, serial
  mode/path, PerryFi mode, snapshot load/save, Notifications (screen /
  console / off).

## GIF capture

F6 writes an animated GIF without including the options overlay. Under
Advanced, **GIF resolution** selects 720x540, 540x405, 360x270, 240x180, or
180x135, while **GIF frame rate** selects 25, 20, 10, or 5 fps. These are the
useful size controls because GIF has no dependable target-bitrate setting.
The defaults remain 720x540 at 25 fps, and the selected profile also applies
to `--gif-out`.

**GIF encoder** defaults to `built-in`. If FFmpeg was found when 1985 was
configured, `FFmpeg optimize` re-encodes the completed capture with a global
palette and inter-frame differences. The original is retained if conversion
fails or does not make the file smaller. Long conversions can briefly pause
the emulator when capture stops. Delete or Backspace on any GIF setting
restores its default. Config keys are `gif_resolution`, `gif_fps`, and
`gif_ffmpeg` under `[advanced]`.

## Notifications

Disk-open failures, serial PTY readiness and PerryFi dial-outs are
surfaced through the notification system. F9 ▸ Advanced ▸
**Notifications** cycles three modes:

- **screen** (default) — a fading bottom-left toast over the PCW display.
- **console** — one stderr line, no on-screen overlay (the pre-0.4.3 behaviour).
- **off** — silent.

Hovering over any status LED in the bottom bar reveals its label
(floppy A/B, printer, serial RX/TX, …).
