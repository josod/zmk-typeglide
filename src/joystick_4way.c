/*

* Copyright (c) 2026 Typeglide
*
* SPDX-License-Identifier: MIT
  */

#define DT_DRV_COMPAT typeglide_joystick_4way

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/virtual_key_position.h>
#include <zephyr/input/input_analog_axis.h>

static void joystick_raw_cb(const struct device *dev,
int channel,
int16_t raw_val);

LOG_MODULE_REGISTER(typeglide_joystick_4way, CONFIG_ZMK_LOG_LEVEL);

enum joystick_direction {
JOY_NONE = 0,
JOY_UP,
JOY_DOWN,
JOY_FORWARD,
JOY_BACKWARD,
};

struct joystick_4way_config {
int32_t threshold;
int32_t rotation_deg;
int32_t hysteresis_deg;

uint8_t layer_count;
size_t binding_count;
const struct zmk_behavior_binding *bindings;


};

struct joystick_4way_data {
    int32_t x;
    int32_t y;

    bool have_x;
    bool have_y;

    bool x_updated;
    bool y_updated;

    enum joystick_direction direction;

    int active_binding_index;
    zmk_keymap_layer_index_t active_layer;
};

/*

* Current calibrated center from Typeglide testing.
  */
  #define JOYSTICK_CENTER_X 127
  #define JOYSTICK_CENTER_Y 127

#define ROT_SCALE 1000

/* ------------------------------------------------------------------------- */
/* Small integer helpers                                                     */
/* ------------------------------------------------------------------------- */

static int32_t abs32(int32_t value) {
return value < 0 ? -value : value;
}

/* ------------------------------------------------------------------------- */
/* Rotation                                                                  */
/* ------------------------------------------------------------------------- */

static void rotate_vector(
int32_t dx,
int32_t dy,
int32_t rotation_deg,
int32_t *rx,
int32_t *ry) {


switch (rotation_deg) {

case 0:
    *rx = dx;
    *ry = dy;
    return;

case 1:
    *rx = (dx * 1000 - dy * 17) / 1000;
    *ry = (dx * 17 + dy * 1000) / 1000;
    return;

case 2:
    *rx = (dx * 999 - dy * 35) / 1000;
    *ry = (dx * 35 + dy * 999) / 1000;
    return;

case 3:
    *rx = (dx * 999 - dy * 52) / 1000;
    *ry = (dx * 52 + dy * 999) / 1000;
    return;

case 4:
    *rx = (dx * 998 - dy * 70) / 1000;
    *ry = (dx * 70 + dy * 998) / 1000;
    return;

case 5:
    *rx = (dx * 996 - dy * 87) / 1000;
    *ry = (dx * 87 + dy * 996) / 1000;
    return;

case -1:
    *rx = (dx * 1000 + dy * 17) / 1000;
    *ry = (-dx * 17 + dy * 1000) / 1000;
    return;

case -2:
    *rx = (dx * 999 + dy * 35) / 1000;
    *ry = (-dx * 35 + dy * 999) / 1000;
    return;

case -3:
    *rx = (dx * 999 + dy * 52) / 1000;
    *ry = (-dx * 52 + dy * 999) / 1000;
    return;

case -4:
    *rx = (dx * 998 + dy * 70) / 1000;
    *ry = (-dx * 70 + dy * 998) / 1000;
    return;

case -5:
    *rx = (dx * 996 + dy * 87) / 1000;
    *ry = (-dx * 87 + dy * 996) / 1000;
    return;

default:
    LOG_WRN("Unsupported rotation %d, using 0",
            rotation_deg);

    *rx = dx;
    *ry = dy;
    return;
}


}

/* ------------------------------------------------------------------------- */
/* Sector classification                                                     */
/* ------------------------------------------------------------------------- */

static enum joystick_direction direction_from_vector(
int32_t x,
int32_t y) {


int32_t ax = abs32(x);
int32_t ay = abs32(y);

/*
 * X axis:
 *
 *   X- = FORWARD
 *   X+ = BACKWARD
 */
if (ax >= ay) {
    return x >= 0 ? JOY_BACKWARD : JOY_FORWARD;
}

/*
 * Y axis:
 *
 *   Y- = UP
 *   Y+ = DOWN
 */
return y >= 0 ? JOY_DOWN : JOY_UP;


}

static void direction_vector(
enum joystick_direction direction,
int32_t *x,
int32_t *y) {


switch (direction) {

case JOY_UP:
    *x = 0;
    *y = -1;
    break;

case JOY_DOWN:
    *x = 0;
    *y = 1;
    break;

case JOY_FORWARD:
    *x = -1;
    *y = 0;
    break;

case JOY_BACKWARD:
    *x = 1;
    *y = 0;
    break;

default:
    *x = 0;
    *y = 0;
    break;
}


}

static bool direction_is_stable(
enum joystick_direction direction,
int32_t x,
int32_t y,
int32_t hysteresis_deg) {


if (direction == JOY_NONE) {
    return false;
}

int32_t forward;
int32_t sideways;

switch (direction) {

case JOY_BACKWARD:
    forward = x;
    sideways = abs32(y);
    break;

case JOY_FORWARD:
    forward = -x;
    sideways = abs32(y);
    break;

case JOY_DOWN:
    forward = y;
    sideways = abs32(x);
    break;

case JOY_UP:
    forward = -y;
    sideways = abs32(x);
    break;

default:
    return false;
}

if (forward <= 0) {
    return false;
}

int32_t margin;

switch (hysteresis_deg) {
case 0:
    margin = 0;
    break;

case 5:
    margin = 9;
    break;

case 10:
    margin = 18;
    break;

case 15:
    margin = 27;
    break;

case 20:
    margin = 36;
    break;

default:
    margin = hysteresis_deg * 18 / 10;
    break;
}

return forward >= (sideways - margin);


}

/* ------------------------------------------------------------------------- */
/* Direction calculation                                                     */
/* ------------------------------------------------------------------------- */

static enum joystick_direction joystick_get_direction(
const struct joystick_4way_config *cfg,
const struct joystick_4way_data *data) {


int32_t dx = data->x - JOYSTICK_CENTER_X;
int32_t dy = data->y - JOYSTICK_CENTER_Y;

LOG_DBG(
    "4WAY: raw x=%d y=%d dx=%d dy=%d threshold=%d",
    data->x,
    data->y,
    dx,
    dy,
    cfg->threshold);

int32_t magnitude_squared =
    dx * dx + dy * dy;

int32_t activation_threshold =
    cfg->threshold;

int32_t release_threshold =
    cfg->threshold + 8;

int32_t threshold =
    (data->direction == JOY_NONE)
        ? activation_threshold
        : release_threshold;

if (magnitude_squared < threshold * threshold) {
    return JOY_NONE;
}

int32_t rx;
int32_t ry;

rotate_vector(
    dx,
    dy,
    cfg->rotation_deg,
    &rx,
    &ry);

LOG_DBG(
    "4WAY: rotated x=%d y=%d rotation=%d",
    rx,
    ry,
    cfg->rotation_deg);

if (data->direction != JOY_NONE) {

    if (direction_is_stable(
            data->direction,
            rx,
            ry,
            cfg->hysteresis_deg)) {

        return data->direction;
    }
}

enum joystick_direction direction =
    direction_from_vector(rx, ry);

LOG_DBG(
    "4WAY: vector rx=%d ry=%d -> direction=%d",
    rx,
    ry,
    direction);

return direction;


}

/* ------------------------------------------------------------------------- */
/* ZMK behavior invocation                                                   */
/* ------------------------------------------------------------------------- */

static int joystick_invoke(
    const struct device *dev,
    const struct joystick_4way_config *cfg,
    struct joystick_4way_data *data,
    struct zmk_input_processor_state *state,
    enum joystick_direction direction,
    bool pressed) {

    ARG_UNUSED(dev);

    if (direction == JOY_NONE) {
        return 0;
    }

    /*
     * Four bindings per layer:
     *
     *   UP
     *   DOWN
     *   FORWARD
     *   BACKWARD
     */
    const size_t directions_per_layer = 4;

    size_t binding_index;

    if (pressed) {
        /*
         * Select the binding from the currently active layer.
         */
        zmk_keymap_layer_index_t layer =
            zmk_keymap_highest_layer_active();


        /*
         * If the active ZMK layer has no joystick mapping,
         * fall back to layer 0 for now.
         */
        if (layer >= cfg->layer_count) {
            LOG_DBG(
                "4WAY: layer %d has no joystick mapping, using layer 0",
                layer);

            layer = 0;
        }

        binding_index =
            ((size_t)layer * directions_per_layer) +
            (direction - 1);

        if (binding_index >= cfg->binding_count) {
            LOG_ERR(
                "No joystick binding: layer=%d direction=%d index=%d",
                layer,
                direction,
                binding_index);

            return -EINVAL;
        }

        /*
         * Remember exactly what we pressed.
         *
         * This is important if the layer changes while
         * the joystick is still held.
         */
        data->active_binding_index = binding_index;
        data->active_layer = layer;

    } else {

        /*
         * Release the exact binding that was pressed.
         * Do NOT look at the current layer here.
         */
        if (data->active_binding_index < 0 ||
            data->active_binding_index >= cfg->binding_count) {

            LOG_ERR(
                "No active joystick binding to release");

            return -EINVAL;
        }

        binding_index = data->active_binding_index;
    }

    const struct zmk_behavior_binding *binding =
        &cfg->bindings[binding_index];

    struct zmk_behavior_binding_event behavior_event = {
        .position =
            ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
                state->input_device_index,
                direction - 1),
        .layer = zmk_keymap_layer_index_to_id(data->active_layer),
        .timestamp = k_uptime_get(),

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    LOG_DBG(
        "4WAY: %s direction=%d binding=%d layer=%d",
        pressed ? "PRESS" : "RELEASE",
        direction,
        binding_index,
        pressed ? zmk_keymap_highest_layer_active()
                : data->active_layer);

    int ret =
        zmk_behavior_invoke_binding(
            binding,
            behavior_event,
            pressed);

    if (!pressed) {
        /*
         * The direction is no longer held.
         */
        data->active_binding_index = -1;
    }

    return ret;
}

/* ------------------------------------------------------------------------- */
/* State transition                                                          */
/* ------------------------------------------------------------------------- */

static int joystick_4way_change_direction(
const struct device *dev,
const struct joystick_4way_config *cfg,
struct joystick_4way_data *data,
struct zmk_input_processor_state *state,
enum joystick_direction new_direction) {


if (new_direction == data->direction) {
    return 0;
}

if (data->direction != JOY_NONE) {

    int ret = joystick_invoke(
        dev,
        cfg,
        data,
        state,
        data->direction,
        false);

    if (ret < 0) {
        return ret;
    }
}

data->direction = new_direction;

if (new_direction != JOY_NONE) {

    int ret = joystick_invoke(
        dev,
        cfg,
        data,
        state,
        new_direction,
        true);

    if (ret < 0) {
        data->direction = JOY_NONE;
        return ret;
    }
}

LOG_DBG(
    "4WAY: direction changed -> %d",
    new_direction);

return 0;


}

/* ------------------------------------------------------------------------- */
/* Input processor                                                           */
/* ------------------------------------------------------------------------- */

static int joystick_4way_handle_event(
const struct device *dev,
struct input_event *event,
uint32_t param1,
uint32_t param2,
struct zmk_input_processor_state *state) {


const struct joystick_4way_config *cfg =
    dev->config;

struct joystick_4way_data *data =
    dev->data;

ARG_UNUSED(param1);
ARG_UNUSED(param2);

if (event->type != INPUT_EV_ABS) {
    return ZMK_INPUT_PROC_CONTINUE;
}

switch (event->code) {

case INPUT_ABS_X:
    data->x = event->value;
    data->have_x = true;
    data->x_updated = true;
    break;

case INPUT_ABS_Y:
    data->y = event->value;
    data->have_y = true;
    data->y_updated = true;
    break;

default:
    return ZMK_INPUT_PROC_CONTINUE;
}

LOG_INF("4WAY _____________________________________________________________ EVENT: type=%d code=%d value=%d",
        event->type,
        event->code,
        event->value);

if (!data->have_x || !data->have_y) {
    return ZMK_INPUT_PROC_STOP;
}

if (!data->x_updated || !data->y_updated) {
    return ZMK_INPUT_PROC_STOP;
}

data->x_updated = false;
data->y_updated = false;

enum joystick_direction direction =
    joystick_get_direction(
        cfg,
        data);

LOG_DBG(
    "4WAY: x=%d y=%d -> direction=%d",
    data->x,
    data->y,
    direction);

int ret = joystick_4way_change_direction(
    dev,
    cfg,
    data,
    state,
    direction);

if (ret < 0) {
    LOG_ERR(
        "Failed to change joystick direction: %d",
        ret);

    return ret;
}

return ZMK_INPUT_PROC_STOP;


}

/* ------------------------------------------------------------------------- */
/* Driver API                                                                */
/* ------------------------------------------------------------------------- */

static const struct zmk_input_processor_driver_api
joystick_4way_api = {
.handle_event = joystick_4way_handle_event,
};

/* ------------------------------------------------------------------------- */
/* Initialization                                                            */
/* ------------------------------------------------------------------------- */

static int joystick_4way_init(
const struct device *dev) {


struct joystick_4way_data *data =
    dev->data;


const struct device *analog =
    DEVICE_DT_GET(DT_NODELABEL(analog_joystick));

if (!device_is_ready(analog)) {
    LOG_ERR("4WAY: analog-axis device not ready");
    return -ENODEV;
}

analog_axis_set_raw_data_cb(
    analog,
    joystick_raw_cb);

data->x = JOYSTICK_CENTER_X;
data->y = JOYSTICK_CENTER_Y;

data->have_x = false;
data->have_y = false;
data->x_updated = false;
data->y_updated = false;
data->direction = JOY_NONE;
data->active_binding_index = -1;
data->active_layer = 0;

const struct joystick_4way_config *cfg =
    dev->config;

LOG_INF(
    "4WAY: initialized threshold=%d rotation=%d hysteresis=%d",
    cfg->threshold,
    cfg->rotation_deg,
    cfg->hysteresis_deg);

return 0;


}

static void joystick_raw_cb(
const struct device *dev,
int channel,
int16_t raw_val) {


ARG_UNUSED(dev);

LOG_INF(
    "ANALOG RAW: ch=%d raw=%d",
    channel,
    raw_val);


}

/* ------------------------------------------------------------------------- */
/* Device tree instantiation                                                */
/* ------------------------------------------------------------------------- */

#define JOYSTICK_4WAY_INST(n)                                               \
    static const struct zmk_behavior_binding                              \
        joystick_4way_bindings_##n[] = {                                  \
            LISTIFY(                                                       \
                DT_INST_PROP_LEN(n, bindings),                             \
                ZMK_KEYMAP_EXTRACT_BINDING,                                \
                (, ),                                                       \
                DT_DRV_INST(n))                                             \
        };                                                                  \
                                                                            \
    static const struct joystick_4way_config                               \
        joystick_4way_config_##n = {                                       \
            .threshold = DT_INST_PROP(n, threshold),                      \
            .rotation_deg = DT_INST_PROP(n, rotation_deg),                \
            .hysteresis_deg = DT_INST_PROP(n, hysteresis_deg),            \
            .layer_count = DT_INST_PROP(n, layer_count),              \
            .binding_count =                                               \
                ARRAY_SIZE(joystick_4way_bindings_##n),                   \
            .bindings = joystick_4way_bindings_##n,                       \
        };                                                                  \
                                                                            \
    static struct joystick_4way_data                                       \
        joystick_4way_data_##n;                                            \
                                                                            \
    DEVICE_DT_INST_DEFINE(                                                 \
        n,                                                                 \
        joystick_4way_init,                                                \
        NULL,                                                              \
        &joystick_4way_data_##n,                                           \
        &joystick_4way_config_##n,                                         \
        POST_KERNEL,                                                       \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                              \
        &joystick_4way_api);

DT_INST_FOREACH_STATUS_OKAY(JOYSTICK_4WAY_INST)
