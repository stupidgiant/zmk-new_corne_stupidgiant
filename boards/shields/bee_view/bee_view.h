/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Copies the gfx framebuffer to the display. Call after drawing a screen. */
void bee_view_flush(void);

/* Implemented by widget_central.c or widget_peripheral.c, whichever is built. */
void bee_view_widget_init(void);
