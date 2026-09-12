
/*
 * Typeglide 4-way analog joystick input processor.
 */

#define DT_DRV_COMPAT typeglide_joystick_4way

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/virtual_key_position.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <stdlib.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct joystick_4way_config {
    struct zmk_behavior_binding bindings[4];
    int32_t threshold;
    uint8_t index;
};

struct joystick_4way_data {
    int32_t x;
    int32_t y;
    int8_t direction;
};


/*
 * Direction values:
 */

#define JOY_CEILING   0
#define JOY_FLOOR     1
#define JOY_FORWARD   2
#define JOY_BACK      3
#define JOY_CENTER   -1

static int joystick_4way_get_direction(
    const struct joystick_4way_config *cfg,
    struct joystick_4way_data *data) {

    int32_t x = data->x;
    int32_t y = data->y;

    LOG_DBG("4WAY: calculating direction");
    LOG_DBG("4WAY: raw x=%d y=%d threshold=%d",
            x, y, cfg->threshold);

    /*
     * Apply dead zone.
     */
    if (abs(x) < cfg->threshold) {
        LOG_DBG("4WAY: X inside dead zone: %d -> 0", x);
        x = 0;
    }

    if (abs(y) < cfg->threshold) {
        LOG_DBG("4WAY: Y inside dead zone: %d -> 0", y);
        y = 0;
    }

    LOG_DBG("4WAY: after dead zone x=%d y=%d", x, y);

    /*
     * Joystick is centered.
     */
    if (x == 0 && y == 0) {
        LOG_DBG("4WAY: CENTER");
        return -1;
    }

    /*
     * Use the dominant axis if the joystick is diagonal.
     */
    if (abs(x) >= abs(y)) {

        if (x < 0) {
            LOG_DBG("4WAY: direction FORWARD");
            return JOY_FORWARD;
        } else {
            LOG_DBG("4WAY: direction BACK");
            return JOY_BACK;
        }

    } else {

        if (y > 0) {
            LOG_DBG("4WAY: direction CEILING");
            return JOY_CEILING;
        } else {
            LOG_DBG("4WAY: direction FLOOR");
            return JOY_FLOOR;
        }
    }
}

static int joystick_4way_handle_event(
    const struct device *dev,
    struct input_event *event,
    uint32_t param1,
    uint32_t param2,
    struct zmk_input_processor_state *state) {

    const struct joystick_4way_config *cfg = dev->config;
    struct joystick_4way_data *data = dev->data;

    LOG_DBG("4WAY: ----------------------------------------");
    LOG_DBG("4WAY: EVENT received");
    LOG_DBG("4WAY: type=%d code=%d value=%d",
            event->type,
            event->code,
            event->value);

    LOG_DBG("4WAY: previous state x=%d y=%d direction=%d",
            data->x,
            data->y,
            data->direction);

    LOG_DBG("4WAY: input_device_index=%d",
            state->input_device_index);

    /*
     * Only process relative events.
     */
    if (event->type != INPUT_EV_REL) {
        LOG_DBG("4WAY: ignoring event - not INPUT_EV_REL");
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /*
     * X axis.
     */
    if (event->code == INPUT_REL_X) {

        LOG_DBG("4WAY: INPUT_REL_X received");
        LOG_DBG("4WAY: X value=%d", event->value);

        if (abs(event->value) < cfg->threshold) {
            data->x = 0;
            LOG_DBG("4WAY: X inside dead zone -> 0");
        } else {
            data->x = event->value;
        }

    /*
     * Y axis.
     */
    } else if (event->code == INPUT_REL_Y) {

        LOG_DBG("4WAY: INPUT_REL_Y received");
        LOG_DBG("4WAY: Y value=%d", event->value);

        if (abs(event->value) < cfg->threshold) {
            data->y = 0;
            LOG_DBG("4WAY: Y inside dead zone -> 0");
        } else {
            data->y = event->value;
        }

    /*
     * Something else.
     */
    } else {

        LOG_DBG("4WAY: ignoring unknown REL code=%d",
                event->code);

        return ZMK_INPUT_PROC_CONTINUE;
    }

    LOG_DBG("4WAY: stored x=%d y=%d",
            data->x,
            data->y);

    /*
     * Determine new joystick direction.
     */
    int8_t new_direction =
        joystick_4way_get_direction(cfg, data);

    LOG_DBG("4WAY: old direction=%d new direction=%d",
            data->direction,
            new_direction);

    /*
     * Nothing changed.
     */
    if (new_direction == data->direction) {

        LOG_DBG("4WAY: direction unchanged");
        LOG_DBG("4WAY: stopping event");

        return ZMK_INPUT_PROC_STOP;
    }

    /*
     * Create the ZMK behavior event.
     */
    struct zmk_behavior_binding_event behavior_event = {
        .position =
            ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
                state->input_device_index,
                cfg->index),

        .timestamp = k_uptime_get(),

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    LOG_DBG("4WAY: behavior event created");
    LOG_DBG("4WAY: processor index=%d", cfg->index);
    LOG_DBG("4WAY: position=%d", behavior_event.position);
    LOG_DBG("4WAY: timestamp=%lld",
            (long long)behavior_event.timestamp);

    /*
     * Release previous direction.
     */
    if (data->direction != JOY_CENTER) {

        LOG_INF("4WAY: RELEASE direction=%d",
                data->direction);

        LOG_DBG("4WAY: invoking binding[%d] RELEASE",
                data->direction);

        int ret = zmk_behavior_invoke_binding(
            &cfg->bindings[data->direction],
            behavior_event,
            false);

        LOG_DBG("4WAY: release result=%d", ret);
    }

    /*
     * Press new direction.
     */
    if (new_direction != JOY_CENTER) {

        LOG_INF("4WAY: PRESS direction=%d",
                new_direction);

        LOG_DBG("4WAY: invoking binding[%d] PRESS",
                new_direction);

        int ret = zmk_behavior_invoke_binding(
            &cfg->bindings[new_direction],
            behavior_event,
            true);

        LOG_DBG("4WAY: press result=%d", ret);
    } else {

        LOG_DBG("4WAY: new direction is CENTER/NONE");
    }

    /*
     * Save new state.
     */
    data->direction = new_direction;

    LOG_DBG("4WAY: new state x=%d y=%d direction=%d",
            data->x,
            data->y,
            data->direction);

    LOG_DBG("4WAY: event processing complete");
    LOG_DBG("4WAY: ----------------------------------------");

    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api joystick_4way_driver_api = {
    .handle_event = joystick_4way_handle_event,
};


static int joystick_4way_init(const struct device *dev) {

    struct joystick_4way_data *data = dev->data;
    const struct joystick_4way_config *cfg = dev->config;

    data->x = 0;
    data->y = 0;
    data->direction = JOY_CENTER;

    LOG_INF("4WAY: ========================================");
    LOG_INF("4WAY: Typeglide joystick 4-way processor");
    LOG_INF("4WAY: INITIALIZING");
    LOG_INF("4WAY: device=%p", dev);
    LOG_INF("4WAY: index=%d", cfg->index);
    LOG_INF("4WAY: threshold=%d", cfg->threshold);

    LOG_INF("4WAY: binding[%d] = CEILING", JOY_CEILING);
    LOG_INF("4WAY: binding[%d] = FLOOR", JOY_FLOOR);
    LOG_INF("4WAY: binding[%d] = FORWARD", JOY_FORWARD);
    LOG_INF("4WAY: binding[%d] = BACK", JOY_BACK);

    LOG_INF("4WAY: initial x=%d", data->x);
    LOG_INF("4WAY: initial y=%d", data->y);
    LOG_INF("4WAY: initial direction=%d", data->direction);

    LOG_INF("4WAY: INITIALIZATION COMPLETE");
    LOG_INF("4WAY: ========================================");

    return 0;
}


#define JOYSTICK_4WAY_INST(n)                                                   \
    static const struct joystick_4way_config config_##n = {                    \
        .bindings = {                                                          \
            LISTIFY(DT_INST_PROP_LEN(n, bindings),                             \
                    ZMK_KEYMAP_EXTRACT_BINDING, (,),                          \
                    DT_DRV_INST(n))                                            \
        },                                                                      \
        .threshold = DT_INST_PROP(n, threshold),                              \
        .index = n,                                                            \
    };                                                                          \
    static struct joystick_4way_data data_##n;                                 \
    DEVICE_DT_INST_DEFINE(                                                     \
        n, joystick_4way_init, NULL,                                             \
        &data_##n, &config_##n,                                                  \
        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       \
        &joystick_4way_driver_api);


DT_INST_FOREACH_STATUS_OKAY(JOYSTICK_4WAY_INST)
