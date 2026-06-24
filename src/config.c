#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir((p), 0755)
#endif

static const char *mono_to_str(MonoMode m) {
    switch (m) {
        case MONO_GREEN: return "green";
        case MONO_AMBER: return "amber";
        case MONO_WHITE: return "white";
        case MONO_OFF:
        default:         return "off";
    }
}

static MonoMode parse_mono(const char *s, MonoMode fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "off")   == 0) return MONO_OFF;
    if (strcasecmp(s, "green") == 0) return MONO_GREEN;
    if (strcasecmp(s, "amber") == 0) return MONO_AMBER;
    if (strcasecmp(s, "white") == 0) return MONO_WHITE;
    fprintf(stderr, "config: unknown monochrome '%s' — using default\n", s);
    return fallback;
}

static const char *video_to_str(VideoMode v) {
    switch (v) {
        case VIDEO_CGA1: return "cga1";
        case VIDEO_CGA2: return "cga2";
        case VIDEO_EGA:  return "ega";
        case VIDEO_PCW:
        default:         return "pcw";
    }
}

static const char *sink_to_str(PrintSink s) {
    return (s == PRINT_SINK_REAL) ? "real" : "pdf";
}

static PrintSink parse_sink(const char *s, PrintSink fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "pdf")  == 0) return PRINT_SINK_PDF;
    if (strcasecmp(s, "real") == 0) return PRINT_SINK_REAL;
    fprintf(stderr, "config: unknown print_sink '%s' — using default\n", s);
    return fallback;
}

static VideoMode parse_video(const char *s, VideoMode fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "pcw")  == 0) return VIDEO_PCW;
    if (strcasecmp(s, "cga")  == 0) return VIDEO_CGA1;   /* legacy alias */
    if (strcasecmp(s, "cga1") == 0) return VIDEO_CGA1;
    if (strcasecmp(s, "cga2") == 0) return VIDEO_CGA2;
    if (strcasecmp(s, "ega")  == 0) return VIDEO_EGA;
    fprintf(stderr, "config: unknown video_mode '%s' — using default\n", s);
    return fallback;
}

static const char *input_device_to_str(InputDevice input) {
    return input == INPUT_DEVICE_MOUSE ? "mouse" : "joystick";
}

static InputDevice parse_input_device(const char *s, InputDevice fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "joystick") == 0) return INPUT_DEVICE_JOYSTICK;
    if (strcasecmp(s, "mouse")    == 0) return INPUT_DEVICE_MOUSE;
    fprintf(stderr, "config: unknown input_device '%s' — using default\n", s);
    return fallback;
}

static const char *mouse_type_to_str(MouseType type) {
    switch (type) {
        case MOUSE_TYPE_KEMPSTON: return "kempston";
        case MOUSE_TYPE_KEYMOUSE: return "keymouse";
        default:                  return "amx";
    }
}

static MouseType parse_mouse_type(const char *s, MouseType fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "amx")      == 0) return MOUSE_TYPE_AMX;
    if (strcasecmp(s, "kempston") == 0) return MOUSE_TYPE_KEMPSTON;
    if (strcasecmp(s, "keymouse") == 0) return MOUSE_TYPE_KEYMOUSE;
    fprintf(stderr, "config: unknown mouse_type '%s' — using default\n", s);
    return fallback;
}

static const char *joystick_type_to_str(JoystickType type) {
    switch (type) {
        case JOYSTICK_TYPE_KEMPSTON: return "kempston";
        case JOYSTICK_TYPE_CASCADE:  return "cascade";
        default:                     return "dksound";
    }
}

static JoystickType parse_joystick_type(const char *s,
                                        JoystickType fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "dksound") == 0
        || strcasecmp(s, "dk_sound") == 0
        || strcasecmp(s, "dktronics") == 0)
        return JOYSTICK_TYPE_DKSOUND;
    if (strcasecmp(s, "kempston") == 0) return JOYSTICK_TYPE_KEMPSTON;
    if (strcasecmp(s, "cascade")  == 0) return JOYSTICK_TYPE_CASCADE;
    /* Legacy alias: the fictional "atari" mode never matched real PCW
     * hardware. Silently migrate it to the closest stand-alone latch. */
    if (strcasecmp(s, "atari") == 0) return JOYSTICK_TYPE_KEMPSTON;
    fprintf(stderr, "config: unknown joystick_type '%s' — using default\n", s);
    return fallback;
}

static const char *model_to_str(PcwModel m) {
    switch (m) {
        case PCW_MODEL_8512: return "8512";
        case PCW_MODEL_9512: return "9512";
        case PCW_MODEL_8256:
        default:             return "8256";
    }
}

static PcwModel parse_model(const char *s, PcwModel fallback) {
    if (!s) return fallback;
    if (strcmp(s, "8256") == 0) return PCW_MODEL_8256;
    if (strcmp(s, "8512") == 0) return PCW_MODEL_8512;
    if (strcmp(s, "9512") == 0) return PCW_MODEL_9512;
    fprintf(stderr, "config: unknown model '%s' — using default\n", s);
    return fallback;
}

static bool parse_bool(const char *s, bool fallback) {
    if (!s) return fallback;
    if (strcasecmp(s, "true")  == 0 || strcmp(s, "1") == 0) return true;
    if (strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0) return false;
    return fallback;
}

static const char *bool_to_str(bool b) { return b ? "true" : "false"; }

static void default_path(char *out, size_t out_size) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(out, out_size, "%s/.config/1985/1985.conf", home);
}

static void ensure_parent(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash) return;
    *slash = 0;
    /* Walk up creating each missing directory. */
    char *p = tmp;
    if (*p == '/') p++;
    while ((p = strchr(p, '/')) != NULL) {
        *p = 0;
        MKDIR(tmp);
        *p = '/';
        p++;
    }
    MKDIR(tmp);
}

void config_defaults(Config *c) {
    memset(c, 0, sizeof(*c));
    c->model                = PCW_MODEL_8256;
    c->memory_kb            = 256;
    c->scale                = 1;
    c->fullscreen           = false;
    c->fullscreen_smoothing = true;
    c->monochrome           = MONO_GREEN;
    c->input_device         = INPUT_DEVICE_JOYSTICK;
    c->mouse_type           = MOUSE_TYPE_AMX;
    c->joystick_type        = JOYSTICK_TYPE_DKSOUND;
    c->tinker               = false;
    c->debug                = false;
    snprintf(c->ext_serial_backend, sizeof(c->ext_serial_backend), "pty");
    c->ext_serial_tcp_port  = 4002;
    snprintf(c->ext_serial_pty_link, sizeof(c->ext_serial_pty_link),
             "/tmp/1985-serial");
}

void config_load(Config *c, const char *path) {
    config_defaults(c);
    if (path) snprintf(c->path, sizeof(c->path), "%s", path);
    else      default_path(c->path, sizeof(c->path));

    FILE *f = fopen(c->path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#' || *p == ';' || *p == '\n' || *p == '[')
            continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p, *v = eq + 1;
        char *nl = strchr(v, '\n');
        if (nl) *nl = 0;

        /* Trim trailing whitespace from key */
        char *end = k + strlen(k);
        while (end > k && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = 0;
        while (*v == ' ' || *v == '\t') v++;
        end = v + strlen(v);
        while (end > v && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *(--end) = 0;

        if      (strcmp(k, "model")                == 0) c->model = parse_model(v, c->model);
        else if (strcmp(k, "memory_kb")            == 0) c->memory_kb = atoi(v);
        else if (strcmp(k, "drive_a")              == 0) snprintf(c->drive_a, sizeof(c->drive_a), "%s", v);
        else if (strcmp(k, "drive_b")              == 0) snprintf(c->drive_b, sizeof(c->drive_b), "%s", v);
        else if (strcmp(k, "scale")                == 0) c->scale = atoi(v);
        else if (strcmp(k, "fullscreen")           == 0) c->fullscreen = parse_bool(v, c->fullscreen);
        else if (strcmp(k, "fullscreen_smoothing") == 0) c->fullscreen_smoothing = parse_bool(v, c->fullscreen_smoothing);
        else if (strcmp(k, "monochrome")           == 0) c->monochrome = parse_mono(v, c->monochrome);
        else if (strcmp(k, "tint_glow")            == 0) c->tint_glow  = parse_bool(v, c->tint_glow);
        else if (strcmp(k, "video_mode")           == 0) c->video_mode = parse_video(v, c->video_mode);
        else if (strcmp(k, "ext_second_drive")        == 0) c->ext_second_drive        = parse_bool(v, c->ext_second_drive);
        else if (strcmp(k, "ext_sanpollo_backplane")  == 0) c->ext_sanpollo_backplane  = parse_bool(v, c->ext_sanpollo_backplane);
        else if (strcmp(k, "ext_serial")              == 0) c->ext_serial              = parse_bool(v, c->ext_serial);
        else if (strcmp(k, "ext_serial_backend")      == 0) {
            if (!strcmp(v, "pty") || !strcmp(v, "tcp"))
                snprintf(c->ext_serial_backend, sizeof(c->ext_serial_backend), "%s", v);
        }
        else if (strcmp(k, "ext_serial_tcp_port")     == 0) {
            int p = atoi(v);
            if (p > 0 && p < 65536) c->ext_serial_tcp_port = p;
        }
        else if (strcmp(k, "ext_serial_pty_link")     == 0) {
            /* Blank value → fall back to the default already stashed
             * by config_defaults so users don't accidentally disable
             * the symlink by clearing the field. */
            if (v[0])
                snprintf(c->ext_serial_pty_link,
                         sizeof(c->ext_serial_pty_link), "%s", v);
        }
        else if (strcmp(k, "ext_perryfi")             == 0) c->ext_perryfi             = parse_bool(v, c->ext_perryfi);
        else if (strcmp(k, "ext_dktronics")           == 0) c->ext_dktronics           = parse_bool(v, c->ext_dktronics);
        else if (strcmp(k, "input_device")             == 0) c->input_device = parse_input_device(v, c->input_device);
        else if (strcmp(k, "ext_pdf_printer")         == 0) c->ext_pdf_printer         = parse_bool(v, c->ext_pdf_printer);
        else if (strcmp(k, "ext_pdf_printer_dir")     == 0) snprintf(c->ext_pdf_printer_dir,
                                                                      sizeof(c->ext_pdf_printer_dir), "%s", v);
        else if (strcmp(k, "ext_print_sink")          == 0) c->ext_print_sink = parse_sink(v, c->ext_print_sink);
        else if (strcmp(k, "printer_centronics_9512") == 0) c->printer_centronics_9512 = parse_bool(v, c->printer_centronics_9512);
        else if (strcmp(k, "tinker")               == 0) c->tinker = parse_bool(v, c->tinker);
        else if (strcmp(k, "debug")                == 0) c->debug  = parse_bool(v, c->debug);
        else if (strcmp(k, "debug_traces")         == 0) c->debug_traces = parse_bool(v, c->debug_traces);
        else if (strcmp(k, "trace_io")             == 0) c->trace_io    = parse_bool(v, c->trace_io);
        else if (strcmp(k, "trace_fdc")            == 0) c->trace_fdc   = parse_bool(v, c->trace_fdc);
        else if (strcmp(k, "trace_input")          == 0) c->trace_input = parse_bool(v, c->trace_input);
        else if (strcmp(k, "mouse_type")           == 0) c->mouse_type = parse_mouse_type(v, c->mouse_type);
        else if (strcmp(k, "joystick_type")        == 0) c->joystick_type = parse_joystick_type(v, c->joystick_type);
        /* Compatibility with the short-lived development key. */
        else if (strcmp(k, "dksound_input")        == 0) {
            c->input_device = (strcasecmp(v, "amx_mouse") == 0
                               || strcasecmp(v, "amx") == 0
                               || strcasecmp(v, "mouse") == 0)
                            ? INPUT_DEVICE_MOUSE : INPUT_DEVICE_JOYSTICK;
        }
    }
    fclose(f);

    if (c->scale     < 1) c->scale     = 1;
    if (c->scale     > 4) c->scale     = 4;
    if (c->memory_kb < 256) c->memory_kb = 256;
}

int config_save(const Config *c) {
    ensure_parent(c->path);
    FILE *f = fopen(c->path, "w");
    if (!f) { perror("config_save"); return -1; }

    fprintf(f, "# 1985 — Amstrad PCW 8256 / 8512 / 9512 emulator\n");
    fprintf(f, "# Edited automatically by the F9 overlay.\n\n");

    fprintf(f, "[machine]\n");
    fprintf(f, "model = %s\n", model_to_str(c->model));
    fprintf(f, "memory_kb = %d\n\n", c->memory_kb);

    fprintf(f, "[storage]\n");
    fprintf(f, "drive_a = %s\n", c->drive_a);
    fprintf(f, "drive_b = %s\n\n", c->drive_b);

    fprintf(f, "[display]\n");
    fprintf(f, "scale = %d\n", c->scale);
    fprintf(f, "fullscreen = %s\n", bool_to_str(c->fullscreen));
    fprintf(f, "fullscreen_smoothing = %s\n", bool_to_str(c->fullscreen_smoothing));
    fprintf(f, "monochrome = %s\n", mono_to_str(c->monochrome));
    fprintf(f, "tint_glow  = %s\n", bool_to_str(c->tint_glow));
    fprintf(f, "video_mode = %s\n\n", video_to_str(c->video_mode));

    fprintf(f, "[extensions]\n");
    fprintf(f, "ext_second_drive        = %s\n",   bool_to_str(c->ext_second_drive));
    fprintf(f, "ext_sanpollo_backplane  = %s\n",   bool_to_str(c->ext_sanpollo_backplane));
    fprintf(f, "ext_serial              = %s\n",   bool_to_str(c->ext_serial));
    fprintf(f, "ext_serial_backend      = %s\n",   c->ext_serial_backend);
    fprintf(f, "ext_serial_tcp_port     = %d\n",   c->ext_serial_tcp_port);
    fprintf(f, "ext_serial_pty_link     = %s\n",   c->ext_serial_pty_link);
    fprintf(f, "ext_perryfi             = %s\n",   bool_to_str(c->ext_perryfi));
    fprintf(f, "ext_dktronics           = %s\n",   bool_to_str(c->ext_dktronics));
    fprintf(f, "input_device            = %s\n",   input_device_to_str(c->input_device));
    fprintf(f, "ext_pdf_printer         = %s\n",   bool_to_str(c->ext_pdf_printer));
    fprintf(f, "ext_pdf_printer_dir     = %s\n",   c->ext_pdf_printer_dir);
    fprintf(f, "ext_print_sink          = %s\n",   sink_to_str(c->ext_print_sink));
    fprintf(f, "printer_centronics_9512 = %s\n\n", bool_to_str(c->printer_centronics_9512));

    fprintf(f, "[advanced]\n");
    fprintf(f, "tinker = %s\n", bool_to_str(c->tinker));
    fprintf(f, "debug = %s\n", bool_to_str(c->debug));
    fprintf(f, "debug_traces = %s\n", bool_to_str(c->debug_traces));
    fprintf(f, "trace_io = %s\n", bool_to_str(c->trace_io));
    fprintf(f, "trace_fdc = %s\n", bool_to_str(c->trace_fdc));
    fprintf(f, "trace_input = %s\n", bool_to_str(c->trace_input));
    fprintf(f, "mouse_type = %s\n", mouse_type_to_str(c->mouse_type));
    fprintf(f, "joystick_type = %s\n", joystick_type_to_str(c->joystick_type));

    fclose(f);
    return 0;
}
