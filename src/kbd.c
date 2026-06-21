#include "kbd.h"
#include <string.h>

/*
 * Scaffold mapping — a tiny subset of the PCW matrix is wired so the
 * emulator can be exercised. Full matrix (96 keys + 2 joysticks)
 * lands later, transcribed from `bin/JoycePcwKeyboard.cxx`.
 *
 * Layout legend (row, bit) from Docs/hardware.txt:
 *   0x3FF1 bit 0 = EXIT
 *   0x3FF1 bit 2 = CUT
 *   0x3FF1 bit 3 = COPY
 *   0x3FF0 bit 3 = PASTE
 *   0x3FF8 bit 1 = STOP
 *   ... etc.
 * The placeholders below give us SPACE → row 5 bit 7 and the four
 * cursor keys, just so something happens when keys are pressed.
 */
typedef struct { int row, bit; } MatrixPos;

static MatrixPos sdl_to_matrix(SDL_Scancode s) {
    switch (s) {
        case SDL_SCANCODE_SPACE:    return (MatrixPos){5, 7};
        case SDL_SCANCODE_RETURN:   return (MatrixPos){2, 2};
        case SDL_SCANCODE_LEFT:     return (MatrixPos){1, 0};
        case SDL_SCANCODE_RIGHT:    return (MatrixPos){1, 1};
        case SDL_SCANCODE_UP:       return (MatrixPos){1, 2};
        case SDL_SCANCODE_DOWN:     return (MatrixPos){1, 3};
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:   return (MatrixPos){2, 5};
        default:                    return (MatrixPos){-1, -1};
    }
}

void kbd_init(Keyboard *k) {
    memset(k, 0, sizeof(*k));
}

u8 kbd_matrix_byte(Keyboard *k, u8 row) {
    if (row >= KBD_ROWS) return 0;
    return k->row[row];
}

void kbd_press(Keyboard *k, int row, int bit) {
    if (row < 0 || row >= KBD_ROWS || bit < 0 || bit > 7) return;
    k->row[row] |= (u8)(1 << bit);
}

void kbd_release(Keyboard *k, int row, int bit) {
    if (row < 0 || row >= KBD_ROWS || bit < 0 || bit > 7) return;
    k->row[row] &= (u8)~(1 << bit);
}

void kbd_handle(Keyboard *k, const SDL_KeyboardEvent *e) {
    MatrixPos p = sdl_to_matrix(e->scancode);
    if (p.row < 0) return;
    if (e->down) kbd_press  (k, p.row, p.bit);
    else         kbd_release(k, p.row, p.bit);
}
