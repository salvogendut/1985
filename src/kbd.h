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
 * A '1' bit means the key is currently pressed. (Joyce's notation
 * is inverted in places — see Docs/hardware.txt — but the firmware
 * reads the same set of bytes either way.)
 *
 * The actual SDL-scancode → (row, bit) mapping is filled in by
 * kbd.c. For the scaffold pass we only wire a handful so the F-key
 * and overlay code compiles and the emulator can be exercised; the
 * full PCW layout (EXIT, EXTRA, COPY, CUT, PASTE, ENTER, DEL→ etc.)
 * lands in a follow-up.
 */

#define KBD_ROWS  16

typedef struct Keyboard {
    u8 row[KBD_ROWS];
} Keyboard;

void kbd_init(Keyboard *k);

/* Return raw matrix byte for row 0..15. mem.c calls this when the
 * Z80 reads 0x3FF0..0x3FFF in the slot currently mapping block 3. */
u8   kbd_matrix_byte(Keyboard *k, u8 row);

/* SDL keyboard event → matrix update. */
void kbd_handle(Keyboard *k, const SDL_KeyboardEvent *e);

/* Direct press / release used by the paste helper. */
void kbd_press  (Keyboard *k, int row, int bit);
void kbd_release(Keyboard *k, int row, int bit);

/* Aliases matching 1984's paste.c API. */
static inline void kbd_key_down(Keyboard *k, int row, int col) { kbd_press  (k, row, col); }
static inline void kbd_key_up  (Keyboard *k, int row, int col) { kbd_release(k, row, col); }
