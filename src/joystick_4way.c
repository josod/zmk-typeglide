
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

LOG_MODULE_REGISTER(typeglide_joystick_4way, CONFIG_ZMK_LOG_LEVEL);

enum joystick_direction {
    JOY_NONE = 0,
    JOY_RIGHT,
    JOY_LEFT,
    JOY_DOWN,
    JOY_UP,
};

struct joystick_4way_config {
    int32_t threshold;

    size_t binding_count;
    const struct zmk_behavior_binding *bindings;
};

struct joystick_4way_data {
    int32_t x;
    int32_t y;
    bool have_x;
    bool have_y;
    enum joystick_direction direction;
};

static enum joystick_direction joystick_get_direction(
    const struct joystick_4way_config *cfg,
    int32_t x,
    int32_t y) {

    const int32_t center_x = 59;
    const int32_t center_y = 127;

    int32_t dx = x - center_x;
    int32_t dy = y - center_y;

    int32_t abs_x = (dx < 0) ? -dx : dx;
    int32_t abs_y = (dy < 0) ? -dy : dy;

    LOG_DBG("4WAY: x=%d y=%d dx=%d dy=%d threshold=%d",
            x, y, dx, dy, cfg->threshold);

    if (abs_x < cfg->threshold && abs_y < cfg->threshold) {
        return JOY_NONE;
    }

    if (abs_x >= abs_y) {
        return (dx >= 0) ? JOY_RIGHT : JOY_LEFT;
    }

    return (dy >= 0) ? JOY_DOWN : JOY_UP;
}

static int joystick_invoke(
    const struct device *dev,
    const struct joystick_4way_config *cfg,
    struct zmk_input_processor_state *state,
    enum joystick_direction direction,
    bool pressed) {

    if (direction == JOY_NONE) {
        return 0;
    }

    /*
     * bindings[] order:
     *
     * 0 = RIGHT
     * 1 = LEFT
     * 2 = DOWN
     * 3 = UP
     */
    size_t index = direction - 1;

    if (index >= cfg->binding_count) {
        LOG_ERR("Invalid joystick direction %d", direction);
        return -EINVAL;
    }

    struct zmk_behavior_binding_event behavior_event = {
        .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
            state->input_device_index,
            0),
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    LOG_DBG(
        "4WAY: %s direction=%d position=%d",
        pressed ? "PRESS" : "RELEASE",
        direction,
        behavior_event.position);

    return zmk_behavior_invoke_binding(
        &cfg->bindings[index],
        behavior_event,
        pressed);
}

static int joystick_4way_change_direction(
    const struct device *dev,
    const struct joystick_4way_config *cfg,
    struct joystick_4way_data *data,
    struct zmk_input_processor_state *state,
    enum joystick_direction new_direction) {

    if (new_direction == data->direction) {
        return 0;
    }

    /*
     * Release the previous direction first.
     */
    if (data->direction != JOY_NONE) {
        int ret = joystick_invoke(
            dev,
            cfg,
            state,
            data->direction,
            false);

        if (ret < 0) {
            return ret;
        }
    }

    data->direction = new_direction;

    /*
     * Press the new direction.
     */
    if (new_direction != JOY_NONE) {
        int ret = joystick_invoke(
            dev,
            cfg,
            state,
            new_direction,
            true);

        if (ret < 0) {
            data->direction = JOY_NONE;
            return ret;
        }
    }

    return 0;
}

static int joystick_4way_handle_event(
    const struct device *dev,
    struct input_event *event,
    uint32_t param1,
    uint32_t param2,
    struct zmk_input_processor_state *state) {

    const struct joystick_4way_config *cfg = dev->config;
    struct joystick_4way_data *data = dev->data;

    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    /*
     * We only consume X/Y absolute joystick events.
     */
    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->code) {
    case INPUT_ABS_X:
        data->x = event->value;
        data->have_x = true;
        break;

    case INPUT_ABS_Y:
        data->y = event->value;
        data->have_y = true;
        break;

    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (!data->have_x || !data->have_y) {
        return ZMK_INPUT_PROC_STOP;
    }

    enum joystick_direction direction =
        joystick_get_direction(cfg, data->x, data->y);

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
        LOG_ERR("Failed to change joystick direction: %d", ret);
        return ret;
    }

    /*
     * We consumed this ABS event.
     */
    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api joystick_4way_api = {
    .handle_event = joystick_4way_handle_event,
};

static int joystick_4way_init(const struct device *dev) {
    struct joystick_4way_data *data = dev->data;

    data->x = 0;
    data->y = 0;
    data->have_x = false;
    data->have_y = false;
    data->direction = JOY_NONE;

    return 0;
}

#define JOYSTICK_4WAY_INST(n)                                                \
    static const struct zmk_behavior_binding                          \
        joystick_4way_bindings_##n[] = {                                      \
            LISTIFY(DT_INST_PROP_LEN(n, bindings),                           \
                    ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))};      \
                                                                              \
    static const struct joystick_4way_config                              \
        joystick_4way_config_##n = {                                          \
            .threshold = DT_INST_PROP(n, threshold),                         \
            .binding_count = ARRAY_SIZE(joystick_4way_bindings_##n),         \
            .bindings = joystick_4way_bindings_##n,                          \
        };                                                                    \
                                                                              \
    static struct joystick_4way_data joystick_4way_data_##n;                  \
                                                                              \
    DEVICE_DT_INST_DEFINE(                                                    \
        n,                                                                    \
        joystick_4way_init,                                                   \
        NULL,                                                                 \
        &joystick_4way_data_##n,                                              \
        &joystick_4way_config_##n,                                             \
        POST_KERNEL,                                                          \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                 \
        &joystick_4way_api);

DT_INST_FOREACH_STATUS_OKAY(JOYSTICK_4WAY_INST)
