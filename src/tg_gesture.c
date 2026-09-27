#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zephyr/devicetree.h>
#include <zmk/behavior.h>
#include <typeglide/events/joystick_state_changed.h>


static const struct zmk_behavior_binding caps_word_binding = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(caps_word)),
    .param1 = 0,
    .param2 = 0,
};
static enum joystick_direction tg_joy = JOY_NONE;


LOG_MODULE_REGISTER(tg_gesture, LOG_LEVEL_DBG);

static int tg_gesture_keycode_listener(const zmk_event_t *eh)
{
    const struct zmk_keycode_state_changed *ev =
        as_zmk_keycode_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool shift = zmk_hid_mod_is_pressed(MOD_LSFT) ||
                 zmk_hid_mod_is_pressed(MOD_RSFT);

    LOG_DBG("SHIFT=%d", shift);

    LOG_DBG("page %u keycode %u %s implicit=%02x explicit=%02x is_mod=%d",
            ev->usage_page,
            ev->keycode,
            ev->state ? "DOWN" : "UP",
            ev->implicit_modifiers,
            ev->explicit_modifiers,
            is_mod(ev->usage_page, ev->keycode));

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tg_gesture, tg_gesture_keycode_listener);
ZMK_SUBSCRIPTION(tg_gesture, zmk_keycode_state_changed);



static int tg_gesture_joystick_listener(const zmk_event_t *eh)
{
    const struct typeglide_joystick_state_changed *ev =
        as_typeglide_joystick_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    tg_joy = ev->direction;

    LOG_DBG("TG STATE: joy=%d", tg_joy);

    if (tg_joy == JOY_FORWARD) {
        bool shift =
            zmk_hid_mod_is_pressed(MOD_LSFT) ||
            zmk_hid_mod_is_pressed(MOD_RSFT);

        if (shift) {
            struct zmk_behavior_binding_event event = {
                .layer = 0,
                .position = 0,
                .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                .source = 0,
#endif
            };

            zmk_behavior_invoke_binding(
                &caps_word_binding,
                event,
                true
            );
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tg_gesture_joystick, tg_gesture_joystick_listener);
ZMK_SUBSCRIPTION(tg_gesture_joystick, typeglide_joystick_state_changed);
