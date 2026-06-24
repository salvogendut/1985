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

/* Real PCW joystick interfaces, each living at its own I/O port:
 *
 *   DKSOUND   AY-3-8912 register 14 (read via port 0xA9). Active-low.
 *             L=2 R=3 D=4 U=5 F=6.
 *   KEMPSTON  Stand-alone latch at port 0x9F. Active-high.
 *             R=0 L=1 D=2 U=3 F=4.
 *   CASCADE   "Cascade" two-player adaptor at port 0xE0. Active-low.
 *             L=~0 R=~1 D=~2 U=~4 F=~7. (Head Over Heels uses this.)
 *
 * The dispatch lives in pcw.c's bus_io_read; aysound owns 0xA9 only. */
typedef enum {
    JOYSTICK_TYPE_DKSOUND = 0,
    JOYSTICK_TYPE_KEMPSTON,
    JOYSTICK_TYPE_CASCADE,
    JOYSTICK_TYPE_COUNT,
} JoystickType;
