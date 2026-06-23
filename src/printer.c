#include "printer.h"
#include "leds.h"

#if HAVE_CAIRO
#include <cairo.h>
#include <cairo-pdf.h>
#endif
#include <stdio.h>
#include <stdlib.h>     /* getenv */
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>      /* fork, execlp, dup2 */
#include <fcntl.h>       /* open, O_WRONLY */
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#endif

/* Spool the just-finalised PDF to the host's default printer via
 * CUPS' `lp`. Detaches: we don't wait on the child so the emulator
 * doesn't stutter while CUPS queues. Returns silently if lp isn't
 * on PATH (the user gets a console message). No-op on Windows. */
/* Returns true if we should capture print bytes — either to keep a
 * user-visible PDF (pdf_enabled) or to feed the Real Printer sink. */
static bool printer_capture_active(const Printer *p) {
    return p->pdf_enabled || p->sink == PRINT_SINK_REAL;
}

/* If wait_then_unlink is true, blocks until lp exits and then removes
 * the file — used when the PDF is ephemeral (/tmp file created only
 * because Real Printer was on without PDF extension enabled). */
static void printer_spool_to_lp(const char *pdf_path, bool wait_then_unlink) {
#ifdef _WIN32
    (void)pdf_path; (void)wait_then_unlink;
    fprintf(stderr, "printer: real-printer sink not implemented on Windows yet\n");
#else
    if (!pdf_path || !pdf_path[0]) return;
    pid_t pid = fork();
    if (pid < 0) {
        perror("printer: fork");
        return;
    }
    if (pid == 0) {
        /* Child: detach from emulator, exec lp. Redirect its stdout
         * and stderr to /dev/null so the "request id is ..." line
         * doesn't leak into our terminal; queuing failures are still
         * surfaced via lp's exit code (which we don't wait on but
         * CUPS will record). */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        execlp("lp", "lp", pdf_path, (char *)NULL);
        _exit(127);
    }
    /* Parent. For an ephemeral temp PDF we must outlive `lp` so we
     * can unlink the file once CUPS has read it into its spool
     * directory. lp typically returns in <100 ms; cost is one print
     * job's worth of latency. For a user-owned PDF, fire and forget
     * (SIGCHLD to SIG_IGN auto-reaps the zombie). */
    if (wait_then_unlink) {
        int status;
        waitpid(pid, &status, 0);
        unlink(pdf_path);
    } else {
        static bool signal_set = false;
        if (!signal_set) {
            signal(SIGCHLD, SIG_IGN);
            signal_set = true;
        }
    }
#endif
}

/* Joyce-compatible matrix-printer geometry. Coordinates are in 1/360in
 * internally; Cairo PDF uses points (1/72in), so divide by five. */
#define PAGE_W_360       2997.0f
#define PAGE_H_360       4208.0f
#define MAX_FORM_H_360   4440.0f
#define TOP_MARGIN_360    360.0f
#define PDF_SCALE            5.0f

#define FAST_STEP            1.0f
#define SLOW_STEP            0.5f
#define PIN_SPACING          4.6875f
#define K_ADJUST             9.0f
#define L_ADJUST            11.0f

/* The PCW print head normally homes to x=0 and the firmware then
 * issues a leftward 0xAA cmd[1]=0 (= 256 units - L_ADJUST) at the
 * start of each line. That puts the head briefly at negative x
 * before drawing-command spacing carries it back across the page,
 * so the leftmost ~1 character of every line gets clipped against
 * the page edge. Joyce draws onto an unbounded canvas and crops
 * later; we render straight to a fixed-size Cairo PDF surface, so
 * shift everything right by a small margin to give that left-side
 * excursion somewhere to land. */
#define LEFT_BLEED_360     320.0f

#define TEXT_MARGIN_PT      36.0f
#define TEXT_TOP_PT         54.0f
#define TEXT_FONT_PT        10.0f
#define TEXT_LINE_PT        12.0f
#define TEXT_CHAR_PT         6.0f

/* Idle-finalise: at 50 Hz, 100 frames = 2 s with no print activity.
 * On expiry the current PDF surface is finalised (trailer + xref
 * written) so viewers can open it; the next byte starts a new file. */
#define IDLE_FRAMES_TO_FINALISE 100

/* PDF surface is widened by LEFT_BLEED_360 so the leftward excursion
 * at line start fits without pushing the right edge of the page off-
 * canvas. PAGE_W_360 stays the PCW's logical print width; SURFACE_W
 * is what we hand to Cairo. */
#define SURFACE_W_360 (PAGE_W_360 + LEFT_BLEED_360)
static float page_w_pt(void) { return SURFACE_W_360 / PDF_SCALE; }
static float page_h_pt(void) { return PAGE_H_360 / PDF_SCALE; }

static void printer_reset_matrix(Printer *p) {
    p->cmd_pos = 0;
    p->mode = PRINTER_MODE_NORMAL;
    p->x = 0.0f;
    p->xstep = FAST_STEP;
    p->xdir = 1.0f;
    p->head_at_left = true;
}

static void printer_reset_text(Printer *p) {
    p->text_x = TEXT_MARGIN_PT;
    p->text_y = TEXT_TOP_PT;
    p->text_esc_skip = 0;
}

void printer_init(Printer *p) {
    memset(p, 0, sizeof(*p));
    p->connected = true;
    p->bail_in = true;
    p->paper_present = true;
    p->feeder_present = true;
    p->y = TOP_MARGIN_360;
    printer_reset_matrix(p);
    printer_reset_text(p);
}

static void printer_make_pdf_path(Printer *p) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char base[64];
    if (!lt || !strftime(base, sizeof(base), "1985-print-%Y%m%d-%H%M%S", lt))
        snprintf(base, sizeof(base), "1985-print");

    /* If the user has the PDF extension off but selected Real Printer,
     * we still need a file to feed lp — drop it in $TMPDIR and remember
     * to unlink after spooling. */
    p->pdf_ephemeral = !p->pdf_enabled;
    const char *tmp_env = p->pdf_ephemeral ? getenv("TMPDIR") : NULL;
    const char *dir = p->pdf_ephemeral ? (tmp_env && tmp_env[0] ? tmp_env : "/tmp")
                                       : (p->pdf_output_dir[0] ? p->pdf_output_dir : ".");
    size_t dir_len = strlen(dir);
    const char *sep = (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\'))
                    ? "" : "/";
    for (int n = 0; n < 100; n++) {
        if (n == 0)
            snprintf(p->pdf_path, sizeof(p->pdf_path), "%s%s%s.pdf", dir, sep, base);
        else
            snprintf(p->pdf_path, sizeof(p->pdf_path), "%s%s%s-%02d.pdf", dir, sep, base, n);

        FILE *f = fopen(p->pdf_path, "rb");
        if (!f) return;
        fclose(f);
    }

    snprintf(p->pdf_path, sizeof(p->pdf_path), "%s%s%s-last.pdf", dir, sep, base);
}

#if HAVE_CAIRO
static bool printer_pdf_open(Printer *p) {
    if (!printer_capture_active(p)) return false;
    if (p->pdf_cr) return true;

    printer_make_pdf_path(p);
    p->pdf_surface = cairo_pdf_surface_create(p->pdf_path, page_w_pt(), page_h_pt());
    cairo_status_t st = cairo_surface_status(p->pdf_surface);
    if (st != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "printer: PDF open failed for %s: %s\n",
                p->pdf_path, cairo_status_to_string(st));
        cairo_surface_destroy(p->pdf_surface);
        p->pdf_surface = NULL;
        p->pdf_path[0] = 0;
        return false;
    }

    p->pdf_cr = cairo_create(p->pdf_surface);
    st = cairo_status(p->pdf_cr);
    if (st != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "printer: PDF context failed for %s: %s\n",
                p->pdf_path, cairo_status_to_string(st));
        cairo_destroy(p->pdf_cr);
        cairo_surface_destroy(p->pdf_surface);
        p->pdf_cr = NULL;
        p->pdf_surface = NULL;
        p->pdf_path[0] = 0;
        return false;
    }

    cairo_set_source_rgb(p->pdf_cr, 0.0, 0.0, 0.0);
    return true;
}

static bool printer_pdf_ensure_page(Printer *p) {
    if (!printer_pdf_open(p)) return false;
    p->pdf_page_open = true;
    cairo_set_source_rgb(p->pdf_cr, 0.0, 0.0, 0.0);
    return true;
}

static void printer_pdf_show_page(Printer *p) {
    if (!p->pdf_cr || !p->pdf_page_open) return;
    cairo_show_page(p->pdf_cr);
    p->pdf_page_open = false;
    printer_reset_text(p);
}

void printer_shutdown(Printer *p) {
    /* Snapshot the path before clearing — we hand it to lp below. */
    char done_path[PATH_MAX] = {0};
    if (p->pdf_cr) {
        if (p->pdf_page_open)
            cairo_show_page(p->pdf_cr);
        cairo_destroy(p->pdf_cr);
        p->pdf_cr = NULL;
    }
    if (p->pdf_surface) {
        cairo_surface_finish(p->pdf_surface);
        cairo_surface_destroy(p->pdf_surface);
        p->pdf_surface = NULL;
        snprintf(done_path, sizeof(done_path), "%s", p->pdf_path);
    }
    p->pdf_page_open = false;
    p->pdf_path[0] = 0;

    if (p->sink == PRINT_SINK_REAL && done_path[0])
        printer_spool_to_lp(done_path, p->pdf_ephemeral);
    p->pdf_ephemeral = false;
}
#else  /* !HAVE_CAIRO */
static bool printer_pdf_ensure_page(Printer *p) { (void)p; return false; }
static void printer_pdf_show_page(Printer *p)   { (void)p; }
void printer_shutdown(Printer *p) {
    /* No Cairo, no surface, nothing to flush. */
    p->pdf_page_open = false;
    p->pdf_path[0] = 0;
}
#endif

void printer_set_sink(Printer *p, PrintSink sink) {
    p->sink = sink;
}

void printer_set_kind(Printer *p, PrinterKind kind) {
    p->kind = kind;
}

void printer_set_pdf_output_dir(Printer *p, const char *dir) {
    char next[PATH_MAX];
    snprintf(next, sizeof(next), "%s", (dir && dir[0]) ? dir : ".");
    if (strcmp(p->pdf_output_dir, next) == 0) return;

    printer_shutdown(p);
    snprintf(p->pdf_output_dir, sizeof(p->pdf_output_dir), "%s", next);
}

void printer_set_pdf_enabled(Printer *p, bool enabled) {
    if (p->pdf_enabled == enabled) return;
    if (!enabled)
        printer_shutdown(p);
    p->pdf_enabled = enabled;
}

static void printer_check_end_form(Printer *p) {
    if (p->y < MAX_FORM_H_360) return;
    printer_pdf_show_page(p);
    p->y = TOP_MARGIN_360;
}

static void printer_mark_active(Printer *p) {
    p->idle_countdown = IDLE_FRAMES_TO_FINALISE;
    leds_ping(LED_PRINTER);
}

void printer_tick(Printer *p) {
    if (!p->idle_countdown) return;
    if (--p->idle_countdown > 0) return;
    /* Finalise the in-progress PDF so the file becomes openable. The
     * next print byte will lazily create a fresh, timestamped file. */
#if HAVE_CAIRO
    if (p->pdf_cr || p->pdf_surface)
        printer_shutdown(p);
#endif
}

static void printer_dot(Printer *p, float xf, float yf) {
    /* PDF surface is PAGE_W_360 × PAGE_H_360; Cairo silently clips
     * anything outside, so skip dots in the bottom margin between
     * PAGE_H_360 and MAX_FORM_H_360 (the head can move that low but
     * the form feeds before those dots would actually mark paper).
     * Shift x by LEFT_BLEED_360 so the firmware's brief negative-x
     * excursion at the start of each line still lands on paper. */
    float xs = xf + LEFT_BLEED_360;
    if (xs < 0.0f || xs >= SURFACE_W_360 || yf < 0.0f || yf >= PAGE_H_360)
        return;
    if (!printer_pdf_ensure_page(p)) return;
    printer_mark_active(p);

#if HAVE_CAIRO
    double xp = xs / PDF_SCALE;
    double yp = yf / PDF_SCALE;
    cairo_rectangle(p->pdf_cr, xp - 0.5, yp - 0.5, 1.0, 1.0);
    cairo_fill(p->pdf_cr);
#endif
}

static void printer_dots(Printer *p, u8 cmd0, u8 cmd1) {
    u8 mask = 0x01;
    float y = p->y;
    for (int n = 0; n < 8; n++) {
        if (cmd1 & mask) printer_dot(p, p->x, y);
        mask <<= 1;
        y += PIN_SPACING;
    }
    if (cmd0 & 1) printer_dot(p, p->x, y);
}

static void printer_move_head(Printer *p, float headmove) {
    p->x += p->xdir * (headmove * p->xstep / 2.0f);
    p->head_at_left = (p->x <= 0.0f);
}

static void printer_obey_matrix_command(Printer *p, u8 port) {
    u8 cmd0 = p->cmd[0];
    u8 cmd1 = p->cmd[1];
    float headmove;

    if (p->mode == PRINTER_MODE_LINEFEED) {
        switch (cmd0) {
            case 0xC0:
                p->mode = PRINTER_MODE_NORMAL;
                return;
            case 0x80:
                p->y += 256.0f;
                printer_check_end_form(p);
                return;
            default:
                return;
        }
    }

    if (p->mode == PRINTER_MODE_PRINTING) {
        if (cmd0 >= 0x80 && cmd0 < 0xC0) {
            headmove = ((float)(cmd0 - 0x80) * 256.0f) + (cmd1 ? cmd1 : 256);
            printer_move_head(p, headmove);
            return;
        }

        if ((cmd0 & 0xC0) == 0) {
            float spacing = ((float)(cmd0 & 0x0E) / 2.0f) + 5.0f;
            printer_dots(p, cmd0, cmd1);
            printer_move_head(p, spacing);
            return;
        }

        if (cmd0 == 0xC0 && cmd1 == 0) {
            p->mode = PRINTER_MODE_NORMAL;
            printer_move_head(p, K_ADJUST);
            return;
        }

        return;
    }

    switch (cmd0) {
        case 0x00:
            if ((port & 0xFF) == 0xFC)
                return;
            break;
        case 0xC0:
            /* End-of-sequence sentinel — no-op in NORMAL mode. Joyce
             * logs it as "unknown" but real ROM code emits 0xC0 0x00
             * as the trailer after every print burst, so swallow it
             * silently to keep diagnostic builds quiet. */
            return;
        case 0xA4:
            p->y += cmd1;
            printer_check_end_form(p);
            return;
        case 0xA8:
        case 0xA9:
        case 0xAA:
        case 0xAB:
            p->mode = PRINTER_MODE_PRINTING;
            p->xdir = (cmd0 & 1) ? 1.0f : -1.0f;
            p->xstep = (cmd0 & 2) ? SLOW_STEP : FAST_STEP;
            headmove = (float)(cmd1 ? cmd1 : 256) - L_ADJUST;
            printer_move_head(p, headmove);
            return;
        case 0xAC:
            p->y += cmd1;
            p->mode = PRINTER_MODE_LINEFEED;
            printer_check_end_form(p);
            return;
        case 0xB8:
            if ((port & 0xFF) == 0xFC && cmd1 == 0) {
                printer_reset_matrix(p);
                return;
            }
            break;
        default:
            break;
    }
}

u8 printer_read(Printer *p, u8 port) {
    if (!p->connected) {
        if (port == 0xFC) return 0x01;
        return 0x21;    /* bail in + no printer */
    }

    if (port == 0xFC) return 0xF8;     /* no controller error */

    /* Port 0xFDh status bits (Joyce JoyceMatrix.hxx):
     *   0x80 BAIL  bail bar in
     *   0x40 READY controller ready
     *   0x20 NOPRINTER
     *   0x10 LCOL  head past left column
     *   0x08 FEEDER  sheet feeder present
     *   0x04 PAPER   paper present
     *   0x02 BUSY
     *   0x01 FAILED
     * CP/M+'s LST: handler reports "LPT not ready" when PAPER is
     * clear, so a bare 0x40 wasn't enough — BIOS thinks the printer
     * is out of paper. We need at minimum READY|FEEDER|PAPER. */
    u8 v = 0x40 | 0x08 | 0x04;          /* READY | FEEDER | PAPER */
    if (p->bail_in)          v |= 0x80; /* BAIL */
    if (p->x > 0.0f)         v |= 0x10; /* LCOL */
    return v;
}

void printer_write(Printer *p, u8 port, u8 val) {
    if (!p->connected) return;

    /* Every byte the guest writes counts as activity for the front-
     * panel LED, even if PDF capture is off and the command goes
     * through the decoder without drawing anything. */
    leds_ping(LED_PRINTER);

    /* 9512 ships a daisywheel — it has no dot addressing, so the
     * built-in printer port's matrix protocol is meaningless to it.
     * Real CP/M+ on a 9512 talks to the daisywheel via Centronics
     * (printer_write_centronics path); the dot-matrix commands here
     * just get dropped on the floor. The LED still pings above so
     * the user sees the port is being driven. */
    if (p->kind == PRINTER_KIND_DAISYWHEEL) {
        p->cmd_pos = 0;
        return;
    }

    p->cmd[p->cmd_pos++] = val;
    if (p->cmd_pos < 2) return;
    p->cmd_pos = 0;

    printer_obey_matrix_command(p, port);
}

static void printer_text_linefeed(Printer *p) {
    p->text_x = TEXT_MARGIN_PT;
    p->text_y += TEXT_LINE_PT;
    if (p->text_y > page_h_pt() - TEXT_MARGIN_PT) {
        printer_pdf_show_page(p);
        printer_reset_text(p);
    }
}

void printer_write_centronics(Printer *p, u8 val) {
    if (!p) return;
    leds_ping(LED_PRINTER);
    if (!printer_capture_active(p)) return;

    if (p->text_esc_skip > 0) {
        p->text_esc_skip--;
        return;
    }

    switch (val) {
        case 0x1B:  /* ESC: ignore a simple one-byte command prefix. */
            p->text_esc_skip = 1;
            return;
        case '\r':
            p->text_x = TEXT_MARGIN_PT;
            return;
        case '\n':
            printer_text_linefeed(p);
            return;
        case '\f':
            printer_pdf_show_page(p);
            return;
        case '\t': {
            int col = (int)((p->text_x - TEXT_MARGIN_PT) / TEXT_CHAR_PT);
            col = ((col / 8) + 1) * 8;
            p->text_x = TEXT_MARGIN_PT + (float)col * TEXT_CHAR_PT;
            return;
        }
        default:
            break;
    }

    if (val < 0x20 || val == 0x7F) return;
    if (!printer_pdf_ensure_page(p)) return;

#if HAVE_CAIRO
    cairo_select_font_face(p->pdf_cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(p->pdf_cr, TEXT_FONT_PT);
    cairo_move_to(p->pdf_cr, p->text_x, p->text_y);
    char s[2] = { (char)val, 0 };
    cairo_show_text(p->pdf_cr, s);
    printer_mark_active(p);
#endif

    p->text_x += TEXT_CHAR_PT;
    if (p->text_x > page_w_pt() - TEXT_MARGIN_PT)
        printer_text_linefeed(p);
}
