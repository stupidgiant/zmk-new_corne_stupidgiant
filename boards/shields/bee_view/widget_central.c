/*
 * SPDX-License-Identifier: MIT
 *
 * Left half: feeds keyboard state into the control-room screen.
 */

#include <zephyr/kernel.h>
#include <lvgl.h>

#include <dt-bindings/zmk/modifiers.h>
#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/wpm.h>

#include "bee_view.h"
#include "screens.h"

static struct bee_left_state state = {.peripheral_battery = -1};
static bool usb_powered;
static bool ready;
static lv_timer_t *blink_timer;

static void redraw(void) {
    if (!ready) {
        return;
    }

    /* A reading of 0 means "not measured yet", so it never raises the alert. */
    state.low = !usb_powered && state.battery > 0 && state.battery <= CONFIG_BEE_VIEW_LOW_BATTERY;
    state.peripheral_low = state.peripheral_battery > 0 &&
                           state.peripheral_battery <= CONFIG_BEE_VIEW_LOW_BATTERY;

    if (state.low || state.peripheral_low) {
        lv_timer_resume(blink_timer);
    } else {
        lv_timer_pause(blink_timer);
        state.blink = false;
    }

    bee_draw_left(&state);
    bee_view_flush();
}

static void blink_cb(lv_timer_t *timer) {
    state.blink = !state.blink;
    redraw();
}

/* Battery ------------------------------------------------------------------ */

struct battery_state {
    uint8_t level;
    bool usb;
};

static void battery_update_cb(struct battery_state s) {
    state.battery = s.level;
    usb_powered = s.usb;
    redraw();
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

/* Right half's battery ------------------------------------------------------ */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

struct peripheral_battery_state {
    int16_t level;
};

static void peripheral_battery_update_cb(struct peripheral_battery_state s) {
    state.peripheral_battery = s.level;
    redraw();
}

static struct peripheral_battery_state peripheral_battery_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    return (struct peripheral_battery_state){.level = (ev != NULL) ? ev->state_of_charge : -1};
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_peripheral_battery, struct peripheral_battery_state,
                            peripheral_battery_update_cb, peripheral_battery_get_state)
ZMK_SUBSCRIPTION(bee_peripheral_battery, zmk_peripheral_battery_state_changed);

#endif

/* Output (USB or a Bluetooth profile) -------------------------------------- */

struct output_state {
    bool usb;
    uint8_t profile;
    bool connected;
};

static void output_update_cb(struct output_state s) {
    state.usb_output = s.usb;
    state.profile = s.profile;
    state.profile_connected = s.connected;
    redraw();
}

static struct output_state output_get_state(const zmk_event_t *eh) {
    return (struct output_state){
        .usb = zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB,
        .profile = zmk_ble_active_profile_index(),
        .connected = zmk_ble_active_profile_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_output, struct output_state, output_update_cb, output_get_state)
ZMK_SUBSCRIPTION(bee_output, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(bee_output, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(bee_output, zmk_usb_conn_state_changed);
#endif

/* Layer --------------------------------------------------------------------- */

struct layer_state {
    uint8_t index;
    const char *name;
};

static void layer_update_cb(struct layer_state s) {
    state.layer = s.index;
    state.layer_name = s.name;
    redraw();
}

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();

    return (struct layer_state){
        .index = index,
        .name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_layer, struct layer_state, layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(bee_layer, zmk_layer_state_changed);

/* Typing speed -------------------------------------------------------------- */

struct wpm_state {
    uint8_t wpm;
};

static void wpm_update_cb(struct wpm_state s) {
    for (int i = 0; i + 1 < BEE_WPM_SAMPLES; i++) {
        state.wpm[i] = state.wpm[i + 1];
    }
    state.wpm[BEE_WPM_SAMPLES - 1] = s.wpm;
    redraw();
}

static struct wpm_state wpm_get_state(const zmk_event_t *eh) {
    return (struct wpm_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_wpm, struct wpm_state, wpm_update_cb, wpm_get_state)
ZMK_SUBSCRIPTION(bee_wpm, zmk_wpm_state_changed);

/* Held modifiers ------------------------------------------------------------ */

struct mods_state {
    uint8_t unused;
};

/* Runs on the display thread after the key event has been fully handled, so
 * the modifier flags are read here and not when the event arrives. Most key
 * presses leave them unchanged and cost no redraw. */
static void mods_update_cb(struct mods_state s) {
    zmk_mod_flags_t flags = zmk_hid_get_explicit_mods();
    uint8_t mods = 0;

    if (flags & (MOD_LSFT | MOD_RSFT)) {
        mods |= BEE_MOD_SFT;
    }
    if (flags & (MOD_LCTL | MOD_RCTL)) {
        mods |= BEE_MOD_CTL;
    }
    if (flags & (MOD_LALT | MOD_RALT)) {
        mods |= BEE_MOD_ALT;
    }
    if (flags & (MOD_LGUI | MOD_RGUI)) {
        mods |= BEE_MOD_GUI;
    }
    if (mods != state.mods) {
        state.mods = mods;
        redraw();
    }
}

static struct mods_state mods_get_state(const zmk_event_t *eh) { return (struct mods_state){0}; }

ZMK_DISPLAY_WIDGET_LISTENER(bee_mods, struct mods_state, mods_update_cb, mods_get_state)
ZMK_SUBSCRIPTION(bee_mods, zmk_keycode_state_changed);

/* Idle ---------------------------------------------------------------------- */

struct activity_state {
    bool idle;
};

static void activity_update_cb(struct activity_state s) {
    state.idle = s.idle;
    redraw();
}

static struct activity_state activity_get_state(const zmk_event_t *eh) {
    return (struct activity_state){.idle = zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE};
}

ZMK_DISPLAY_WIDGET_LISTENER(bee_activity, struct activity_state, activity_update_cb,
                            activity_get_state)
ZMK_SUBSCRIPTION(bee_activity, zmk_activity_state_changed);

void bee_view_widget_init(void) {
    blink_timer = lv_timer_create(blink_cb, 500, NULL);
    lv_timer_pause(blink_timer);

    bee_battery_init();
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    bee_peripheral_battery_init();
#endif
    bee_output_init();
    bee_layer_init();
    bee_wpm_init();
    bee_mods_init();
    bee_activity_init();

    ready = true;
    redraw();
}
