/*
 * SPDX-License-Identifier: MIT
 */

#include "gfx.h"

#include <string.h>

static uint8_t fb[GFX_H][(GFX_W + 7) / 8];

void gfx_clear(void) { memset(fb, 0, sizeof(fb)); }

bool gfx_get(int x, int y) {
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) {
        return false;
    }
    return fb[y][x >> 3] & (1 << (x & 7));
}

void gfx_px(int x, int y, enum gfx_paint p) {
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) {
        return;
    }
    bool quarter = !(x & 1) && !(y & 1);
    bool half = !((x + y) & 1);
    uint8_t bit = 1 << (x & 7);
    uint8_t *cell = &fb[y][x >> 3];

    switch (p) {
    case GFX_ON:
        *cell |= bit;
        break;
    case GFX_OFF:
        *cell &= ~bit;
        break;
    case GFX_W25:
        if (quarter) {
            *cell |= bit;
        }
        break;
    case GFX_W50:
        if (half) {
            *cell |= bit;
        }
        break;
    case GFX_K25:
        if (quarter) {
            *cell &= ~bit;
        }
        break;
    case GFX_K50:
        if (half) {
            *cell &= ~bit;
        }
        break;
    }
}

void gfx_rect(int x, int y, int w, int h, enum gfx_paint p) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            gfx_px(i, j, p);
        }
    }
}

void gfx_frame(int x, int y, int w, int h, int t, enum gfx_paint p) {
    gfx_rect(x, y, w, t, p);
    gfx_rect(x, y + h - t, w, t, p);
    gfx_rect(x, y + t, t, h - 2 * t, p);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, p);
}

/* Rounded rectangle: corner pixels are kept when inside a circle of radius r. */
void gfx_rrect(int x, int y, int w, int h, int r, enum gfx_paint p) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int dx = 0, dy = 0;
            if (i < r) {
                dx = r - i;
            } else if (i >= w - r) {
                dx = i - (w - r - 1);
            }
            if (j < r) {
                dy = r - j;
            } else if (j >= h - r) {
                dy = j - (h - r - 1);
            }
            if (dx && dy && (2 * dx - 1) * (2 * dx - 1) + (2 * dy - 1) * (2 * dy - 1) > 4 * r * r) {
                continue;
            }
            gfx_px(x + i, y + j, p);
        }
    }
}

void gfx_disc(int cx, int cy, int r, enum gfx_paint p) {
    for (int j = -r; j <= r; j++) {
        for (int i = -r; i <= r; i++) {
            if (i * i + j * j <= r * r) {
                gfx_px(cx + i, cy + j, p);
            }
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, int t, enum gfx_paint p) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        gfx_rect(x0, y0, t, t, p);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* Diagonal warning stripes. */
void gfx_hazard(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            gfx_px(x + i, y + j, ((i + j) % 6) < 3 ? GFX_ON : GFX_OFF);
        }
    }
}

/* Four L-shaped corner brackets around a box. */
void gfx_corners(int x, int y, int w, int h, int len, enum gfx_paint p) {
    gfx_rect(x, y, len, 1, p);
    gfx_rect(x, y, 1, len, p);
    gfx_rect(x + w - len, y, len, 1, p);
    gfx_rect(x + w - 1, y, 1, len, p);
    gfx_rect(x, y + h - 1, len, 1, p);
    gfx_rect(x, y + h - len, 1, len, p);
    gfx_rect(x + w - len, y + h - 1, len, 1, p);
    gfx_rect(x + w - 1, y + h - len, 1, len, p);
}

/* 5x7 font: five column bytes per glyph, bit 0 is the top row. */
struct glyph57 {
    char c;
    uint8_t col[5];
};

static const struct glyph57 font57[] = {
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}}, {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}}, {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}}, {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}}, {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}}, {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}}, {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}}, {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}}, {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}}, {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}}, {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
};

static char upper(char c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }

static const uint8_t *glyph57(char c) {
    c = upper(c);
    for (unsigned i = 0; i < sizeof(font57) / sizeof(font57[0]); i++) {
        if (font57[i].c == c) {
            return font57[i].col;
        }
    }
    return NULL; /* unknown characters and spaces draw as blanks */
}

int gfx_text_width(const char *s, int sx, int gap) {
    int n = strlen(s);
    return n ? n * 5 * sx + (n - 1) * gap : 0;
}

void gfx_text(int x, int y, const char *s, int sx, int sy, int gap, enum gfx_paint p) {
    for (; *s; s++, x += 5 * sx + gap) {
        const uint8_t *g = glyph57(*s);
        if (!g) {
            continue;
        }
        for (int c = 0; c < 5; c++) {
            for (int r = 0; r < 7; r++) {
                if (g[c] & (1 << r)) {
                    gfx_rect(x + c * sx, y + r * sy, sx, sy, p);
                }
            }
        }
    }
}

/* 3x5 font: 15 bits per glyph, top row first, leftmost pixel is the high bit. */
#define T(a, b, c, d, e) (uint16_t)(((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e))

static const uint16_t tiny_alpha[26] = {
    T(2, 5, 7, 5, 5), T(6, 5, 6, 5, 6), T(3, 4, 4, 4, 3), T(6, 5, 5, 5, 6), T(7, 4, 6, 4, 7),
    T(7, 4, 6, 4, 4), T(3, 4, 5, 5, 3), T(5, 5, 7, 5, 5), T(7, 2, 2, 2, 7), T(1, 1, 1, 5, 2),
    T(5, 5, 6, 5, 5), T(4, 4, 4, 4, 7), T(5, 7, 7, 5, 5), T(7, 5, 5, 5, 5), T(2, 5, 5, 5, 2),
    T(6, 5, 6, 4, 4), T(2, 5, 5, 7, 3), T(6, 5, 6, 5, 5), T(3, 4, 2, 1, 6), T(7, 2, 2, 2, 2),
    T(5, 5, 5, 5, 7), T(5, 5, 5, 5, 2), T(5, 5, 7, 7, 5), T(5, 5, 2, 5, 5), T(5, 5, 2, 2, 2),
    T(7, 1, 2, 4, 7),
};

static const uint16_t tiny_digit[10] = {
    T(7, 5, 5, 5, 7), T(2, 6, 2, 2, 7), T(6, 1, 2, 4, 7), T(6, 1, 2, 1, 6), T(5, 5, 7, 1, 1),
    T(7, 4, 6, 1, 6), T(3, 4, 7, 5, 7), T(7, 1, 2, 2, 2), T(7, 5, 7, 5, 7), T(7, 5, 7, 1, 6),
};

int gfx_tiny_width(const char *s) {
    int n = strlen(s);
    return n ? n * 4 - 1 : 0;
}

void gfx_tiny(int x, int y, const char *s, enum gfx_paint p) {
    for (; *s; s++, x += 4) {
        char c = upper(*s);
        uint16_t g = 0;
        if (c >= 'A' && c <= 'Z') {
            g = tiny_alpha[c - 'A'];
        } else if (c >= '0' && c <= '9') {
            g = tiny_digit[c - '0'];
        }
        for (int r = 0; r < 5; r++) {
            for (int col = 0; col < 3; col++) {
                if (g & (1 << (14 - (r * 3 + col)))) {
                    gfx_px(x + col, y + r, p);
                }
            }
        }
    }
}
