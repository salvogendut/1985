#include <stdio.h>
#include <string.h>

#include "config.h"
#include "pcwmouse.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_mouse_enablement(void) {
    Config config;
    config_defaults(&config);
    config.ext_sanpollo_backplane = true;
    config.ext_dktronics = false;
    config.input_device = INPUT_DEVICE_MOUSE;

    for (int type = 0; type < MOUSE_TYPE_COUNT; type++) {
        config.mouse_type = (MouseType)type;
        CHECK(config_mouse_input_enabled(&config));
    }

    config.input_device = INPUT_DEVICE_JOYSTICK;
    CHECK(!config_mouse_input_enabled(&config));
    config.input_device = INPUT_DEVICE_MOUSE;
    config.ext_sanpollo_backplane = false;
    CHECK(!config_mouse_input_enabled(&config));
    return 0;
}

static int test_amx(void) {
    PcwMouse mouse;
    pcwmouse_init(&mouse, true, MOUSE_TYPE_AMX);
    pcwmouse_add_motion(&mouse, 8.0f, -8.0f);
    pcwmouse_set_button(&mouse, 0, true);

    CHECK(pcwmouse_handles_port(&mouse, 0xA0));
    CHECK(pcwmouse_handles_port(&mouse, 0xA3));
    CHECK(!pcwmouse_handles_port(&mouse, 0xD0));
    CHECK((pcwmouse_read(&mouse, 0xA1) & 0x0F) != 0);
    CHECK((pcwmouse_read(&mouse, 0xA0) & 0x0F) != 0);
    CHECK((pcwmouse_read(&mouse, 0xA2) & 0x01) == 0);

    pcwmouse_clear_input(&mouse);
    CHECK(pcwmouse_read(&mouse, 0xA0) == 0);
    CHECK(pcwmouse_read(&mouse, 0xA1) == 0);
    CHECK(pcwmouse_read(&mouse, 0xA2) == 0x07);
    return 0;
}

static int test_kempston(void) {
    PcwMouse mouse;
    pcwmouse_init(&mouse, true, MOUSE_TYPE_KEMPSTON);
    pcwmouse_add_motion(&mouse, 5.0f, 3.0f);
    pcwmouse_set_button(&mouse, 0, true);
    pcwmouse_set_button(&mouse, 2, true);

    CHECK(!pcwmouse_handles_port(&mouse, 0xA0));
    CHECK(pcwmouse_handles_port(&mouse, 0xD0));
    CHECK(pcwmouse_handles_port(&mouse, 0xD4));
    CHECK(pcwmouse_read(&mouse, 0xD0) == 5);
    CHECK(pcwmouse_read(&mouse, 0xD1) == (u8)-3);
    CHECK((pcwmouse_read(&mouse, 0xD4) & 0x03) == 0);

    pcwmouse_clear_input(&mouse);
    CHECK(pcwmouse_read(&mouse, 0xD4) == 0xFF);
    return 0;
}

static int test_keymouse(void) {
    PcwMouse mouse;
    u8 keyboard[16];
    memset(keyboard, 0xFF, sizeof(keyboard));

    pcwmouse_init(&mouse, true, MOUSE_TYPE_KEYMOUSE);
    pcwmouse_add_motion(&mouse, 16.0f, 8.0f);
    pcwmouse_set_button(&mouse, 0, true);
    pcwmouse_set_button(&mouse, 1, true);
    pcwmouse_set_button(&mouse, 2, true);
    pcwmouse_overlay_kbd(&mouse, keyboard);

    CHECK(!pcwmouse_handles_port(&mouse, 0xA0));
    CHECK(!pcwmouse_handles_port(&mouse, 0xD0));
    CHECK(keyboard[0xB] == 0x82);
    CHECK(keyboard[0xC] == 0x3F);
    CHECK(keyboard[0xD] == 0xE1);
    CHECK(keyboard[0xE] == 0xFF);
    return 0;
}

int main(void) {
    if (test_mouse_enablement() != 0) return 1;
    if (test_amx() != 0) return 1;
    if (test_kempston() != 0) return 1;
    if (test_keymouse() != 0) return 1;
    return 0;
}
