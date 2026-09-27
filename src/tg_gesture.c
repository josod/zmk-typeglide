#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zephyr/devicetree.h>
#include <zmk/behavior.h>
#include <typeglide/events/joystick_state_changed.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

static const struct zmk_behavior_binding caps_word_binding = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(caps_word)),
    .param1 = 0,
    .param2 = 0,
};
static enum joystick_direction tg_joy = JOY_NONE;
static void tg_resolve(void);
static bool caps_word_triggered;
static bool tg_pending_scroll;
static struct k_work_delayable tg_scroll_work;
static bool tg_lshift;
static bool tg_rshift;
static bool tg_scrolling;

#include <dt-bindings/zmk/pointing.h>

static const struct zmk_behavior_binding scroll_right_binding = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(msc)),
    .param1 = SCRL_RIGHT,
    .param2 = 0,
};

#define TG_SCROLL_TIMEOUT_MS 5000

LOG_MODULE_REGISTER(tg_gesture, LOG_LEVEL_DBG);


static void tg_scroll_timeout(struct k_work *work)
{
    if (!tg_pending_scroll) {
        return;
    }

    tg_pending_scroll = false;
    tg_scrolling = true;

    LOG_DBG("TG: joystick timeout -> scroll start");

    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = 0,
#endif
    };

    zmk_behavior_invoke_binding(
        &scroll_right_binding,
        event,
        true
    );
}

static void tg_init(void)
{
    k_work_init_delayable(&tg_scroll_work, tg_scroll_timeout);
}

SYS_INIT(tg_init, APPLICATION, 0);

static int tg_gesture_keycode_listener(const zmk_event_t *eh)
{
    const struct zmk_keycode_state_changed *ev =
        as_zmk_keycode_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->usage_page == 0x07) {
           if (ev->keycode == 225) {
               tg_lshift = ev->state;
           } else if (ev->keycode == 229) {
               tg_rshift = ev->state;
           }
    }

    bool tg_shift = tg_lshift || tg_rshift;

    LOG_DBG(
        "TG SHIFT: event key=%u state=%d own=L%d R%d -> %d | HID=L%d R%d",
        ev->keycode,
        ev->state,
        tg_lshift,
        tg_rshift,
        tg_shift,
        zmk_hid_mod_is_pressed(MOD_LSFT),
        zmk_hid_mod_is_pressed(MOD_RSFT)
    );


    bool shift = tg_lshift || tg_rshift;
    LOG_DBG("SHIFT=%d", shift);

    tg_resolve();

    LOG_DBG(
        "BEFORE: page=%u key=%u state=%d implicit=%02x explicit=%02x "
        "LSFT=%d RSFT=%d",
        ev->usage_page,
        ev->keycode,
        ev->state,
        ev->implicit_modifiers,
        ev->explicit_modifiers,
        zmk_hid_mod_is_pressed(MOD_LSFT),
        zmk_hid_mod_is_pressed(MOD_RSFT)
    );


    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tg_gesture, tg_gesture_keycode_listener);
ZMK_SUBSCRIPTION(tg_gesture, zmk_keycode_state_changed);

static enum joystick_direction tg_prev_joy = JOY_NONE;

static void tg_resolve(void)
{
    bool shift = tg_lshift || tg_rshift;

        LOG_DBG("RESOLVE: joy=%d prev=%d shift=%d pending=%d scrolling=%d",
                tg_joy, tg_prev_joy, shift,
                tg_pending_scroll, tg_scrolling);

        /*
         * FORWARD was released while scrolling.
         */
        if (tg_scrolling && tg_joy != JOY_FORWARD) {
            tg_scrolling = false;

            LOG_DBG("TG: joystick released -> scroll stop");

            struct zmk_behavior_binding_event event = {
                .layer = 0,
                .position = 0,
                .timestamp = k_uptime_get(),
    #if IS_ENABLED(CONFIG_ZMK_SPLIT)
                .source = 0,
    #endif
            };

            zmk_behavior_invoke_binding(
                &scroll_right_binding,
                event,
                false
            );
        }

        bool gesture = (tg_joy == JOY_FORWARD && shift);

    /*
     * FORWARD + SHIFT:
     *
     * If FORWARD was previously pending as a possible scroll,
     * this is now confirmed as the CAPS WORD gesture.
     */
    if (gesture) {

        if (tg_pending_scroll) {
            k_work_cancel_delayable(&tg_scroll_work);
            tg_pending_scroll = false;

            LOG_DBG("TG: pending scroll cancelled -> CAPS WORD");
        }

        if (!caps_word_triggered) {
            caps_word_triggered = true;

            LOG_DBG("TG: invoking CAPS WORD");

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

        tg_prev_joy = tg_joy;
        return;
    }

    /*
     * No longer in the CAPS WORD gesture.
     */
    if (tg_joy != JOY_FORWARD) {
        caps_word_triggered = false;
    }

    /*
     * New FORWARD without SHIFT:
     * don't scroll yet — start the disambiguation timer.
     */
    if (tg_joy == JOY_FORWARD &&
        tg_prev_joy != JOY_FORWARD &&
        !shift) {

        tg_pending_scroll = true;

        LOG_DBG("TG: joystick pending -> %d ms", TG_SCROLL_TIMEOUT_MS);

        k_work_reschedule(
            &tg_scroll_work,
            K_MSEC(TG_SCROLL_TIMEOUT_MS)
        );
    }

    tg_prev_joy = tg_joy;
}

static int tg_gesture_joystick_listener(const zmk_event_t *eh)
{
    const struct typeglide_joystick_state_changed *ev =
        as_typeglide_joystick_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    tg_joy = ev->direction;

    LOG_DBG("TG STATE: joy=%d", tg_joy);
    LOG_DBG("TG MODS: LSHIFT=%d RSHIFT=%d",
            tg_lshift, tg_rshift);
    tg_resolve();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(tg_gesture_joystick, tg_gesture_joystick_listener);
ZMK_SUBSCRIPTION(tg_gesture_joystick, typeglide_joystick_state_changed);
