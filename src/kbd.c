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
        /* Editor cluster — Joyce mapping (Docs/joyce.txt §4.1):
         * Ins=CUT, Home=COPY, PgUp=PASTE, Del=DEL→, End=CAN, PgDn=DOC/PAGE. */
        case SDL_SCANCODE_INSERT:       return (MatrixPos){1, 2};
        case SDL_SCANCODE_HOME:         return (MatrixPos){1, 3};
        case SDL_SCANCODE_END:          return (MatrixPos){10, 2};
        case SDL_SCANCODE_PAGEUP:       return (MatrixPos){0, 3};
        case SDL_SCANCODE_PAGEDOWN:     return (MatrixPos){1, 4};
        case SDL_SCANCODE_CAPSLOCK:     return (MatrixPos){8, 6};
        case SDL_SCANCODE_PRINTSCREEN:  return (MatrixPos){1, 1};   /* PTR */

        /* Numeric keypad — Joyce mapping. The PCW's keypad doubles as
         * a cursor/editing cluster (LINE/EOL, FIND/EXCH, etc.); these
         * positions are the PCW labels under each pad key. */
        case SDL_SCANCODE_KP_0:         return (MatrixPos){0, 1};   /* RELAY */
        case SDL_SCANCODE_KP_1:         return (MatrixPos){2, 4};   /* FIND/EXCH */
        case SDL_SCANCODE_KP_2:         return (MatrixPos){10, 6};  /* ↓ */
        case SDL_SCANCODE_KP_3:         return (MatrixPos){0, 4};   /* UNIT/PARA */
        case SDL_SCANCODE_KP_4:         return (MatrixPos){1, 7};   /* ← */
        case SDL_SCANCODE_KP_5:         return (MatrixPos){0, 7};
        case SDL_SCANCODE_KP_6:         return (MatrixPos){0, 6};   /* → */
        case SDL_SCANCODE_KP_7:         return (MatrixPos){1, 5};   /* LINE/EOL */
        case SDL_SCANCODE_KP_8:         return (MatrixPos){1, 6};   /* ↑ */
        case SDL_SCANCODE_KP_9:         return (MatrixPos){0, 5};   /* WORD/CHAR */
        case SDL_SCANCODE_KP_PERIOD:    return (MatrixPos){10, 2};  /* CAN */
        case SDL_SCANCODE_KP_DIVIDE:    return (MatrixPos){3, 6};   /* / */
        case SDL_SCANCODE_KP_MULTIPLY:  return (MatrixPos){1, 1};   /* PTR */
        case SDL_SCANCODE_KP_MINUS:     return (MatrixPos){10, 3};  /* [-] Clear */
        case SDL_SCANCODE_KP_PLUS:      return (MatrixPos){2, 7};   /* [+] Set */

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

        /* PCW function keys live behind Shift+F1..Shift+F8 — see
         * kbd_handle below. Unshifted F1..F12 are reserved for the
         * emulator's own shortcuts (overlay, screenshot, quit, …). */

        default: return (MatrixPos){-1, -1};
    }
}

/* Shift+F1..Shift+F8 → PCW f1..f8. Even indices (F1/F3/F5/F7) sit on
 * their own matrix bit; odd indices (F2/F4/F6/F8) share that bit and
 * pick up the "shift" half from the host Shift key the user is
 * holding — that's how the real PCW spells f2/f4/f6/f8. */
static MatrixPos pcw_fkey_pos(SDL_Scancode s) {
    switch (s) {
        case SDL_SCANCODE_F1: case SDL_SCANCODE_F2: return (MatrixPos){0, 2};
        case SDL_SCANCODE_F3: case SDL_SCANCODE_F4: return (MatrixPos){0, 0};
        case SDL_SCANCODE_F5: case SDL_SCANCODE_F6: return (MatrixPos){10, 0};
        case SDL_SCANCODE_F7: case SDL_SCANCODE_F8: return (MatrixPos){10, 4};
        default: return (MatrixPos){-1, -1};
    }
}

void kbd_init(Keyboard *k) {
    memset(k, 0, sizeof(*k));
}

u8 kbd_matrix_byte(Keyboard *k, u8 row) {
    if (row >= KBD_ROWS) return 0;
    if (row == 13) {
        /* Seasip Joyce §10.2: 0x3FFD bit 7 = 0 if LK2 jumper present,
         * 1 if not. We don't model the motherboard links, so report
         * "absent" (= 1) — which is also the factory default on a
         * stock PCW. Bit 6 (Shift Lock LED) is left to whatever the
         * MCU has placed in the row; we don't track LED state. */
        return (u8)(k->row[13] | 0x80);
    }
    if (row == 15) {
        /* Seasip Joyce §10:
         *   bit 7 = 1 if the keyboard is currently transmitting state
         *   bit 6 toggles with each update from kbd to PCW
         * In emulation we're always "transmitting" (no MCU silence
         * window), so bit 7 is sticky. Bit 6 toggles at the ASIC
         * timer cadence — that's the heartbeat CP/M+ watches.
         * (Matches PCW MiSTer rtl/key_joystick.sv:484-485.) */
        u8 v = k->row[15] & 0x3F;
        v |= 0x80;
        if (k->ticker & 1) v |= 0x40;
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

static inline bool key_down(const Keyboard *k, int row, int bit) {
    return (k->row[row] & (u8)(1 << bit)) != 0;
}

/* Helper: OR a chord bit into byte `b` if any of N (row,bit) pairs are
 * currently pressed. The (row,bit) list is the PCW matrix position of
 * each PC key that participates in this chord. */
static inline u8 chord_or(const Keyboard *k, u8 b, int bit,
                          const int (*positions)[2], int n) {
    for (int i = 0; i < n; i++) {
        if (key_down(k, positions[i][0], positions[i][1])) {
            b |= (u8)(1 << bit);
            return b;
        }
    }
    return b;
}

void kbd_synth_joystick_chords(const Keyboard *k, u8 *win) {
    /* PCW keys are at the (row,bit) positions defined in sdl_to_matrix
     * above. Per PCW MiSTer rtl/key_joystick.sv:340-447 and the Seasip
     * Joyce hardware doc §10.3. We assume LK1/2/3 absent — so the
     * shift-as-fire2 chord and the W/A/D/X variant on 0x3FFC are not
     * synthesised (they require LK2). */

    /* 0x3FFC — inverted T around F1/F3 + ESC + KP_0 + SPACE + KP_ENTER */
    {
        u8 b = win[0xC] & 0xC0;     /* preserve b7-b6 (high keys) */
        if (key_down(k, 0, 0)) b |= 1u << 0;   /* F3/F4 → down */
        if (key_down(k, 0, 2)) b |= 1u << 1;   /* F1/F2 → up   */
        if (key_down(k, 1, 0)) b |= 1u << 2;   /* ESC    → left */
        if (key_down(k, 0, 1)) b |= 1u << 3;   /* KP_0   → right */
        if (key_down(k, 5, 7)) b |= 1u << 4;   /* SPACE  → fire1 */
        if (key_down(k,10, 5)) b |= 1u << 5;   /* KP_ENT → fire2 */
        win[0xC] |= b & 0x3F;
    }

    /* 0x3FFD — numeric keypad inverted T */
    {
        u8 b = 0;
        if (key_down(k,10, 2)) b |= 1u << 0;   /* KP_PERIOD → down */
        if (key_down(k, 0, 7)) b |= 1u << 1;   /* KP_5      → up   */
        if (key_down(k, 2, 4)) b |= 1u << 2;   /* KP_1      → left */
        if (key_down(k, 0, 4)) b |= 1u << 3;   /* KP_3      → right */
        if (key_down(k,10, 6)) b |= 1u << 4;   /* KP_2      → fire-ish */
        if (key_down(k, 5, 7)) b |= 1u << 5;   /* SPACE     → fire1 */
        win[0xD] |= b & 0x3F;
    }

    /* 0x3FFE — ASDFGHJ=up, ZXCVBNM=down, QEO[L,/=left, WRP];.\=right */
    {
        static const int up_keys[][2]    = {{8,5},{7,4},{7,5},{6,5},{6,4},{5,4},{5,5}};
        static const int down_keys[][2]  = {{8,7},{7,7},{7,6},{6,7},{6,6},{5,6},{4,6}};
        static const int left_keys[][2]  = {{8,3},{7,2},{4,2},{3,2},{4,4},{4,7},{3,6}};
        static const int right_keys[][2] = {{7,3},{6,2},{3,3},{2,1},{3,5},{3,7},{2,6}};
        u8 b = 0;
        b = chord_or(k, b, 0, up_keys,    7);
        b = chord_or(k, b, 1, down_keys,  7);
        b = chord_or(k, b, 2, left_keys,  7);
        b = chord_or(k, b, 3, right_keys, 7);
        if (key_down(k, 5, 7)) b |= 1u << 4;  /* SPACE → fire1 */
        /* fire2 (bit 5) requires LK2 (=Shift) — not synthesised */
        win[0xE] |= b & 0x3F;
    }

    /* 0x3FFF — HJKL;=up, BNM,./\=down, QEO[ADZC=left, WRP]SFXV=right */
    {
        static const int up_keys[][2]    = {{5,4},{5,5},{4,5},{4,4},{3,5}};
        static const int down_keys[][2]  = {{6,6},{5,6},{4,6},{4,7},{3,7},{3,6},{2,6}};
        static const int left_keys[][2]  = {{8,3},{7,2},{4,2},{3,2},{8,5},{7,5},{8,7},{7,6}};
        static const int right_keys[][2] = {{7,3},{6,2},{3,3},{2,1},{7,4},{6,5},{7,7},{6,7}};
        u8 b = 0;
        b = chord_or(k, b, 0, up_keys,    5);
        b = chord_or(k, b, 1, down_keys,  7);
        b = chord_or(k, b, 2, left_keys,  8);
        b = chord_or(k, b, 3, right_keys, 8);
        if (key_down(k, 5, 7)) b |= 1u << 4;  /* SPACE → fire1 */
        /* fire2 (bit 5) requires LK2 — not synthesised */
        win[0xF] |= b & 0x3F;
    }
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
    /* PCW f1..f8 are reached via Shift+F1..Shift+F8. On key-down we
     * only press the matrix bit when Shift is actually held — otherwise
     * F-keys are entirely the host's. On key-up we always release: if
     * the user lets go of Shift before the F-key, the F-key UP event
     * arrives with no Shift modifier, and we still need to clear the
     * matrix bit we set during the DOWN. */
    if (e->scancode >= SDL_SCANCODE_F1 && e->scancode <= SDL_SCANCODE_F8) {
        MatrixPos fp = pcw_fkey_pos(e->scancode);
        if (fp.row < 0) return;
        if (e->down) {
            if (!(e->mod & SDL_KMOD_SHIFT)) return;
            kbd_press(k, fp.row, fp.bit);
        } else {
            kbd_release(k, fp.row, fp.bit);
        }
        return;
    }

    MatrixPos p = sdl_to_matrix(e->scancode);
    if (p.row < 0) return;
    if (e->down) kbd_press  (k, p.row, p.bit);
    else         kbd_release(k, p.row, p.bit);
}
