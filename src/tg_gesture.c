#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/devicetree.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>

#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/pointing.h>

#include <typeglide/events/joystick_state_changed.h>
#include <typeglide/events/gesture_timeout.h>

LOG_MODULE_REGISTER(tg_gesture, LOG_LEVEL_DBG);


/*
 * --------------------------------------------------------------------------
 * Timeout event
 * --------------------------------------------------------------------------
 *
 * The delayed work runs on the Zephyr workqueue.
 *
 * It does NOT modify gesture state and does NOT invoke a ZMK behavior.
 * It only raises this event.
 *
 * The gesture listener then handles the timeout in the normal ZMK event
 * processing context.
 */


/*
 * --------------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------------
 */

#define TG_SCROLL_TIMEOUT_MS 300


/*
 * --------------------------------------------------------------------------
 * ZMK behavior bindings
 * --------------------------------------------------------------------------
 */

static const struct zmk_behavior_binding caps_word_binding = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(caps_word)),
    .param1 = 0,
    .param2 = 0,
};

static const struct zmk_behavior_binding scroll_right_binding = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(msc)),
    .param1 = SCRL_RIGHT,
    .param2 = 0,
};


/*
 * --------------------------------------------------------------------------
 * Gesture state
 * --------------------------------------------------------------------------
 */

static enum joystick_direction tg_joy = JOY_NONE;
static enum joystick_direction tg_prev_joy = JOY_NONE;

static bool tg_lshift;
static bool tg_rshift;

static bool tg_pending_scroll;
static bool tg_scrolling;
static bool caps_word_triggered;


/*
 * --------------------------------------------------------------------------
 * Delayed work
 * --------------------------------------------------------------------------
 */

static struct k_work_delayable tg_scroll_work;


/*
 * --------------------------------------------------------------------------
 * Resolve reason
 * --------------------------------------------------------------------------
 */

enum tg_resolve_reason {
    TG_RESOLVE_INPUT,
    TG_RESOLVE_TIMEOUT,
};


/*
 * --------------------------------------------------------------------------
 * Timer callback
 * --------------------------------------------------------------------------
 *
 * IMPORTANT:
 *
 * This executes on the Zephyr workqueue.
 *
 * It deliberately does NOT inspect or modify the gesture state and does
 * NOT invoke a behavior.
 *
 * It merely posts an event. The actual decision is made by tg_resolve().
 */

static void tg_scroll_timeout(struct k_work *work)
{
    LOG_DBG("TG: scroll timeout event");

    raise_typeglide_gesture_timeout(
        (struct typeglide_gesture_timeout){}
    );
}


/*
 * --------------------------------------------------------------------------
 * Gesture resolver
 * --------------------------------------------------------------------------
 *
 * This is the single owner of the gesture state machine.
 *
 * Both joystick/Shift input and the timer timeout eventually come through
 * here.
 */

static void tg_resolve(enum tg_resolve_reason reason)
{
    bool shift = tg_lshift || tg_rshift;

    LOG_DBG(
        "RESOLVE: reason=%d joy=%d prev=%d shift=%d pending=%d scrolling=%d",
        reason,
        tg_joy,
        tg_prev_joy,
        shift,
        tg_pending_scroll,
        tg_scrolling
    );


    /*
     * ----------------------------------------------------------------------
     * TIMEOUT
     * ----------------------------------------------------------------------
     *
     * The timer has expired.
     *
     * We now inspect the CURRENT state and decide whether scrolling should
     * actually start.
     *
     * This is important: the timer itself does not make the decision.
     */

    if (reason == TG_RESOLVE_TIMEOUT) {

        if (!tg_pending_scroll) {
            LOG_DBG("TG: timeout ignored");
            return;
        }

        /*
         * The gesture is no longer a valid scroll gesture.
         *
         * For example:
         *
         *   FORWARD -> release -> timeout
         *
         * or:
         *
         *   FORWARD -> SHIFT -> timeout
         */

        if (tg_joy != JOY_FORWARD || shift) {
            tg_pending_scroll = false;

            LOG_DBG("TG: timeout no longer valid");

            return;
        }

        /*
         * The joystick is still FORWARD and no Shift is held.
         *
         * Confirm the scroll gesture.
         */

        tg_pending_scroll = false;
        tg_scrolling = true;

        LOG_DBG("TG: timeout -> scroll start");

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

        return;
    }


    /*
     * ----------------------------------------------------------------------
     * INPUT
     * ----------------------------------------------------------------------
     *
     * Normal Shift / joystick state change.
     */


    /*
     * If scrolling is active and FORWARD is no longer active,
     * release the scroll behavior.
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


    /*
     * ----------------------------------------------------------------------
     * FORWARD + SHIFT = CAPS WORD
     * ----------------------------------------------------------------------
     */

    bool gesture = (tg_joy == JOY_FORWARD && shift);

    if (gesture) {

        /*
         * FORWARD may previously have started the scroll disambiguation
         * timer. Cancel that pending gesture.
         *
         * State is changed BEFORE cancelling the work item. Even if the
         * work item has become runnable, the eventual timeout event will
         * see pending == false and do nothing.
         */

        if (tg_pending_scroll) {

            tg_pending_scroll = false;

            k_work_cancel_delayable(&tg_scroll_work);

            LOG_DBG(
                "TG: pending scroll cancelled -> CAPS WORD"
            );
        }


        /*
         * Invoke CAPS WORD only once for this gesture.
         */

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
     * ----------------------------------------------------------------------
     * No longer in the CAPS WORD gesture.
     * ----------------------------------------------------------------------
     */

    if (tg_joy != JOY_FORWARD) {
        caps_word_triggered = false;
    }


    /*
     * ----------------------------------------------------------------------
     * New FORWARD without SHIFT
     * ----------------------------------------------------------------------
     *
     * Don't scroll immediately.
     *
     * Start the ambiguity timer:
     *
     *     FORWARD + no Shift
     *          |
     *          +-- Shift arrives --> CAPS WORD
     *          |
     *          +-- timeout -------> SCROLL
     */

    if (tg_joy == JOY_FORWARD &&
        tg_prev_joy != JOY_FORWARD &&
        !shift) {

        tg_pending_scroll = true;

        LOG_DBG(
            "TG: joystick pending -> %d ms",
            TG_SCROLL_TIMEOUT_MS
        );

        k_work_reschedule(
            &tg_scroll_work,
            K_MSEC(TG_SCROLL_TIMEOUT_MS)
        );
    }


    tg_prev_joy = tg_joy;
}


/*
 * --------------------------------------------------------------------------
 * Shift key listener
 * --------------------------------------------------------------------------
 */

static int tg_gesture_keycode_listener(const zmk_event_t *eh)
{
    const struct zmk_keycode_state_changed *ev =
        as_zmk_keycode_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }


    /*
     * USB HID usage IDs:
     *
     *   225 = Left Shift
     *   229 = Right Shift
     */

    if (ev->usage_page == 0x07) {

        if (ev->keycode == 225) {
            tg_lshift = ev->state;

        } else if (ev->keycode == 229) {
            tg_rshift = ev->state;
        }
    }


    LOG_DBG(
        "TG SHIFT: event key=%u state=%d own=L%d R%d",
        ev->keycode,
        ev->state,
        tg_lshift,
        tg_rshift
    );


    tg_resolve(TG_RESOLVE_INPUT);

    return ZMK_EV_EVENT_BUBBLE;
}


/*
 * --------------------------------------------------------------------------
 * Joystick listener
 * --------------------------------------------------------------------------
 */

static int tg_gesture_joystick_listener(const zmk_event_t *eh)
{
    const struct typeglide_joystick_state_changed *ev =
        as_typeglide_joystick_state_changed(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }


    tg_joy = ev->direction;


    LOG_DBG(
        "TG STATE: joy=%d",
        tg_joy
    );

    LOG_DBG(
        "TG MODS: LSHIFT=%d RSHIFT=%d",
        tg_lshift,
        tg_rshift
    );


    tg_resolve(TG_RESOLVE_INPUT);

    return ZMK_EV_EVENT_BUBBLE;
}


/*
 * --------------------------------------------------------------------------
 * Timeout event listener
 * --------------------------------------------------------------------------
 */

static int tg_gesture_timeout_listener(const zmk_event_t *eh)
{
    const struct typeglide_gesture_timeout *ev =
        as_typeglide_gesture_timeout(eh);

    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    LOG_DBG("TG: timeout event received");

    tg_resolve(TG_RESOLVE_TIMEOUT);

    return ZMK_EV_EVENT_BUBBLE;
}


/*
 * --------------------------------------------------------------------------
 * Initialization
 * --------------------------------------------------------------------------
 */

static int tg_init(void)
{
    k_work_init_delayable(
        &tg_scroll_work,
        tg_scroll_timeout
    );
    return 0;
}

SYS_INIT(tg_init, APPLICATION, 0);


/*
 * --------------------------------------------------------------------------
 * ZMK event subscriptions
 * --------------------------------------------------------------------------
 */

ZMK_LISTENER(tg_gesture, tg_gesture_keycode_listener);
ZMK_SUBSCRIPTION(
    tg_gesture,
    zmk_keycode_state_changed
);


ZMK_LISTENER(
    tg_gesture_joystick,
    tg_gesture_joystick_listener
);

ZMK_SUBSCRIPTION(
    tg_gesture_joystick,
    typeglide_joystick_state_changed
);


ZMK_LISTENER(
    tg_gesture_timeout,
    tg_gesture_timeout_listener
);

ZMK_SUBSCRIPTION(
    tg_gesture_timeout,
    typeglide_gesture_timeout
);
