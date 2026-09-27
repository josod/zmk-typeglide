#pragma once

#include <zmk/event_manager.h>

struct typeglide_gesture_timeout {
    zmk_event_t header;
};

ZMK_EVENT_DECLARE(typeglide_gesture_timeout);
