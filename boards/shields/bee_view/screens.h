/*
 * SPDX-License-Identifier: MIT
 *
 * The two bee_view screens. Each function redraws the whole gfx framebuffer
 * from a plain state struct, so this file knows nothing about ZMK.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BEE_WPM_SAMPLES 12
#define BEE_PROFILES 5

#define BEE_MOD_SFT 0x1
#define BEE_MOD_CTL 0x2
#define BEE_MOD_ALT 0x4
#define BEE_MOD_GUI 0x8

struct bee_left_state {
    uint8_t layer;
    const char *layer_name;
    uint8_t battery;
    int16_t peripheral_battery; /* -1 until the right half has reported */
    bool low;                   /* this half's battery is low */
    bool peripheral_low;
    bool usb_output; /* keystrokes are going out over USB, not Bluetooth */
    uint8_t profile;
    bool profile_connected;
    uint8_t mods; /* BEE_MOD_* */
    uint8_t wpm[BEE_WPM_SAMPLES];
    bool idle;
    bool blink; /* alternates while an alert is showing */
};

struct bee_right_state {
    uint8_t battery;
    bool low;
    bool connected; /* linked to the left half */
    bool idle;
    bool typing;
    bool wings_down;
    bool eyes_closed;
    bool blink;
};

void bee_draw_left(const struct bee_left_state *s);
void bee_draw_right(const struct bee_right_state *s);
