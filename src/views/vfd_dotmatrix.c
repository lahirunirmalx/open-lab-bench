/**
 * Dot-matrix VFD digit renderer — see vfd_dotmatrix.h.
 *
 * 5 columns × 7 rows per glyph, each glyph encoded as 5 bytes (one per
 * column, low bit = top dot). Only the characters that appear in numeric
 * readouts are encoded (digits, dot, minus, space).
 */

#include "vfd_dotmatrix.h"

#include <stdint.h>

static const uint8_t DOT_MATRIX_FONT[12][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
};

int vfd_char_width(int dot_size, int dot_gap) {
    return 5 * (dot_size * 2 + dot_gap);
}

int vfd_char_height(int dot_size, int dot_gap) {
    return 7 * (dot_size * 2 + dot_gap);
}

static void set_color(SDL_Renderer *r, vfd_color_t c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void draw_dot(SDL_Renderer *r, int cx, int cy, int radius,
                     vfd_color_t col, bool on) {
    if (!on) {
        vfd_color_t dim = { (Uint8)(col.r / 15), (Uint8)(col.g / 15),
                            (Uint8)(col.b / 15), 80 };
        set_color(r, dim);
        for (int dy = -radius + 1; dy < radius; dy++)
            for (int dx = -radius + 1; dx < radius; dx++)
                if (dx * dx + dy * dy < radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        return;
    }

    int glow_r = radius + 2;
    vfd_color_t glow = { (Uint8)(col.r / 6), (Uint8)(col.g / 6),
                         (Uint8)(col.b / 6), 60 };
    set_color(r, glow);
    for (int dy = -glow_r; dy <= glow_r; dy++) {
        for (int dx = -glow_r; dx <= glow_r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= glow_r * glow_r && d2 > radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }

    set_color(r, col);
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++)
            if (dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);

    vfd_color_t hi = { (Uint8)(col.r + (255 - col.r) / 2),
                       (Uint8)(col.g + (255 - col.g) / 2),
                       (Uint8)(col.b + (255 - col.b) / 2), 255 };
    set_color(r, hi);
    int hi_r = radius / 2;
    if (hi_r < 1) hi_r = 1;
    for (int dy = -hi_r; dy <= hi_r; dy++)
        for (int dx = -hi_r; dx <= hi_r; dx++)
            if (dx * dx + dy * dy <= hi_r * hi_r)
                SDL_RenderDrawPoint(r, cx + dx - 1, cy + dy - 1);
}

static void draw_char(SDL_Renderer *r, int x, int y, int ch_idx,
                      int dot_size, int dot_gap,
                      vfd_color_t on_col, vfd_color_t off_col, bool show_off) {
    if (ch_idx < 0 || ch_idx > 11) return;
    const uint8_t *pattern = DOT_MATRIX_FONT[ch_idx];
    int spacing = dot_size * 2 + dot_gap;
    for (int col = 0; col < 5; col++) {
        uint8_t coldata = pattern[col];
        for (int row = 0; row < 7; row++) {
            bool on = (coldata >> row) & 1;
            int px = x + col * spacing + dot_size;
            int py = y + row * spacing + dot_size;
            if (on)            draw_dot(r, px, py, dot_size, on_col, true);
            else if (show_off) draw_dot(r, px, py, dot_size, off_col, false);
        }
    }
}

int vfd_draw_number(SDL_Renderer *r, int x, int y, const char *str,
                    int dot_size, int dot_gap, int char_gap,
                    vfd_color_t on_col, vfd_color_t off_col, bool show_off) {
    int cx = x;
    int char_w = vfd_char_width(dot_size, dot_gap);
    for (const char *p = str; *p; p++) {
        int ch_idx = -1;
        if      (*p >= '0' && *p <= '9') ch_idx = *p - '0';
        else if (*p == '.')              ch_idx = 10;
        else if (*p == '-')              ch_idx = 11;
        else if (*p == ' ')              { cx += char_w + char_gap; continue; }

        if (ch_idx >= 0) {
            draw_char(r, cx, y, ch_idx, dot_size, dot_gap, on_col, off_col, show_off);
            cx += (ch_idx == 10) ? ((dot_size * 2 + dot_gap) * 2 + char_gap)
                                 : (char_w + char_gap);
        }
    }
    return cx - x;
}
