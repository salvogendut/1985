#include "kbd.h"
#include <string.h>

/*
 * PCW key matrix — transcribed from joyce's JoycePcwKeyboard.cxx:43-167.
 *
 * Each entry maps a host SDL3 scancode to a PCW (row, bit) where bit
 * is the bit POSITION within the row byte (joyce stores it as a mask
 * 1/2/4/.../128; we use 0..7 to match kbd_press's existing API).
 * Joyce's table has separate unshifted/shifted variants but for the
 * main alphabetic/punctuation keys they're identical — physical-key
 * scancodes don't change under shift, the PCW firmware reads the
 * SHIFT bit (row 2 bit 5) alongside.
 */
typedef struct { int row, bit; } MatrixPos;

static MatrixPos sdl_to_matrix(SDL_Scancode s) {
    switch (s) {
        /* Letters */
        case SDL_SCANCODE_A: return (MatrixPos){8, 5};
        case SDL_SCANCODE_B: return (MatrixPos){6, 6};
        case SDL_SCANCODE_C: return (MatrixPos){7, 6};
        case SDL_SCANCODE_D: return (MatrixPos){7, 5};
        case SDL_SCANCODE_E: return (MatrixPos){7, 2};
        case SDL_SCANCODE_F: return (MatrixPos){6, 5};
        case SDL_SCANCODE_G: return (MatrixPos){6, 4};
        case SDL_SCANCODE_H: return (MatrixPos){5, 4};
        case SDL_SCANCODE_I: return (MatrixPos){4, 3};
        case SDL_SCANCODE_J: return (MatrixPos){5, 5};
        case SDL_SCANCODE_K: return (MatrixPos){4, 5};
        case SDL_SCANCODE_L: return (MatrixPos){4, 4};
        case SDL_SCANCODE_M: return (MatrixPos){4, 6};
        case SDL_SCANCODE_N: return (MatrixPos){5, 6};
        case SDL_SCANCODE_O: return (MatrixPos){4, 2};
        case SDL_SCANCODE_P: return (MatrixPos){3, 3};
        case SDL_SCANCODE_Q: return (MatrixPos){8, 3};
        case SDL_SCANCODE_R: return (MatrixPos){6, 2};
        case SDL_SCANCODE_S: return (MatrixPos){7, 4};
        case SDL_SCANCODE_T: return (MatrixPos){6, 3};
        case SDL_SCANCODE_U: return (MatrixPos){5, 2};
        case SDL_SCANCODE_V: return (MatrixPos){6, 7};
        case SDL_SCANCODE_W: return (MatrixPos){7, 3};
        case SDL_SCANCODE_X: return (MatrixPos){7, 7};
        case SDL_SCANCODE_Y: return (MatrixPos){5, 3};
        case SDL_SCANCODE_Z: return (MatrixPos){8, 7};

        /* Digits */
        case SDL_SCANCODE_0: return (MatrixPos){4, 0};
        case SDL_SCANCODE_1: return (MatrixPos){8, 0};
        case SDL_SCANCODE_2: return (MatrixPos){8, 1};
        case SDL_SCANCODE_3: return (MatrixPos){7, 1};
        case SDL_SCANCODE_4: return (MatrixPos){7, 0};
        case SDL_SCANCODE_5: return (MatrixPos){6, 1};
        case SDL_SCANCODE_6: return (MatrixPos){6, 0};
        case SDL_SCANCODE_7: return (MatrixPos){5, 1};
        case SDL_SCANCODE_8: return (MatrixPos){5, 0};
        case SDL_SCANCODE_9: return (MatrixPos){4, 1};

        /* Punctuation */
        case SDL_SCANCODE_SPACE:        return (MatrixPos){5, 7};
        case SDL_SCANCODE_APOSTROPHE:   return (MatrixPos){3, 4};
        case SDL_SCANCODE_COMMA:        return (MatrixPos){4, 7};
        case SDL_SCANCODE_MINUS:        return (MatrixPos){3, 1};
        case SDL_SCANCODE_PERIOD:       return (MatrixPos){3, 7};
        case SDL_SCANCODE_SLASH:        return (MatrixPos){3, 6};
        case SDL_SCANCODE_BACKSLASH:    return (MatrixPos){2, 6};
        case SDL_SCANCODE_SEMICOLON:    return (MatrixPos){3, 5};
        case SDL_SCANCODE_EQUALS:       return (MatrixPos){3, 0};
        case SDL_SCANCODE_LEFTBRACKET:  return (MatrixPos){3, 2};
        case SDL_SCANCODE_RIGHTBRACKET: return (MatrixPos){2, 1};
        case SDL_SCANCODE_GRAVE:        return (MatrixPos){8, 2};

        /* Editing / control */
        case SDL_SCANCODE_RETURN:       return (MatrixPos){2, 2};
        case SDL_SCANCODE_KP_ENTER:     return (MatrixPos){10, 5};
        case SDL_SCANCODE_BACKSPACE:    return (MatrixPos){9, 7};
        case SDL_SCANCODE_TAB:          return (MatrixPos){8, 4};
        case SDL_SCANCODE_ESCAPE:       return (MatrixPos){1, 0};
        case SDL_SCANCODE_DELETE:       return (MatrixPos){2, 0};
        case SDL_SCANCODE_INSERT:       return (MatrixPos){1, 2};
        case SDL_SCANCODE_HOME:         return (MatrixPos){1, 3};
        case SDL_SCANCODE_END:          return (MatrixPos){10, 2};
        case SDL_SCANCODE_PAGEUP:       return (MatrixPos){0, 3};
        case SDL_SCANCODE_PAGEDOWN:     return (MatrixPos){1, 4};
        case SDL_SCANCODE_CAPSLOCK:     return (MatrixPos){10, 5};

        /* Arrows */
        case SDL_SCANCODE_UP:           return (MatrixPos){1, 6};
        case SDL_SCANCODE_DOWN:         return (MatrixPos){10, 6};
        case SDL_SCANCODE_LEFT:         return (MatrixPos){1, 7};
        case SDL_SCANCODE_RIGHT:        return (MatrixPos){0, 6};

        /* Modifiers */
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:       return (MatrixPos){2, 5};
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:        return (MatrixPos){10, 1};
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:         return (MatrixPos){10, 7};

        /* PCW function-key block. F2/F4/F6/F8 use the same matrix
         * positions as F1/F3/F5/F7 plus synthetic Shift below, matching
         * Joyce's m_autoShift handling. F9/F10 have no PCW matrix key. */
        case SDL_SCANCODE_F1:
        case SDL_SCANCODE_F2:  return (MatrixPos){0, 2};
        case SDL_SCANCODE_F3:
        case SDL_SCANCODE_F4:  return (MatrixPos){0, 0};
        case SDL_SCANCODE_F5:
        case SDL_SCANCODE_F6:  return (MatrixPos){10, 0};
        case SDL_SCANCODE_F7:
        case SDL_SCANCODE_F8:  return (MatrixPos){10, 4};
        case SDL_SCANCODE_F11: return (MatrixPos){2, 7};
        case SDL_SCANCODE_F12: return (MatrixPos){10, 3};

        default: return (MatrixPos){-1, -1};
    }
}

static int shift_bit_for_scancode(SDL_Scancode s) {
    switch (s) {
        case SDL_SCANCODE_LSHIFT: return 0;
        case SDL_SCANCODE_RSHIFT: return 1;
        default: return -1;
    }
}

static int auto_shift_bit_for_scancode(SDL_Scancode s) {
    switch (s) {
        case SDL_SCANCODE_F2: return 0;
        case SDL_SCANCODE_F4: return 1;
        case SDL_SCANCODE_F6: return 2;
        case SDL_SCANCODE_F8: return 3;
        default: return -1;
    }
}

static void kbd_update_shift(Keyboard *k) {
    if (k->shift_held || k->auto_shift_held)
        k->row[2] |= (u8)(1 << 5);
    else
        k->row[2] &= (u8)~(1 << 5);
}

void kbd_init(Keyboard *k) {
    memset(k, 0, sizeof(*k));
}

u8 kbd_matrix_byte(Keyboard *k, u8 row) {
    if (row >= KBD_ROWS) return 0;
    if (row == 15) {
        u8 v = k->row[15] & 0x3F;
        if (k->ticker & 1) v |= 0x80;
        if (k->ticker & 2) v |= 0x40;
        return v;
    }
    return k->row[row];
}

void kbd_tick(Keyboard *k) {
    k->ticker++;
}

void kbd_scan_into_ram(Keyboard *k, u8 *kbd_window) {
    /* Joyce JoycePcwKeyboard.cxx:484-489 — the keyboard MCU "writes" the
     * scanned matrix into PCWRAM[0xFFF0..0xFFFF] periodically. CPU writes
     * into that window survive between scans (it's just RAM). We mirror
     * that: the periodic scan overwrites the 16 bytes with our current
     * matrix data; in between, the RAM is whatever the CPU last wrote. */
    for (int i = 0; i < KBD_ROWS; i++)
        kbd_window[i] = kbd_matrix_byte(k, (u8)i);
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
    int sb = shift_bit_for_scancode(e->scancode);
    if (sb >= 0) {
        if (e->down) k->shift_held |=  (u8)(1 << sb);
        else         k->shift_held &= (u8)~(1 << sb);
        kbd_update_shift(k);
        return;
    }

    int ab = auto_shift_bit_for_scancode(e->scancode);
    if (ab >= 0) {
        if (e->down) k->auto_shift_held |=  (u8)(1 << ab);
        else         k->auto_shift_held &= (u8)~(1 << ab);
        kbd_update_shift(k);
    }

    MatrixPos p = sdl_to_matrix(e->scancode);
    if (p.row < 0) return;
    if (e->down) kbd_press  (k, p.row, p.bit);
    else         kbd_release(k, p.row, p.bit);
}
