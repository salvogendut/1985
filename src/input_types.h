#pragma once

typedef enum {
    INPUT_DEVICE_JOYSTICK = 0,
    INPUT_DEVICE_MOUSE,
} InputDevice;

typedef enum {
    MOUSE_TYPE_AMX = 0,
    MOUSE_TYPE_KEMPSTON,
    MOUSE_TYPE_COUNT,
} MouseType;

typedef enum {
    JOYSTICK_TYPE_DKSOUND = 0,
    JOYSTICK_TYPE_ATARI,
    JOYSTICK_TYPE_COUNT,
} JoystickType;
