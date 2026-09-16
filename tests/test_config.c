#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#define TEST_CONFIG_PATH "test-config-reset.conf"

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

int main(void) {
    remove(TEST_CONFIG_PATH);

    Config cfg;
    config_defaults(&cfg);
    assert(cfg.joystick_hidapi);

    snprintf(cfg.path, sizeof(cfg.path), "%s", TEST_CONFIG_PATH);
    cfg.model = PCW_MODEL_9512;
    cfg.memory_kb = 2048;
    cfg.ext_serial = true;
    cfg.ext_perryfi = true;
    cfg.joystick_hidapi = false;
    snprintf(cfg.drive_a, sizeof(cfg.drive_a), "custom.dsk");
    assert(config_save(&cfg) == 0);

    assert(config_reset_defaults(&cfg) == 0);
    assert(strcmp(cfg.path, TEST_CONFIG_PATH) == 0);
    assert(cfg.model == PCW_MODEL_8256);
    assert(cfg.memory_kb == 256);
    assert(!cfg.ext_serial);
    assert(!cfg.ext_perryfi);
    assert(cfg.drive_a[0] == '\0');
    assert(cfg.joystick_hidapi);
    assert(strcmp(cfg.ext_serial_backend, "pty") == 0);

    Config loaded;
    config_load(&loaded, TEST_CONFIG_PATH);
    assert(strcmp(loaded.path, TEST_CONFIG_PATH) == 0);
    assert(loaded.model == PCW_MODEL_8256);
    assert(loaded.memory_kb == 256);
    assert(loaded.drive_a[0] == '\0');
    assert(loaded.joystick_hidapi);
    assert(file_contains(TEST_CONFIG_PATH, "joystick_hidapi = true"));
    assert(!file_contains(TEST_CONFIG_PATH, "custom.dsk"));

    remove(TEST_CONFIG_PATH);
    puts("config reset tests passed");
    return 0;
}
