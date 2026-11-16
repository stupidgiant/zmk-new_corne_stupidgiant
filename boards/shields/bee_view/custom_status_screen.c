/*
 * SPDX-License-Identifier: MIT
 *
 * The screens are drawn upright (68 wide, 160 tall) by gfx.c. The nice!view
 * panel is 160x68 and mounted on its side, so this file turns the picture a
 * quarter turn while copying it into one full-screen LVGL canvas. The turn
 * matches the stock nice_view shield.
 */

#include <lvgl.h>
#include <zephyr/kernel.h>

#include "bee_view.h"
#include "gfx.h"

#define PANEL_W 160
#define PANEL_H 68

static lv_color_t panel[PANEL_W * PANEL_H];
static lv_obj_t *canvas;

void bee_view_flush(void) {
    /* On this panel LVGL's "black" is the light, reflective pixel state and
     * "white" is the dark one, so lit pixels are drawn in black. */
    const lv_color_t on =
        IS_ENABLED(CONFIG_BEE_VIEW_INVERTED) ? lv_color_white() : lv_color_black();
    const lv_color_t off =
        IS_ENABLED(CONFIG_BEE_VIEW_INVERTED) ? lv_color_black() : lv_color_white();

    for (int y = 0; y < GFX_H; y++) {
        for (int x = 0; x < GFX_W; x++) {
            panel[x * PANEL_W + (PANEL_W - 1 - y)] = gfx_get(x, y) ? on : off;
        }
    }
    lv_obj_invalidate(canvas);
}

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas, panel, PANEL_W, PANEL_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);

    bee_view_widget_init();

    return screen;
}
