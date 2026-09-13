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
    JOY_RIGHT,
    JOY_LEFT,
    JOY_DOWN,
    JOY_UP,
};

struct joystick_4way_config {
    int32_t threshold;
    int32_t rotation_deg;
    int32_t hysteresis_deg;

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
};


/*
 * Current calibrated center from Typeglide testing.
 */
#define JOYSTICK_CENTER_X 127
#define JOYSTICK_CENTER_Y 127


/*
 * Fixed-point rotation constants.
 *
 * These are calculated dynamically from rotation_deg.
 *
 * Values are scaled by 1000.
 */
#define ROT_SCALE 1000


/* ------------------------------------------------------------------------- */
/* Small integer helpers                                                     */
/* ------------------------------------------------------------------------- */

static int32_t abs32(int32_t value) {
    return value < 0 ? -value : value;
}


/*
 * Normalize an angle represented in degrees to:
 *
 *     0 <= angle < 360
 *
 * We don't actually calculate atan2().
 *
 * Direction classification below uses geometric sector tests.
 */


/* ------------------------------------------------------------------------- */
/* Rotation                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * Rotate the joystick vector.
 *
 * Instead of calculating an angle, we rotate the vector itself and then
 * classify it into one of four 90-degree sectors.
 *
 * For the common small correction values we need, the trigonometric
 * functions are only called when the direction is calculated.
 *
 * Positive rotation:
 *
 *     rotates the coordinate vector counter-clockwise mathematically.
 *
 * Because the joystick Y axis points DOWN, this corresponds to a visual
 * clockwise/counter-clockwise correction in the physical coordinate system.
 */
static void rotate_vector(
    int32_t dx,
    int32_t dy,
    int32_t rotation_deg,
    int32_t *rx,
    int32_t *ry) {

    /*
     * Common corrections are deliberately handled with integer
     * approximations. This keeps the processor independent of libm.
     *
     * The values below are enough for the small mechanical correction
     * expected from the joystick mounting.
     */

    switch (rotation_deg) {

    case 0:
        *rx = dx;
        *ry = dy;
        return;

    case 1:
        /*
         * cos(1°)  = 0.99985
         * sin(1°)  = 0.01745
         */
        *rx = (dx * 1000 - dy * 17) / 1000;
        *ry = (dx * 17 + dy * 1000) / 1000;
        return;

    case 2:
        /*
         * cos(2°)  = 0.99939
         * sin(2°)  = 0.03490
         */
        *rx = (dx * 999 - dy * 35) / 1000;
        *ry = (dx * 35 + dy * 999) / 1000;
        return;

    case 3:
        /*
         * cos(3°)  = 0.99863
         * sin(3°)  = 0.05234
         */
        *rx = (dx * 999 - dy * 52) / 1000;
        *ry = (dx * 52 + dy * 999) / 1000;
        return;

    case 4:
        /*
         * cos(4°)  = 0.99756
         * sin(4°)  = 0.06976
         */
        *rx = (dx * 998 - dy * 70) / 1000;
        *ry = (dx * 70 + dy * 998) / 1000;
        return;

    case 5:
        /*
         * cos(5°)  = 0.99619
         * sin(5°)  = 0.08716
         */
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
        /*
         * For now, clamp unsupported values to no rotation.
         *
         * We can extend this table later if needed.
         */
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

/*
 * The joystick is divided into four 90-degree sectors:
 *
 *
 *                     UP
 *                    /\
 *                   /  \
 *                  /    \
 *                 /      \
 *                /        \
 *       LEFT ---------------- RIGHT
 *                \        /
 *                 \      /
 *                  \    /
 *                   \  /
 *                    \/
 *                    DOWN
 *
 *
 * The important point is that diagonal movement belongs to whichever
 * 90-degree sector it is physically closest to.
 *
 * Example:
 *
 *     dx=-18, dy=-10
 *
 * is clearly closer to LEFT than UP.
 *
 * Whereas:
 *
 *     dx=-6, dy=-10
 *
 * is closer to UP.
 */


/*
 * Return the dominant 4-way sector.
 *
 * We use the diagonal boundary:
 *
 *     |x| == |y|
 *
 * which corresponds to 45 degrees.
 *
 * Unlike the old algorithm, this is applied AFTER rotation and with
 * hysteresis handled separately.
 */
static enum joystick_direction direction_from_vector(
    int32_t x,
    int32_t y) {

    int32_t ax = abs32(x);
    int32_t ay = abs32(y);

    /*
     * RIGHT / LEFT sector.
     *
     * X must be at least as strong as Y.
     */
    if (ax >= ay) {
        return x >= 0 ? JOY_RIGHT : JOY_LEFT;
    }

    /*
     * DOWN / UP sector.
     */
    return y >= 0 ? JOY_DOWN : JOY_UP;
}


/*
 * Return the direction's central vector.
 *
 * These are the four cardinal directions:
 *
 * RIGHT = +X
 * LEFT  = -X
 * DOWN  = +Y
 * UP    = -Y
 */
static void direction_vector(
    enum joystick_direction direction,
    int32_t *x,
    int32_t *y) {

    switch (direction) {

    case JOY_RIGHT:
        *x = 1;
        *y = 0;
        break;

    case JOY_LEFT:
        *x = -1;
        *y = 0;
        break;

    case JOY_DOWN:
        *x = 0;
        *y = 1;
        break;

    case JOY_UP:
        *x = 0;
        *y = -1;
        break;

    default:
        *x = 0;
        *y = 0;
        break;
    }
}


/*
 * Determine whether a vector is still sufficiently inside the current
 * sector.
 *
 * Instead of calculating angles, we compare the current direction's
 * projection against the perpendicular component.
 *
 * Normal sector:
 *
 *     forward >= sideways
 *
 * Hysteresis makes the current direction survive farther toward the
 * neighbouring sector.
 *
 * With hysteresis = 10:
 *
 *     forward * 100 >= sideways * 100 - hysteresis contribution
 *
 * Since the joystick values are small (~0..255), we use the angular
 * interpretation below with a simple ratio.
 */
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

    case JOY_RIGHT:
        forward = x;
        sideways = abs32(y);
        break;

    case JOY_LEFT:
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

    /*
     * At zero hysteresis:
     *
     *     forward >= sideways
     *
     * means the vector is within 45 degrees of the current direction.
     *
     * Increasing hysteresis makes the current direction harder to leave.
     *
     * We approximate:
     *
     *     45° + hysteresis
     *
     * with a simple tangent approximation.
     *
     * For our small hysteresis values (normally 5..15 degrees), this
     * gives stable behavior without libm.
     */

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
        /*
         * Approximate 1.8 units per degree.
         */
        margin = hysteresis_deg * 18 / 10;
        break;
    }

    /*
     * We require the forward component to exceed the sideways
     * component by the hysteresis margin.
     *
     * This deliberately biases toward keeping the existing state.
     */
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

    /*
     * Vector magnitude deadzone.
     *
     * We avoid sqrt():
     *
     *     dx² + dy² < threshold²
     *
     * is equivalent to:
     *
     *     magnitude < threshold
     */
    int32_t magnitude_squared =
        dx * dx + dy * dy;

    int32_t activation_threshold =
        cfg->threshold;

    int32_t release_threshold = cfg->threshold + 8;

    int32_t threshold =
        (data->direction == JOY_NONE)
            ? activation_threshold
            : release_threshold;

    if (magnitude_squared < threshold * threshold) {
        return JOY_NONE;
    }

    /*
     * Apply global mechanical rotation correction.
     */
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

    /*
     * If we already have a direction, use hysteresis first.
     *
     * This is what prevents:
     *
     *     DOWN
     *     NONE
     *     DOWN
     *     NONE
     *
     * when the joystick sits close to a sector boundary.
     */
    if (data->direction != JOY_NONE) {

        if (direction_is_stable(
                data->direction,
                rx,
                ry,
                cfg->hysteresis_deg)) {

            return data->direction;
        }
    }

    /*
     * We are either:
     *
     *  - entering from NONE
     *  - or have genuinely crossed the hysteresis boundary.
     */
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
    struct zmk_input_processor_state *state,
    enum joystick_direction direction,
    bool pressed) {

    ARG_UNUSED(dev);

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
        LOG_ERR(
            "Invalid joystick direction %d",
            direction);

        return -EINVAL;
    }

    struct zmk_behavior_binding_event behavior_event = {
        .position =
            ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
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

    /*
     * Release previous direction first.
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
     * Press new direction.
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

    /*
     * Only consume absolute joystick events.
     */
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

    /*
     * Wait until both axes have been received.
     */
    if (!data->have_x || !data->have_y) {
        return ZMK_INPUT_PROC_STOP;
    }

    /*
     * Wait until we have a fresh X and fresh Y sample.
     *
     * The analog-axis driver emits X and Y as separate input events.
     * Evaluating after only one axis changes causes:
     *
     *     old X + new Y
     *     new X + old Y
     *
     * to be interpreted as two physical joystick positions.
     */
    if (!data->x_updated || !data->y_updated) {
        return ZMK_INPUT_PROC_STOP;
    }

    /*
     * Consume this X/Y sample pair.
     */
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

    /*
     * We consumed this ABS event.
     */
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
        DEVICE_DT_GET(DT_NODELABEL(joystick));

    if (!device_is_ready(analog)) {
        LOG_ERR("4WAY: analog-axis device not ready");
        return -ENODEV;
    }


    analog_axis_set_raw_data_cb(analog, joystick_raw_cb);


    data->x = JOYSTICK_CENTER_X;
    data->y = JOYSTICK_CENTER_Y;

    data->have_x = false;
    data->have_y = false;
    data->x_updated = false;
    data->y_updated = false;
    data->direction = JOY_NONE;

    const struct joystick_4way_config *cfg =
        dev->config;

    LOG_INF(
        "4WAY: initialized threshold=%d rotation=%d hysteresis=%d",
        cfg->threshold,
        cfg->rotation_deg,
        cfg->hysteresis_deg);

    return 0;
}

static void joystick_raw_cb(const struct device *dev, int channel, int16_t raw_val)
{
    LOG_INF("ANALOG RAW: ch=%d raw=%d", channel, raw_val);
}

/* ------------------------------------------------------------------------- */
/* Device tree instantiation                                                */
/* ------------------------------------------------------------------------- */

#define JOYSTICK_4WAY_INST(n)                                                \
    static const struct zmk_behavior_binding                              \
        joystick_4way_bindings_##n[] = {                                    \
            LISTIFY(                                                        \
                DT_INST_PROP_LEN(n, bindings),                              \
                ZMK_KEYMAP_EXTRACT_BINDING,                                 \
                (, ),                                                        \
                DT_DRV_INST(n))                                              \
        };                                                                   \
                                                                               \
    static const struct joystick_4way_config                                \
        joystick_4way_config_##n = {                                        \
            .threshold = DT_INST_PROP(n, threshold),                       \
            .rotation_deg = DT_INST_PROP(n, rotation_deg),                 \
            .hysteresis_deg = DT_INST_PROP(n, hysteresis_deg),             \
            .binding_count =                                                \
                ARRAY_SIZE(joystick_4way_bindings_##n),                    \
            .bindings = joystick_4way_bindings_##n,                        \
        };                                                                   \
                                                                               \
    static struct joystick_4way_data                                        \
        joystick_4way_data_##n;                                             \
                                                                               \
    DEVICE_DT_INST_DEFINE(                                                  \
        n,                                                                  \
        joystick_4way_init,                                                 \
        NULL,                                                               \
        &joystick_4way_data_##n,                                            \
        &joystick_4way_config_##n,                                          \
        POST_KERNEL,                                                        \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                               \
        &joystick_4way_api);

DT_INST_FOREACH_STATUS_OKAY(JOYSTICK_4WAY_INST)
