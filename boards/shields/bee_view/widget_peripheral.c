/*
 * SPDX-License-Identifier: MIT
 *
 * Right half: the bee. It flaps while you type on this half, blinks now and
 * then, and sleeps when the keyboard goes idle.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include <string.h>

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "bee_view.h"
#include "screens.h"

#define FLAP_MS 200         /* one wing beat while typing */
#define FLAP_HOLD_MS 1200   /* keep flapping this long after the last key */
#define ACTIVE_HOLD_MS 5000 /* banner reads ACTIVE this long after the last key */
#define ALERT_MS 500
#define QUIET_MS 1000

static struct bee_right_state state;
static struct bee_right_state drawn;
static bool drawn_once;
static bool usb_powered;
static int64_t last_press = -ACTIVE_HOLD_MS;
static int64_t next_blink;
static lv_timer_t *anim;

/* One animation step. The timer slows down or stops when nothing is moving,
 * and the screen is only redrawn when the picture would change. */
static void tick(lv_timer_t *timer) {
    int64_t now = k_uptime_get();
    bool flapping = now - last_press < FLAP_HOLD_MS;
    bool alert;

    state.low = !usb_powered && state.battery > 0 && state.battery <= CONFIG_BEE_VIEW_LOW_BATTERY;
    alert = state.low || !state.connected;

    state.typing = now - last_press < ACTIVE_HOLD_MS;
    state.wings_down = flapping ? !state.wings_down : false;
    state.blink = alert && ((now / ALERT_MS) & 1);

    if (state.eyes_closed) {
        state.eyes_closed = false;
    } else if (!state.idle && now >= next_blink) {
        state.eyes_closed = true;
        next_blink = now + 3000 + (now % 2500);
    }

    if (!drawn_once || memcmp(&state, &drawn, sizeof(state)) != 0) {
        bee_draw_right(&state);
        bee_view_flush();
        drawn = state;
        drawn_once = true;
    }

    if (flapping || state.eyes_closed) {
        lv_timer_set_period(anim, FLAP_MS);
    } else if (alert) {
        lv_timer_set_period(anim, ALERT_MS);
    } else if (state.idle) {
        lv_timer_pause(anim);
    } else {
        lv_timer_set_period(anim, QUIET_MS);
    }
}

/* Wake the animation so a state change shows up straight away. */
static void kick(void) {
    lv_timer_resume(anim);
    lv_timer_set_period(anim, FLAP_MS);
    lv_timer_ready(anim);
}

/* Battery ------------------------------------------------------------------ */

struct battery_state {
    uint8_t level;
    bool usb;
};

static void battery_update_cb(struct battery_state s) {
    state.battery = s.level;
    usb_powered = s.usb;
    kick();
}

static struct battery_state battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_battery, struct battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(bee_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(bee_battery, zmk_usb_conn_state_changed);
#endif

/* Link to the left half ---------------------------------------------------- */

struct link_state {
    bool connected;
};

static void link_update_cb(struct link_state s) {
    state.connected = s.connected;
    kick();
}

static struct link_state link_get_state(const zmk_event_t *eh) {
    return (struct link_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_link, struct link_state, link_update_cb, link_get_state)
ZMK_SUBSCRIPTION(bee_link, zmk_split_peripheral_status_changed);

/* Idle ---------------------------------------------------------------------- */

struct activity_state {
    bool idle;
};

static void activity_update_cb(struct activity_state s) {
    state.idle = s.idle;
    kick();
}

static struct activity_state activity_get_state(const zmk_event_t *eh) {
    return (struct activity_state){.idle = zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE};
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_activity, struct activity_state, activity_update_cb,
                            activity_get_state)
ZMK_SUBSCRIPTION(bee_activity, zmk_activity_state_changed);

/* Key presses on this half -------------------------------------------------- */

struct press_state {
    bool pressed;
};

static void press_update_cb(struct press_state s) {
    if (s.pressed) {
        last_press = k_uptime_get();
        kick();
    }
}

static struct press_state press_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    return (struct press_state){.pressed = (ev != NULL) && ev->state};
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_press, struct press_state, press_update_cb, press_get_state)
ZMK_SUBSCRIPTION(bee_press, zmk_position_state_changed);

void bee_view_widget_init(void) {
    anim = lv_timer_create(tick, QUIET_MS, NULL);
    next_blink = k_uptime_get() + 3000;

    bee_battery_init();
    bee_link_init();
    bee_activity_init();
    bee_press_init();
}
