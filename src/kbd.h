#pragma once
#include "types.h"
#include <SDL3/SDL.h>

/*
 * PCW keyboard matrix
 *
 * The PCW exposes the keyboard scan as 12 bytes at addresses
 * 0x3FF0..0x3FFB (in whichever Z80 slot currently maps RAM block 3),
 * plus 4 bytes of status/joystick at 0x3FFC..0x3FFF. Bytes 0..9 are
 * the main key matrix; rows 10..11 carry the optional keyboard-side
 * joystick (J1/J2).
 *
 * Bits in the memory-mapped window are ACTIVE-HIGH: a '1' bit means
 * the key is currently pressed. (MAME's IP_ACTIVE_LOW applies at the
 * port-input level before the keyboard MCU shifts bytes into the
 * memory window; joyce stores the "1 = pressed" form directly into
 * PCWRAM[0xFFF0+k] — JoycePcwKeyboard.cxx:489 — and CP/M+ accepts it.)
 *
 * The full SDL-scancode → (row, bit) mapping is in kbd.c, transcribed
 * from joyce-2.4.2's JoycePcwKeyboard.cxx:43-167.
 */

#define KBD_ROWS  16

typedef struct Keyboard {
    u8 row[KBD_ROWS];
    u8 ticker;          /* serial-protocol heartbeat — bits 7,6 of byte 0xFFFF */
} Keyboard;

void kbd_init(Keyboard *k);

/* Advance the keyboard MCU's serial-clock ticker. Called at ~300 Hz from
 * the ASIC timer tick. Joyce JoycePcwKeyboard.cxx:475-478 — bits 7 and 6
 * of PCW byte 0xFFFF (keyboard row 15) toggle as the MCU's serial bit
 * clock; CP/M+ uses this as a "keyboard alive" heartbeat. */
void kbd_tick(Keyboard *k);

/* Return raw matrix byte for row 0..15. */
u8   kbd_matrix_byte(Keyboard *k, u8 row);

/* Write current matrix state into the 16-byte keyboard window in RAM
 * (joyce-style — RAM-backed kbd map that the MCU periodically refreshes). */
void kbd_scan_into_ram(Keyboard *k, u8 *kbd_window);

/* OR "fake joystick" chord bits into the low 6 bits of 0x3FFC-0x3FFF
 * (window indices 12-15), per Seasip Joyce §10.3 and PCW MiSTer
 * key_joystick.sv:340-447. The keyboard MCU does this aggregation on
 * real hardware; we synthesise it from current key state. Call after
 * kbd_scan_into_ram so the OR sits on top of the raw matrix bytes. */
void kbd_synth_joystick_chords(const Keyboard *k, u8 *kbd_window);

/* SDL keyboard event → matrix update. */
void kbd_handle(Keyboard *k, const SDL_KeyboardEvent *e);

/* Direct SDL scancode press / release used by the pilot PTY. Returns
 * false when the scancode has no PCW matrix mapping. */
bool kbd_sdl_key(Keyboard *k, SDL_Scancode scancode, bool down);

/* Direct press / release used by the paste helper. */
void kbd_press  (Keyboard *k, int row, int bit);
void kbd_release(Keyboard *k, int row, int bit);

/* Aliases matching 1984's paste.c API. */
static inline void kbd_key_down(Keyboard *k, int row, int col) { kbd_press  (k, row, col); }
static inline void kbd_key_up  (Keyboard *k, int row, int col) { kbd_release(k, row, col); }
