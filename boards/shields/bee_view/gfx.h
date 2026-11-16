/*
 * SPDX-License-Identifier: MIT
 *
 * Tiny 1-bit drawing library for the bee_view screens. It draws into its own
 * 68x160 portrait framebuffer and has no dependencies, so the screens can be
 * rendered on a PC as well as on the keyboard.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GFX_W 68
#define GFX_H 160

/* How a shape changes the pixels it covers. */
enum gfx_paint {
    GFX_ON,  /* light every pixel */
    GFX_OFF, /* clear every pixel */
    GFX_W25, /* light one pixel in four (faint grey on black) */
    GFX_W50, /* light every other pixel (checkerboard) */
    GFX_K25, /* clear one pixel in four (light shading on white) */
    GFX_K50, /* clear every other pixel (dark shading on white) */
};

void gfx_clear(void);
bool gfx_get(int x, int y);
void gfx_px(int x, int y, enum gfx_paint p);
void gfx_rect(int x, int y, int w, int h, enum gfx_paint p);
void gfx_frame(int x, int y, int w, int h, int t, enum gfx_paint p);
void gfx_rrect(int x, int y, int w, int h, int r, enum gfx_paint p);
void gfx_disc(int cx, int cy, int r, enum gfx_paint p);
void gfx_line(int x0, int y0, int x1, int y1, int t, enum gfx_paint p);
void gfx_hazard(int x, int y, int w, int h);
void gfx_corners(int x, int y, int w, int h, int len, enum gfx_paint p);

/* 5x7 font, scaled by sx/sy, with `gap` unscaled pixels between glyphs. */
int gfx_text_width(const char *s, int sx, int gap);
void gfx_text(int x, int y, const char *s, int sx, int sy, int gap, enum gfx_paint p);

/* 3x5 font for tiny labels (letters and digits only). */
int gfx_tiny_width(const char *s);
void gfx_tiny(int x, int y, const char *s, enum gfx_paint p);
