/*
 * SPDX-License-Identifier: MIT
 */

#include "screens.h"
#include "gfx.h"

#include <stdio.h>
#include <string.h>

/* Shared pieces ---------------------------------------------------------- */

/* Inverted title bar with a label on each side, and a dithered fade below. */
static void header(const char *left, const char *right) {
    gfx_rect(4, 3, 60, 9, GFX_ON);
    gfx_text(6, 4, left, 1, 1, 1, GFX_OFF);
    gfx_text(62 - gfx_text_width(right, 1, 1), 4, right, 1, 1, 1, GFX_OFF);
    gfx_rect(4, 12, 60, 2, GFX_W50);
    gfx_rect(4, 14, 60, 2, GFX_W25);
}

/* Status banner: a solid block normally, an outline on the off beat of an alert. */
static void banner(const char *label, bool outline) {
    int x = (GFX_W - gfx_text_width(label, 1, 1)) / 2;

    if (outline) {
        gfx_frame(4, 136, 60, 10, 1, GFX_ON);
        gfx_text(x, 138, label, 1, 1, 1, GFX_ON);
    } else {
        gfx_rect(4, 136, 60, 10, GFX_ON);
        gfx_text(x, 138, label, 1, 1, 1, GFX_OFF);
    }
    gfx_hazard(4, 150, 60, 5);
}

/* Segmented gauge: full segments are solid, empty ones are dithered. */
static void gauge(int x, int y, int segments, int seg_w, int seg_h, int percent) {
    int filled = percent <= 0 ? 0 : (percent * segments + 99) / 100;

    if (filled > segments) {
        filled = segments;
    }
    for (int i = 0; i < segments; i++) {
        gfx_rect(x + i * (seg_w + 1), y, seg_w, seg_h, i < filled ? GFX_ON : GFX_W25);
    }
}

/* Left half ---------------------------------------------------------------- */

/* The layer name in large type with a dithered drop shadow. Names are cut at
 * six characters or the first space or slash, so "FN/NUMPAD" shows as "FN". */
static void layer_title(const struct bee_left_state *s) {
    char name[7] = {0};
    const char *src = s->layer_name;

    for (int i = 0; src && src[i] && src[i] != '/' && src[i] != ' ' && i < 6; i++) {
        name[i] = src[i];
    }
    if (!name[0]) {
        snprintf(name, sizeof(name), "L%d", s->layer);
    }

    int n = strlen(name);
    int sx = n <= 3 ? 3 : 2;
    int gap = n <= 5 ? 2 : 1;
    int w = gfx_text_width(name, sx, gap);
    int x = n <= 5 ? 4 : (GFX_W - w) / 2;

    /* Six letters fill the whole width, so their shadow only drops straight
     * down; a diagonal one would clog the 1px gaps between them. */
    gfx_text(n <= 5 ? x + 2 : x, 21, name, sx, 3, gap, GFX_W50);
    gfx_text(x, 19, name, sx, 3, gap, GFX_ON);
}

static void mod_cell(int x, const char *label, bool held) {
    int tx = x + (14 - gfx_tiny_width(label)) / 2 + 1;

    if (held) {
        gfx_rect(x, 91, 14, 9, GFX_ON);
        gfx_tiny(tx, 93, label, GFX_OFF);
    } else {
        gfx_frame(x, 91, 14, 9, 1, GFX_ON);
        gfx_tiny(tx, 93, label, GFX_ON);
    }
}

/* Typing speed history as a line with a dithered area underneath. Like the
 * stock nice!view graph, it scales itself between the lowest and highest
 * sample on screen. */
static void wpm_graph(const uint8_t *wpm) {
    const int x0 = 7, x1 = 61, top = 115, bottom = 129;
    int lo = 255, hi = 0;
    int px[BEE_WPM_SAMPLES], py[BEE_WPM_SAMPLES];
    char value[4];

    gfx_corners(4, 104, 60, 29, 4, GFX_ON);
    gfx_tiny(7, 107, "WPM", GFX_ON);
    snprintf(value, sizeof(value), "%03d", wpm[BEE_WPM_SAMPLES - 1]);
    gfx_tiny(62 - gfx_tiny_width(value), 107, value, GFX_ON);

    for (int i = 0; i < BEE_WPM_SAMPLES; i++) {
        lo = wpm[i] < lo ? wpm[i] : lo;
        hi = wpm[i] > hi ? wpm[i] : hi;
    }
    int range = hi - lo ? hi - lo : 1;

    for (int i = 0; i < BEE_WPM_SAMPLES; i++) {
        px[i] = x0 + i * (x1 - x0) / (BEE_WPM_SAMPLES - 1);
        py[i] = bottom - (wpm[i] - lo) * (bottom - top) / range;
    }
    for (int i = 0; i + 1 < BEE_WPM_SAMPLES; i++) {
        for (int x = px[i]; x <= px[i + 1]; x++) {
            int y = py[i] + (py[i + 1] - py[i]) * (x - px[i]) / (px[i + 1] - px[i]);
            gfx_rect(x, y + 1, 1, bottom + 1 - y, GFX_W25);
        }
    }
    for (int i = 0; i + 1 < BEE_WPM_SAMPLES; i++) {
        gfx_line(px[i], py[i], px[i + 1], py[i + 1], 1, GFX_ON);
    }
    gfx_rect(px[BEE_WPM_SAMPLES - 1] - 1, py[BEE_WPM_SAMPLES - 1] - 1, 3, 3, GFX_ON);
}

void bee_draw_left(const struct bee_left_state *s) {
    char num[4];

    gfx_clear();

    snprintf(num, sizeof(num), "%02d", s->layer % 100);
    header("LAYER", num);
    layer_title(s);
    gfx_hazard(4, 45, 60, 5);

    /* Batteries for both halves */
    gfx_text(4, 54, "L", 1, 1, 1, GFX_ON);
    gauge(12, 54, 8, 5, 7, s->battery);
    gfx_text(4, 64, "R", 1, 1, 1, GFX_ON);
    gauge(12, 64, 8, 5, 7, s->peripheral_battery);

    gfx_rect(4, 75, 60, 1, GFX_ON);
    gfx_rect(4, 76, 60, 1, GFX_W50);

    /* Output: USB, or the five Bluetooth profiles */
    gfx_text(4, 80, s->usb_output ? "USB" : "BT", 1, 1, 1, GFX_ON);
    for (int i = 0; i < BEE_PROFILES; i++) {
        int x = 24 + i * 8;

        gfx_frame(x, 80, 7, 7, 1, GFX_ON);
        if (i != s->profile) {
            gfx_rect(x + 1, 81, 5, 5, GFX_W25);
        } else if (s->profile_connected) {
            gfx_rect(x + 1, 81, 5, 5, GFX_ON);
        } else {
            gfx_rect(x + 1, 81, 5, 5, GFX_W50);
        }
    }

    mod_cell(4, "SFT", s->mods & BEE_MOD_SFT);
    mod_cell(19, "CTL", s->mods & BEE_MOD_CTL);
    mod_cell(35, "ALT", s->mods & BEE_MOD_ALT);
    mod_cell(50, "GUI", s->mods & BEE_MOD_GUI);

    wpm_graph(s->wpm);

    if (s->low) {
        banner("LOW PWR L", s->blink);
    } else if (s->peripheral_low) {
        banner("LOW PWR R", s->blink);
    } else {
        banner(s->idle ? "IDLE" : "NORMAL", false);
    }
}

/* Right half --------------------------------------------------------------- */

static void wing(int x, int y, int size) {
    gfx_rect(x, y, size, size, GFX_OFF);
    gfx_frame(x, y, size, size, 2, GFX_ON);
}

static void bee(int dy, bool wings_down, bool eyes_closed, bool asleep) {
    int wy = dy + (wings_down ? 5 : 0);

    /* Wings: three square frames a side */
    wing(2, 28 + wy, 16);
    wing(8, 35 + wy, 14);
    wing(14, 43 + wy, 11);
    wing(50, 25 + wy, 16);
    wing(44, 33 + wy, 15);
    wing(40, 42 + wy, 11);

    /* Striped back, drawn as a grey slab behind the body */
    gfx_rrect(19, 55 + dy, 39, 34, 6, GFX_OFF);
    gfx_rrect(20, 56 + dy, 37, 32, 5, GFX_ON);
    gfx_rrect(20, 56 + dy, 37, 32, 5, GFX_K50);

    /* Black outline so the body separates from everything behind it */
    gfx_rrect(10, 50 + dy, 42, 40, 8, GFX_OFF);

    /* Antennae */
    gfx_line(23, 51 + dy, 18, 42 + dy, 2, GFX_ON);
    gfx_line(37, 51 + dy, 42, 42 + dy, 2, GFX_ON);
    gfx_rrect(16, 40 + dy, 5, 4, 1, GFX_ON);
    gfx_rrect(41, 40 + dy, 5, 4, 1, GFX_ON);

    /* Body with shading on the underside and right edge */
    gfx_rrect(12, 52 + dy, 38, 36, 6, GFX_ON);
    gfx_rect(12, 82 + dy, 38, 6, GFX_K50);
    gfx_rect(12, 78 + dy, 38, 4, GFX_K25);
    gfx_rect(46, 52 + dy, 4, 36, GFX_K25);

    /* Square eyes with highlights, or shut */
    if (eyes_closed) {
        gfx_rect(19, 68 + dy, 6, 2, GFX_OFF);
        gfx_rect(37, 68 + dy, 6, 2, GFX_OFF);
    } else {
        gfx_rect(19, 62 + dy, 6, 11, GFX_OFF);
        gfx_rect(37, 62 + dy, 6, 11, GFX_OFF);
        gfx_rect(20, 63 + dy, 2, 3, GFX_ON);
        gfx_rect(38, 63 + dy, 2, 3, GFX_ON);
        gfx_px(23, 70 + dy, GFX_ON);
        gfx_px(41, 70 + dy, GFX_ON);
    }

    /* Smile and blush */
    gfx_px(28, 73 + dy, GFX_OFF);
    gfx_px(29, 74 + dy, GFX_OFF);
    gfx_rect(30, 75 + dy, 3, 1, GFX_OFF);
    gfx_px(33, 74 + dy, GFX_OFF);
    gfx_px(34, 73 + dy, GFX_OFF);
    gfx_rect(14, 75 + dy, 4, 3, GFX_K50);
    gfx_rect(44, 75 + dy, 4, 3, GFX_K50);

    /* Legs */
    static const int8_t legs[4][2] = {{18, 5}, {25, 6}, {34, 6}, {41, 5}};
    for (int i = 0; i < 4; i++) {
        gfx_rect(legs[i][0] - 1, 89 + dy, 5, legs[i][1] + 1, GFX_OFF);
        gfx_rect(legs[i][0], 89 + dy, 3, legs[i][1], GFX_ON);
    }

    if (asleep) {
        gfx_rect(25, 28, 14, 9, GFX_OFF);
        gfx_text(27, 29, "ZZ", 1, 1, 1, GFX_ON);
    }
}

void bee_draw_right(const struct bee_right_state *s) {
    char num[4];

    gfx_clear();
    header("BEE", s->connected ? "LINK" : "LOST");

    /* Spotlight, frame and sparkles */
    gfx_disc(34, 68, 31, GFX_W25);
    gfx_corners(4, 22, 60, 92, 4, GFX_ON);
    gfx_rect(6, 64, 1, 5, GFX_ON);
    gfx_rect(4, 66, 5, 1, GFX_ON);
    gfx_rect(61, 96, 1, 5, GFX_ON);
    gfx_rect(59, 98, 5, 1, GFX_ON);

    bee(0, s->wings_down, s->eyes_closed || s->idle, s->idle);
    gfx_rect(16, 103, 32, 2, GFX_W50);

    /* Battery */
    gfx_tiny(4, 121, "PWR", GFX_ON);
    gauge(18, 120, 6, 4, 7, s->battery);
    snprintf(num, sizeof(num), "%02d", s->battery > 99 ? 99 : s->battery);
    gfx_text(64 - gfx_text_width(num, 1, 1), 120, num, 1, 1, 1, GFX_ON);
    gfx_rect(4, 131, 60, 1, GFX_W50);

    if (!s->connected) {
        banner("NO LINK", s->blink);
    } else if (s->low) {
        banner("LOW PWR", s->blink);
    } else if (s->idle) {
        banner("STANDBY", false);
    } else {
        banner(s->typing ? "ACTIVE" : "READY", false);
    }
}
