#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

enum joystick_direction {
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_FORWARD,
    JOY_BACKWARD,
};

struct typeglide_joystick_state_changed {
    enum joystick_direction direction;
};

ZMK_EVENT_DECLARE(typeglide_joystick_state_changed);
