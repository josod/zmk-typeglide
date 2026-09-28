/*
 * Typeglide Caps Word / Scroll behavior
 *
 * Press:
 *
 *   Shift already held  -> CAPS WORD
 *   no Shift             -> wait
 *
 * During wait:
 *
 *   Shift pressed        -> CAPS WORD
 *   timeout              -> SCROLL
 *
 * Release:
 *
 *   still waiting        -> cancel
 *   selected binding     -> release selected binding
 */

#define DT_DRV_COMPAT typeglide_caps_word_or_scroll

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_caps_word_or_scroll_config {
    struct zmk_behavior_binding scroll_binding;
    struct zmk_behavior_binding caps_word_binding;

    zmk_mod_flags_t mods;
    uint32_t timeout_ms;
};

struct behavior_caps_word_or_scroll_data {
    struct k_work_delayable work;
    struct zmk_behavior_binding_event event;
    struct zmk_behavior_binding *pressed_binding;
    const struct device *dev;
    bool pending;
};

/*
 * --------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------
 */

static bool shift_is_active(
    const struct behavior_caps_word_or_scroll_config *config)
{
    return (zmk_hid_get_explicit_mods() & config->mods) != 0;
}


/*
 * --------------------------------------------------------------------------
 * Timeout
 * --------------------------------------------------------------------------
 */

static void caps_word_or_scroll_timeout(struct k_work *work)
{
    struct k_work_delayable *dwork =
        k_work_delayable_from_work(work);

    struct behavior_caps_word_or_scroll_data *data =
        CONTAINER_OF(
            dwork,
            struct behavior_caps_word_or_scroll_data,
            work);


    const struct behavior_caps_word_or_scroll_config *config =
        data->dev->config;

    if (!data->pending) {
        return;
    }

    /*
     * We are resolving the delayed press now.
     */
    data->pending = false;

    /*
     * Shift might have arrived immediately before the timeout.
     * Check the actual ZMK modifier state rather than assuming
     * the Shift listener got here first.
     */
    if (shift_is_active(config)) {
        data->pressed_binding =
            (struct zmk_behavior_binding *)&config->caps_word_binding;

        LOG_DBG("timeout: Shift active -> Caps Word");
    } else {
        data->pressed_binding =
            (struct zmk_behavior_binding *)&config->scroll_binding;

        LOG_DBG("timeout -> scroll");
    }

    zmk_behavior_invoke_binding(
        data->pressed_binding,
        data->event,
        true);
}


/*
 * --------------------------------------------------------------------------
 * Shift listener
 * --------------------------------------------------------------------------
 *
 * There can be more than one instance of this behavior in the device tree,
 * so maintain a device list exactly like behavior_caps_word does.
 */

static int caps_word_or_scroll_keycode_state_changed(
    const zmk_event_t *eh);

ZMK_LISTENER(
    behavior_caps_word_or_scroll,
    caps_word_or_scroll_keycode_state_changed);

ZMK_SUBSCRIPTION(
    behavior_caps_word_or_scroll,
    zmk_keycode_state_changed);


#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),

static const struct device *devs[] = {
    DT_INST_FOREACH_STATUS_OKAY(GET_DEV)
};


static int caps_word_or_scroll_keycode_state_changed(
    const zmk_event_t *eh)
{
    struct zmk_keycode_state_changed *ev =
        as_zmk_keycode_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * Only Shift can resolve the pending ambiguity.
     */
    if (ev->usage_page != HID_USAGE_KEY) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * Check each instance. Normally there will only be one, but this
     * keeps the behavior properly instance-based.
     */
    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];

        struct behavior_caps_word_or_scroll_data *data =
            dev->data;

        const struct behavior_caps_word_or_scroll_config *config =
            data->dev->config;

        if (!data->pending) {
            continue;
        }

        if ((zmk_hid_get_explicit_mods() & config->mods) == 0) {
            continue;
        }

        /*
         * Shift has resolved the pending gesture.
         */
        data->pending = false;

        k_work_cancel_delayable(&data->work);

        data->pressed_binding =
            (struct zmk_behavior_binding *)&config->caps_word_binding;

        LOG_DBG("Shift arrived -> Caps Word");

        zmk_behavior_invoke_binding(
            data->pressed_binding,
            data->event,
            true);
    }

    return ZMK_EV_EVENT_BUBBLE;
}


/*
 * --------------------------------------------------------------------------
 * Behavior press
 * --------------------------------------------------------------------------
 */

static int on_caps_word_or_scroll_binding_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event)
{
    const struct device *dev =
        zmk_behavior_get_binding(binding->behavior_dev);

    const struct behavior_caps_word_or_scroll_config *config =
        dev->config;

    struct behavior_caps_word_or_scroll_data *data =
        dev->data;

    if (data->pending || data->pressed_binding != NULL) {
        LOG_ERR("Caps-word-or-scroll already active");

        return -EBUSY;
    }

    /*
     * Save the event because the actual binding may be invoked later
     * by the timer or Shift listener.
     */
    data->event = event;

    /*
     * Shift is already held.
     */
    if (shift_is_active(config)) {
        data->pressed_binding =
            (struct zmk_behavior_binding *)&config->caps_word_binding;

        LOG_DBG("press with Shift -> Caps Word");

        return zmk_behavior_invoke_binding(
            data->pressed_binding,
            event,
            true);
    }

    /*
     * No Shift yet. Wait for either:
     *
     *     Shift -> Caps Word
     *     timeout -> Scroll
     */
    data->pending = true;

    LOG_DBG(
        "press -> pending for %u ms",
        config->timeout_ms);

    k_work_reschedule(
        &data->work,
        K_MSEC(config->timeout_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}


/*
 * --------------------------------------------------------------------------
 * Behavior release
 * --------------------------------------------------------------------------
 */

static int on_caps_word_or_scroll_binding_released(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event)
{
    const struct device *dev =
        zmk_behavior_get_binding(binding->behavior_dev);

    struct behavior_caps_word_or_scroll_data *data =
        dev->data;

    /*
     * Released before the timeout.
     *
     * There is no decision to make, so simply cancel the pending gesture.
     */
    if (data->pending) {
        data->pending = false;

        k_work_cancel_delayable(&data->work);

        LOG_DBG("release while pending -> cancel");

        return ZMK_BEHAVIOR_OPAQUE;
    }

    /*
     * Nothing was actually pressed.
     */
    if (data->pressed_binding == NULL) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    /*
     * Release exactly the binding that was selected on press.
     */
    struct zmk_behavior_binding *pressed_binding =
        data->pressed_binding;

    data->pressed_binding = NULL;

    LOG_DBG("release selected binding");

    return zmk_behavior_invoke_binding(
        pressed_binding,
        event,
        false);
}


/*
 * --------------------------------------------------------------------------
 * Driver API
 * --------------------------------------------------------------------------
 */

static const struct behavior_driver_api
behavior_caps_word_or_scroll_driver_api = {
    .binding_pressed =
        on_caps_word_or_scroll_binding_pressed,

    .binding_released =
        on_caps_word_or_scroll_binding_released,

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata =
        zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_caps_word_or_scroll_init(const struct device *dev)
{
    struct behavior_caps_word_or_scroll_data *data = dev->data;

    k_work_init_delayable(&data->work, caps_word_or_scroll_timeout);

    return 0;
}

/*
 * --------------------------------------------------------------------------
 * Device tree binding extraction
 * --------------------------------------------------------------------------
 */

#define TRANSFORM_ENTRY(idx, node)                                             \
    {                                                                          \
        .behavior_dev = DEVICE_DT_NAME(                                       \
            DT_INST_PHANDLE_BY_IDX(node, bindings, idx)),                     \
        .param1 = COND_CODE_0(                                                 \
            DT_INST_PHA_HAS_CELL_AT_IDX(                                      \
                node, bindings, idx, param1),                                 \
            (0),                                                               \
            (DT_INST_PHA_BY_IDX(node, bindings, idx, param1))),               \
        .param2 = COND_CODE_0(                                                 \
            DT_INST_PHA_HAS_CELL_AT_IDX(                                      \
                node, bindings, idx, param2),                                 \
            (0),                                                               \
            (DT_INST_PHA_BY_IDX(node, bindings, idx, param2))),               \
    }


#define KP_INST(n)                                                            \
static struct behavior_caps_word_or_scroll_data\
    behavior_caps_word_or_scroll_data_##n = {\
        .pending = false,\
        .pressed_binding = NULL,\
        .dev = DEVICE_DT_INST_GET(n),\
    };                                                                    \
                                                                               \
    static const struct behavior_caps_word_or_scroll_config                    \
        behavior_caps_word_or_scroll_config_##n = {                           \
            .scroll_binding = TRANSFORM_ENTRY(0, n),                          \
            .caps_word_binding = TRANSFORM_ENTRY(1, n),                       \
            .mods = DT_INST_PROP(n, mods),                                    \
            .timeout_ms = DT_INST_PROP_OR(n, timeout_ms, 300),                \
        };                                                                     \
                                                                               \
    BEHAVIOR_DT_INST_DEFINE(                                                  \
        n,                                                                     \
         behavior_caps_word_or_scroll_init,                                                                    \
        NULL,                                                                  \
        &behavior_caps_word_or_scroll_data_##n,                               \
        &behavior_caps_word_or_scroll_config_##n,                             \
        POST_KERNEL,                                                           \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
        &behavior_caps_word_or_scroll_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif
