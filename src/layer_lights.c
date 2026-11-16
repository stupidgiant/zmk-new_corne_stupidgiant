/*
 * SPDX-License-Identifier: MIT
 *
 * Layer lights: while a layer listed in the keymap's `lyl` node is active, the
 * LEDs under the keys that do something on that layer light up in the layer's
 * color.
 *
 * How it fits together:
 *  - The left half knows the keymap. On every layer change it builds a bitmask
 *    of the key positions that are not &trans or &none on the top layer, and
 *    invokes the `lyl` behavior with that mask and a color. The behavior is
 *    "global", so ZMK runs it on both halves.
 *  - Each half lights the LEDs whose key position is in the mask. Which LED
 *    sits under which key is learned once by calibration and kept in flash.
 *    Until then every LED lights, so the layer color still shows.
 *  - Calibration runs on the left half first and then on the right. LEDs light
 *    white one at a time and the next key pressed on that half is taken as the
 *    key above it; an LED nobody answers within 15 seconds is skipped. The
 *    left half holds the keymap on an all-&none layer meanwhile, so the key
 *    presses type nothing.
 *  - The LED strip is shared with ZMK's RGB underglow. While layer lights are
 *    showing, underglow is switched off, and it is put back afterwards.
 */

#define DT_DRV_COMPAT zmk_behavior_layer_lights

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <drivers/ext_power.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define NUM_LEDS DT_PROP(STRIP_NODE, chain_length)
#define MAX_POSITIONS 48
#define UNMAPPED 0xFF

/* The two binding parameters carry: a 48-bit key mask (all of param1 plus the
 * low 16 bits of param2), a 12-bit color (param2 bits 16-27) and a command
 * (param2 bits 28-31). */
#define CMD_SHOW 0x0
#define CMD_CALIBRATE_RIGHT 0xE /* sent by the left half when its own turn is over */
#define CMD_CALIBRATE 0xF       /* the calibration key */

#define IS_CENTRAL (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#define POWER_SETTLE_MS 60
#define DONE_FLASH_MS 700
#define SKIP_LED_MS 15000

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));

static uint8_t led_position[NUM_LEDS]; /* key position under each LED */
static uint8_t led_position_backup[NUM_LEDS];
static struct led_rgb pixels[NUM_LEDS];

static uint64_t want_mask;
static struct led_rgb want_color;

static bool calibrating;
static uint8_t cal_index;
static bool save_pending;
static bool done_flash;

/* What the strip was doing before layer lights took it over. */
static bool taken_over;
static bool underglow_was_on;
static bool ext_power_was_on;

static void apply(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(apply_work, apply);
static void skip_led(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(skip_work, skip_led);

/* Left half only: called when this half's calibration turn ends. */
static void calibration_turn_over(void);

static bool is_calibrated(void) {
    for (int i = 0; i < NUM_LEDS; i++) {
        if (led_position[i] != UNMAPPED) {
            return true;
        }
    }
    return false;
}

static struct led_rgb dim(struct led_rgb c) {
    return (struct led_rgb){
        .r = c.r * CONFIG_ZMK_LAYER_LIGHTS_BRIGHTNESS / 100,
        .g = c.g * CONFIG_ZMK_LAYER_LIGHTS_BRIGHTNESS / 100,
        .b = c.b * CONFIG_ZMK_LAYER_LIGHTS_BRIGHTNESS / 100,
    };
}

/* Returns true when power was only just switched on, in which case the LEDs
 * need a moment before they will accept data. */
static bool take_over(void) {
    if (taken_over) {
        return false;
    }
    taken_over = true;

    ext_power_was_on = ext_power_get(ext_power) > 0;
    zmk_rgb_underglow_get_state(&underglow_was_on);
    if (underglow_was_on) {
        zmk_rgb_underglow_off(); /* this also cuts power, so turn it back on */
    }
    ext_power_enable(ext_power);
    return true;
}

static void give_back(void) {
    bool underglow_on = false;

    if (!taken_over) {
        return;
    }
    taken_over = false;

    memset(pixels, 0, sizeof(pixels));
    led_strip_update_rgb(strip, pixels, NUM_LEDS);

    zmk_rgb_underglow_get_state(&underglow_on);
    if (underglow_was_on && !underglow_on) {
        zmk_rgb_underglow_on();
    } else if (underglow_was_on && underglow_on) {
        /* It was switched off above, so it can only be on again because the
         * RGB toggle key was pressed meanwhile. The intent was to turn it off. */
        zmk_rgb_underglow_off();
    } else if (!underglow_on && !ext_power_was_on) {
        ext_power_disable(ext_power);
    }
}

static void apply(struct k_work *work) {
    if (save_pending) {
        save_pending = false;
        settings_save_one("lyl/map", led_position, sizeof(led_position));
    }

    if (!calibrating && !done_flash && want_mask == 0) {
        give_back();
        return;
    }

    if (take_over()) {
        k_work_reschedule(&apply_work, K_MSEC(POWER_SETTLE_MS));
        return;
    }

    memset(pixels, 0, sizeof(pixels));

    if (calibrating) {
        pixels[cal_index] = dim((struct led_rgb){.r = 255, .g = 255, .b = 255});
    } else if (done_flash) {
        for (int i = 0; i < NUM_LEDS; i++) {
            pixels[i] = dim((struct led_rgb){.g = 255});
        }
        done_flash = false;
        k_work_reschedule(&apply_work, K_MSEC(DONE_FLASH_MS));
    } else {
        bool calibrated = is_calibrated();

        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t pos = led_position[i];

            if (!calibrated || (pos < MAX_POSITIONS && (want_mask & BIT64(pos)))) {
                pixels[i] = dim(want_color);
            }
        }
    }

    led_strip_update_rgb(strip, pixels, NUM_LEDS);
}

/* Calibration: the LEDs light one at a time and the next key pressed on this
 * half is recorded as the key above the lit LED. */

static void calibration_start(void) {
    memcpy(led_position_backup, led_position, sizeof(led_position));
    memset(led_position, UNMAPPED, sizeof(led_position));
    cal_index = 0;
    calibrating = true;
    k_work_reschedule(&skip_work, K_MSEC(SKIP_LED_MS));
}

static void calibration_finish(void) {
    calibrating = false;
    k_work_cancel_delayable(&skip_work);

    if (is_calibrated()) {
        save_pending = true;
        done_flash = true;
    } else {
        /* Nobody pressed anything, so keep the map from before. */
        memcpy(led_position, led_position_backup, sizeof(led_position));
    }
    calibration_turn_over();
}

static void calibration_record(uint8_t position) {
    led_position[cal_index] = position;
    if (++cal_index >= NUM_LEDS) {
        calibration_finish();
    } else {
        k_work_reschedule(&skip_work, K_MSEC(SKIP_LED_MS));
    }
    k_work_reschedule(&apply_work, K_NO_WAIT);
}

static void skip_led(struct k_work *work) {
    if (calibrating) {
        calibration_record(UNMAPPED);
    }
}

static void remote_key_pressed(void);

static int position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        remote_key_pressed(); /* a key on the other half */
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif
    if (calibrating) {
        calibration_record(ev->position < MAX_POSITIONS ? ev->position : UNMAPPED);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_lights_position, position_listener);
ZMK_SUBSCRIPTION(layer_lights_position, zmk_position_state_changed);

/* The calibration session, run by the left half: its own turn, then the right
 * half's, with the keymap parked on the calibration layer throughout. */

#if IS_CENTRAL

#include <zmk/keymap.h>

#define SESSION_IDLE_MS 40000

static bool session;
static bool right_turn;
static uint8_t right_presses;

static void session_end(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(session_end_work, session_end);

static void send(uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_DRV_INST(0)),
        .param1 = param1,
        .param2 = param2,
    };
    struct zmk_behavior_binding_event event = {
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    zmk_behavior_invoke_binding(&binding, event, true);
}

static void start_right_turn(struct k_work *work) {
    right_turn = true;
    right_presses = 0;
    send(0, (uint32_t)CMD_CALIBRATE_RIGHT << 28);
    k_work_reschedule(&session_end_work, K_MSEC(SESSION_IDLE_MS));
}

static K_WORK_DEFINE(right_turn_work, start_right_turn);

static void session_end(struct k_work *work) {
    session = false;
    right_turn = false;
#if DT_INST_NODE_HAS_PROP(0, calibration_layer)
    zmk_keymap_layer_deactivate(DT_INST_PROP(0, calibration_layer));
#endif
}

static void calibration_session_begin(void) {
    if (session) {
        return;
    }
    session = true;
#if DT_INST_NODE_HAS_PROP(0, calibration_layer)
    zmk_keymap_layer_activate(DT_INST_PROP(0, calibration_layer));
#endif
    calibration_start();
}

static void calibration_turn_over(void) {
    if (IS_ENABLED(CONFIG_ZMK_SPLIT)) {
        k_work_submit(&right_turn_work);
    } else {
        k_work_reschedule(&session_end_work, K_NO_WAIT);
    }
}

/* The right half does not report back, so its turn is taken to be over once
 * it has had a key press for every LED, or has gone quiet for a while. */
static void remote_key_pressed(void) {
    if (!right_turn) {
        return;
    }
    if (++right_presses >= NUM_LEDS) {
        k_work_reschedule(&session_end_work, K_MSEC(DONE_FLASH_MS));
    } else {
        k_work_reschedule(&session_end_work, K_MSEC(SESSION_IDLE_MS));
    }
}

#else /* right half */

static void calibration_session_begin(void) {} /* waits for CMD_CALIBRATE_RIGHT */
static void calibration_turn_over(void) {}
static void remote_key_pressed(void) {}

#endif

/* The behavior: how a command reaches each half. */

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    uint32_t command = binding->param2 >> 28;

    if (command == CMD_CALIBRATE) {
        calibration_session_begin();
    } else if (command == CMD_CALIBRATE_RIGHT) {
        if (!IS_CENTRAL && !calibrating) {
            calibration_start();
        }
    } else if (command == CMD_SHOW) {
        uint32_t color = (binding->param2 >> 16) & 0xFFF;

        want_mask = ((uint64_t)(binding->param2 & 0xFFFF) << 32) | binding->param1;
        want_color = (struct led_rgb){
            .r = ((color >> 8) & 0xF) * 17,
            .g = ((color >> 4) & 0xF) * 17,
            .b = (color & 0xF) * 17,
        };
    }

    k_work_reschedule(&apply_work, K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api layer_lights_api = {
    .binding_pressed = on_pressed,
    .binding_released = on_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

static int layer_lights_init(const struct device *dev) {
    memset(led_position, UNMAPPED, sizeof(led_position));
    return 0;
}

BEHAVIOR_DT_INST_DEFINE(0, layer_lights_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &layer_lights_api);

/* The saved LED map. */

static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (settings_name_steq(name, "map", NULL) && len == sizeof(led_position)) {
        read_cb(cb_arg, led_position, sizeof(led_position));
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(layer_lights, "lyl", NULL, settings_set, NULL, NULL);

/* Left half only: watch the layers and send out the mask. */

#if IS_CENTRAL

#include <zmk/events/layer_state_changed.h>
#include <zmk/matrix.h>

BUILD_ASSERT(ZMK_KEYMAP_LEN <= MAX_POSITIONS, "layer lights support at most 48 key positions");

static const uint32_t lit_layers[] = DT_INST_PROP(0, layers);
static const uint32_t lit_colors[] = DT_INST_PROP(0, colors);

BUILD_ASSERT(ARRAY_SIZE(lit_layers) == ARRAY_SIZE(lit_colors),
             "lyl needs one color for each layer");

static uint32_t sent_param1 = 0;
static uint32_t sent_param2 = 0;

static bool does_something(const struct zmk_behavior_binding *binding) {
    return binding != NULL && binding->behavior_dev != NULL &&
           strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(trans))) != 0 &&
           strcmp(binding->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(none))) != 0;
}

static void layer_changed(struct k_work *work) {
    zmk_keymap_layer_index_t layer = zmk_keymap_highest_layer_active();
    uint64_t mask = 0;
    uint32_t color = 0;

    for (int i = 0; i < ARRAY_SIZE(lit_layers); i++) {
        if (lit_layers[i] != layer) {
            continue;
        }
        zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(layer);

        for (int pos = 0; pos < ZMK_KEYMAP_LEN; pos++) {
            if (does_something(zmk_keymap_get_layer_binding_at_idx(id, pos))) {
                mask |= BIT64(pos);
            }
        }
        /* 0xRRGGBB down to 0xRGB */
        color = ((lit_colors[i] >> 12) & 0xF00) | ((lit_colors[i] >> 8) & 0x0F0) |
                ((lit_colors[i] >> 4) & 0x00F);
        break;
    }

    uint32_t param1 = (uint32_t)mask;
    uint32_t param2 = (CMD_SHOW << 28) | (color << 16) | (uint32_t)(mask >> 32);

    if (param1 == sent_param1 && param2 == sent_param2) {
        return; /* nothing new to show, so keep the radio quiet */
    }
    sent_param1 = param1;
    sent_param2 = param2;

    send(param1, param2);
}

static K_WORK_DEFINE(layer_work, layer_changed);

static int layer_listener(const zmk_event_t *eh) {
    k_work_submit(&layer_work);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_lights_layer, layer_listener);
ZMK_SUBSCRIPTION(layer_lights_layer, zmk_layer_state_changed);

#endif /* central */

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
