/**
 * Shared "full GUI" toolkit — see full_common.h.
 *
 * Ports the bulk of the original legacy/main.c so both the dual-channel and
 * single-channel "full" views can reuse the rendering. Every helper here
 * takes the per-instance full_ctx_t so nothing leaks into file-scope state.
 */

#include "full_common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* ---- color palette ---- */

const full_color_t COL_BG_DARK      = {30, 30, 32, 255};
const full_color_t COL_BG_PANEL     = {42, 42, 46, 255};
const full_color_t COL_BG_WIDGET    = {28, 28, 30, 255};
const full_color_t COL_HEADER       = {24, 24, 26, 255};
const full_color_t COL_BORDER       = {60, 60, 65, 255};
const full_color_t COL_BORDER_LIGHT = {80, 80, 88, 255};
const full_color_t COL_TEXT         = {200, 200, 205, 255};
const full_color_t COL_TEXT_DIM     = {120, 120, 128, 255};
const full_color_t COL_LABEL        = {160, 160, 168, 255};
const full_color_t COL_ACCENT       = {0, 180, 220, 255};
const full_color_t COL_SUCCESS      = {50, 205, 100, 255};
const full_color_t COL_WARNING      = {255, 180, 0, 255};
const full_color_t COL_ERROR        = {220, 60, 60, 255};
const full_color_t COL_VFD_ON       = {0, 255, 120, 255};
const full_color_t COL_BTN_NORMAL   = {55, 55, 60, 255};
const full_color_t COL_BTN_HOVER    = {70, 70, 78, 255};
const full_color_t COL_BTN_ACTIVE   = {0, 150, 180, 255};

/* Private colors used only inside this file. */
static const full_color_t COL_VFD_BG       = {8, 18, 12, 255};
static const full_color_t COL_VFD_OFF      = {0, 60, 35, 255};
static const full_color_t COL_SCOPE_BG     = {10, 20, 15, 255};
static const full_color_t COL_SCOPE_GRID   = {30, 50, 35, 255};
static const full_color_t COL_SCOPE_LINE   = {80, 255, 120, 255};
static const full_color_t COL_INPUT_BG     = {22, 22, 24, 255};
static const full_color_t COL_INPUT_BORDER = {70, 70, 78, 255};
static const full_color_t COL_INPUT_FOCUS  = {0, 150, 180, 255};

/* ---- primitives ---- */

void full_set_color(SDL_Renderer *r, full_color_t c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void full_fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

void full_draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r, &rect);
}

void full_fill_rounded(SDL_Renderer *r, int x, int y, int w, int h, int radius) {
    full_fill_rect(r, x + radius, y, w - 2 * radius, h);
    full_fill_rect(r, x, y + radius, w, h - 2 * radius);
    for (int corner = 0; corner < 4; corner++) {
        int cx = (corner % 2 == 0) ? x + radius : x + w - radius;
        int cy = (corner < 2) ? y + radius : y + h - radius;
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx * dx + dy * dy <= radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
            }
        }
    }
}

void full_draw_text(full_ctx_t *c, TTF_Font *font, const char *text,
                    int x, int y, full_color_t color, int align) {
    if (!font || !text || !*text) return;
    SDL_Color sdl_col = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, sdl_col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(c->renderer, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        if      (align == 1) dst.x = x - surf->w / 2;
        else if (align == 2) dst.x = x - surf->w;
        SDL_RenderCopy(c->renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void full_draw_text_centered(full_ctx_t *c, TTF_Font *font, const char *text,
                             int cx, int cy, full_color_t color) {
    if (!font || !text || !*text) return;
    SDL_Color sdl_col = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, sdl_col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(c->renderer, surf);
    if (tex) {
        SDL_Rect dst = {cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
        SDL_RenderCopy(c->renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

int full_add_button(full_ctx_t *c, int x, int y, int w, int h, int id) {
    if (c->num_buttons >= FULL_MAX_BTNS) return -1;
    c->buttons[c->num_buttons] = (full_button_t){ .rect = {x, y, w, h}, .id = id };
    return c->num_buttons++;
}

int full_button_at(full_ctx_t *c, int mx, int my) {
    for (int i = 0; i < c->num_buttons; i++) {
        SDL_Rect *r = &c->buttons[i].rect;
        if (mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h)
            return c->buttons[i].id;
    }
    return 0;
}

bool full_point_in_rect(int x, int y, const SDL_Rect *rect) {
    return x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

void full_draw_led(SDL_Renderer *r, int cx, int cy, int radius, bool on,
                   full_color_t on_col) {
    full_color_t col = on ? on_col : COL_TEXT_DIM;
    full_set_color(r, COL_BORDER);
    for (int dy = -radius - 1; dy <= radius + 1; dy++) {
        for (int dx = -radius - 1; dx <= radius + 1; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= (radius + 1) * (radius + 1) && d2 > (radius - 1) * (radius - 1))
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
    full_set_color(r, col);
    for (int dy = -radius + 1; dy < radius; dy++) {
        for (int dx = -radius + 1; dx < radius; dx++) {
            if (dx * dx + dy * dy < (radius - 1) * (radius - 1))
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
    if (on) {
        full_color_t hl = {(Uint8)(col.r + (255 - col.r) / 2),
                           (Uint8)(col.g + (255 - col.g) / 2),
                           (Uint8)(col.b + (255 - col.b) / 2), 255};
        full_set_color(r, hl);
        for (int dy = -radius / 3; dy <= 0; dy++)
            for (int dx = -radius / 3; dx <= 0; dx++)
                if (dx * dx + dy * dy < radius * radius / 9)
                    SDL_RenderDrawPoint(r, cx + dx - 1, cy + dy - 1);
    }
}

void full_draw_button(full_ctx_t *c, int x, int y, int w, int h, const char *text,
                      bool active, bool hover, int id) {
    full_color_t bg = active ? COL_BTN_ACTIVE : (hover ? COL_BTN_HOVER : COL_BTN_NORMAL);
    full_color_t border = active ? COL_ACCENT : COL_BORDER_LIGHT;
    full_color_t text_col = active ? (full_color_t){255, 255, 255, 255} : COL_TEXT;

    full_set_color(c->renderer, bg);
    full_fill_rounded(c->renderer, x, y, w, h, 3);
    full_set_color(c->renderer, border);
    full_draw_rect(c->renderer, x, y, w, h);
    full_draw_text_centered(c, c->font_small, text, x + w / 2, y + h / 2, text_col);
    full_add_button(c, x, y, w, h, id);
}

/* ---- VFD dot matrix font ---- */

static const uint8_t DOT_MATRIX_FONT[12][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
};

static void draw_vfd_dot(SDL_Renderer *r, int cx, int cy, int radius,
                         full_color_t col, bool on) {
    if (!on) {
        full_color_t dim = {(Uint8)(col.r / 15), (Uint8)(col.g / 15),
                            (Uint8)(col.b / 15), 80};
        full_set_color(r, dim);
        for (int dy = -radius + 1; dy < radius; dy++)
            for (int dx = -radius + 1; dx < radius; dx++)
                if (dx * dx + dy * dy < radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        return;
    }

    int glow_r = radius + 2;
    full_color_t glow1 = {(Uint8)(col.r / 6), (Uint8)(col.g / 6), (Uint8)(col.b / 6), 60};
    full_set_color(r, glow1);
    for (int dy = -glow_r; dy <= glow_r; dy++) {
        for (int dx = -glow_r; dx <= glow_r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= glow_r * glow_r && d2 > radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }

    full_set_color(r, col);
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++)
            if (dx * dx + dy * dy <= radius * radius)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);

    full_color_t hi = {(Uint8)(col.r + (255 - col.r) / 2),
                       (Uint8)(col.g + (255 - col.g) / 2),
                       (Uint8)(col.b + (255 - col.b) / 2), 255};
    full_set_color(r, hi);
    int hi_r = radius / 2;
    if (hi_r < 1) hi_r = 1;
    for (int dy = -hi_r; dy <= hi_r; dy++)
        for (int dx = -hi_r; dx <= hi_r; dx++)
            if (dx * dx + dy * dy <= hi_r * hi_r)
                SDL_RenderDrawPoint(r, cx + dx - 1, cy + dy - 1);
}

static void draw_vfd_char(SDL_Renderer *r, int x, int y, int ch_idx,
                          int dot_size, int dot_gap,
                          full_color_t on_col, full_color_t off_col,
                          bool show_off) {
    if (ch_idx < 0 || ch_idx > 11) return;
    const uint8_t *pattern = DOT_MATRIX_FONT[ch_idx];
    int dot_spacing = dot_size * 2 + dot_gap;
    for (int col = 0; col < 5; col++) {
        uint8_t coldata = pattern[col];
        for (int row = 0; row < 7; row++) {
            bool on = (coldata >> row) & 1;
            int px = x + col * dot_spacing + dot_size;
            int py = y + row * dot_spacing + dot_size;
            if (on)            draw_vfd_dot(r, px, py, dot_size, on_col,  true);
            else if (show_off) draw_vfd_dot(r, px, py, dot_size, off_col, false);
        }
    }
}

static int draw_vfd_number(SDL_Renderer *r, int x, int y, const char *str,
                           int dot_size, int dot_gap, int char_gap,
                           full_color_t on_col, full_color_t off_col, bool show_off) {
    int cx = x;
    int char_w = 5 * (dot_size * 2 + dot_gap);
    for (const char *p = str; *p; p++) {
        int ch_idx = -1;
        if      (*p >= '0' && *p <= '9') ch_idx = *p - '0';
        else if (*p == '.')              ch_idx = 10;
        else if (*p == '-')              ch_idx = 11;
        else if (*p == ' ')              { cx += char_w + char_gap; continue; }

        if (ch_idx >= 0) {
            draw_vfd_char(r, cx, y, ch_idx, dot_size, dot_gap, on_col, off_col, show_off);
            cx += (ch_idx == 10) ? ((dot_size * 2 + dot_gap) * 2 + char_gap)
                                 : (char_w + char_gap);
        }
    }
    return cx - x;
}

/* ---- visualizations ---- */

static void draw_bar_meter(full_ctx_t *c, int x, int y, int w, int h,
                           float value, float max_val, const char *label, const char *unit) {
    SDL_Renderer *r = c->renderer;
    full_set_color(r, (full_color_t){35, 35, 38, 255});
    full_fill_rect(r, x, y, w, h);
    full_set_color(r, COL_BORDER_LIGHT);
    full_draw_rect(r, x, y, w, h);

    int bar_x = x + 8, bar_y = y + 22, bar_w = w - 16, bar_h = 16;
    full_set_color(r, (full_color_t){20, 20, 22, 255});
    full_fill_rect(r, bar_x, bar_y, bar_w, bar_h);

    float frac = value / max_val;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int fill_w = (int)(bar_w * frac);

    for (int i = 0; i < fill_w; i++) {
        float f = (float)i / bar_w;
        Uint8 red, green;
        if (f < 0.5f) { red = (Uint8)(f * 2 * 200); green = 200; }
        else          { red = 200; green = (Uint8)((1.0f - (f - 0.5f) * 2) * 200); }
        SDL_SetRenderDrawColor(r, red, green, 50, 255);
        SDL_RenderDrawLine(r, bar_x + i, bar_y + 1, bar_x + i, bar_y + bar_h - 2);
    }

    full_set_color(r, (full_color_t){80, 80, 85, 255});
    full_draw_rect(r, bar_x, bar_y, bar_w, bar_h);

    full_set_color(r, COL_TEXT_DIM);
    for (int i = 0; i <= 10; i++) {
        int tx = bar_x + bar_w * i / 10;
        int th = (i % 5 == 0) ? 4 : 2;
        SDL_RenderDrawLine(r, tx, bar_y + bar_h, tx, bar_y + bar_h + th);
    }

    full_draw_text(c, c->font_small, label, x + 8, y + 4, COL_LABEL, 0);
    char val_str[24];
    snprintf(val_str, sizeof(val_str), "%.2f %s", value, unit);
    full_draw_text(c, c->font_small, val_str, x + w - 8, y + 4, COL_TEXT, 2);

    char scale_str[8];
    snprintf(scale_str, sizeof(scale_str), "0");
    full_draw_text(c, c->font_small, scale_str, bar_x, bar_y + bar_h + 5, COL_TEXT_DIM, 0);
    snprintf(scale_str, sizeof(scale_str), "%.0f", max_val);
    full_draw_text(c, c->font_small, scale_str, bar_x + bar_w, bar_y + bar_h + 5, COL_TEXT_DIM, 2);
}

static void draw_temp_gauge(full_ctx_t *c, int x, int y, int w, int h,
                            float temp_c, bool warning_blink) {
    SDL_Renderer *r = c->renderer;
    bool is_hot = temp_c >= 50.0f;
    bool blink_on = warning_blink && ((SDL_GetTicks() / 300) % 2 == 0);

    if (is_hot && blink_on) full_set_color(r, (full_color_t){80, 30, 30, 255});
    else                    full_set_color(r, (full_color_t){35, 35, 38, 255});
    full_fill_rect(r, x, y, w, h);

    if (is_hot) full_set_color(r, blink_on ? COL_ERROR : (full_color_t){150, 60, 60, 255});
    else        full_set_color(r, COL_BORDER_LIGHT);
    full_draw_rect(r, x, y, w, h);
    if (is_hot) full_draw_rect(r, x + 1, y + 1, w - 2, h - 2);

    full_color_t title_col = is_hot ? COL_ERROR : COL_LABEL;
    full_draw_text(c, c->font_small, "TEMPERATURE", x + 8, y + 4, title_col, 0);

    int gauge_x = x + 25, gauge_y = y + 22, gauge_w = w - 50, gauge_h = 20;
    full_draw_text(c, c->font_small, "C", x + 10,      gauge_y + 2, (full_color_t){100, 150, 255, 255}, 0);
    full_draw_text(c, c->font_small, "H", x + w - 18,  gauge_y + 2, COL_ERROR, 0);

    full_set_color(r, (full_color_t){20, 20, 22, 255});
    full_fill_rect(r, gauge_x, gauge_y, gauge_w, gauge_h);

    float frac = temp_c / 100.0f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int fill_w = (int)(gauge_w * frac);
    for (int i = 0; i < fill_w; i++) {
        float f = (float)i / gauge_w;
        Uint8 red, green, blue;
        if (f < 0.5f) {
            blue = (Uint8)((1.0f - f * 2) * 200);
            green = (Uint8)(f * 2 * 200);
            red = 50;
        } else {
            blue = 50;
            green = (Uint8)((1.0f - (f - 0.5f) * 2) * 200);
            red = (Uint8)((f - 0.5f) * 2 * 200 + 50);
        }
        SDL_SetRenderDrawColor(r, red, green, blue, 255);
        SDL_RenderDrawLine(r, gauge_x + i, gauge_y + 1, gauge_x + i, gauge_y + gauge_h - 2);
    }

    int warn_x = gauge_x + gauge_w / 2;
    full_set_color(r, (full_color_t){200, 100, 0, 255});
    SDL_RenderDrawLine(r, warn_x, gauge_y, warn_x, gauge_y + gauge_h);

    full_set_color(r, (full_color_t){80, 80, 85, 255});
    full_draw_rect(r, gauge_x, gauge_y, gauge_w, gauge_h);

    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.1f", temp_c);
    full_color_t val_col = is_hot
        ? (blink_on ? (full_color_t){255, 100, 100, 255} : COL_ERROR) : COL_TEXT;
    full_draw_text(c, c->font_medium, temp_str, x + w / 2 - 10, gauge_y + gauge_h + 2, val_col, 0);
    full_draw_text(c, c->font_small, "C", x + w / 2 + 20, gauge_y + gauge_h + 4, val_col, 0);

    if (is_hot && blink_on) {
        full_draw_text_centered(c, c->font_small, "! HOT !", x + w / 2, y + h - 8, COL_ERROR);
    }
}

static void draw_scope(full_ctx_t *c, int x, int y, int w, int h,
                       full_trace_t *trace, const char *label) {
    SDL_Renderer *r = c->renderer;
    full_set_color(r, COL_SCOPE_BG);
    full_fill_rect(r, x, y, w, h);

    int samples = trace->count < FULL_TRACE_LEN ? trace->count : FULL_TRACE_LEN;
    if (samples < 2) {
        full_set_color(r, COL_BORDER);
        full_draw_rect(r, x, y, w, h);
        full_draw_text(c, c->font_small, "NO DATA", x + w / 2 - 25, y + h / 2 - 6, COL_TEXT_DIM, 0);
        return;
    }

    float v_min = 1e9f, v_max = -1e9f;
    float a_min = 1e9f, a_max = -1e9f;
    for (int i = 0; i < samples; i++) {
        int idx = (trace->head - samples + i + FULL_TRACE_LEN) % FULL_TRACE_LEN;
        float v = trace->voltage[idx];
        float a = trace->current[idx];
        if (v < v_min) v_min = v;
        if (v > v_max) v_max = v;
        if (a < a_min) a_min = a;
        if (a > a_max) a_max = a;
    }

    float v_range = v_max - v_min;
    float a_range = a_max - a_min;
    if (v_range < 0.5f) { v_range = 0.5f; v_min -= 0.25f; v_max += 0.25f; }
    if (a_range < 0.1f) { a_range = 0.1f; a_min -= 0.05f; a_max += 0.05f; }
    v_min -= v_range * 0.1f; v_max += v_range * 0.1f;
    a_min -= a_range * 0.1f; a_max += a_range * 0.1f;
    if (v_min < 0)  v_min = 0;
    if (a_min < 0)  a_min = 0;
    if (v_max > 40) v_max = 40;
    if (a_max > 8)  a_max = 8;
    v_range = v_max - v_min;
    a_range = a_max - a_min;

    full_set_color(r, COL_SCOPE_GRID);
    for (int i = 1; i < 4; i++) {
        int gy = y + h * i / 4;
        SDL_RenderDrawLine(r, x, gy, x + w, gy);
    }
    for (int i = 1; i < 10; i++) {
        int gx = x + w * i / 10;
        SDL_RenderDrawLine(r, gx, y, gx, y + h);
    }
    SDL_SetRenderDrawColor(r, 40, 60, 45, 255);
    SDL_RenderDrawLine(r, x, y + h / 2, x + w, y + h / 2);

    int plot_x = x + 2, plot_w = w - 4;
    int plot_y = y + 2, plot_h = h - 4;

    full_color_t col_current = {255, 180, 50, 255};
    full_set_color(r, col_current);
    int prev_px = -1, prev_py = -1;
    for (int i = 0; i < samples; i++) {
        int idx = (trace->head - samples + i + FULL_TRACE_LEN) % FULL_TRACE_LEN;
        float a = trace->current[idx];
        int px = plot_x + i * plot_w / FULL_TRACE_LEN;
        int py = plot_y + plot_h - (int)((a - a_min) / a_range * plot_h);
        if (py < plot_y) py = plot_y;
        if (py > plot_y + plot_h - 1) py = plot_y + plot_h - 1;
        if (prev_px >= 0) SDL_RenderDrawLine(r, prev_px, prev_py, px, py);
        prev_px = px; prev_py = py;
    }

    full_set_color(r, COL_SCOPE_LINE);
    prev_px = -1; prev_py = -1;
    for (int i = 0; i < samples; i++) {
        int idx = (trace->head - samples + i + FULL_TRACE_LEN) % FULL_TRACE_LEN;
        float v = trace->voltage[idx];
        int px = plot_x + i * plot_w / FULL_TRACE_LEN;
        int py = plot_y + plot_h - (int)((v - v_min) / v_range * plot_h);
        if (py < plot_y) py = plot_y;
        if (py > plot_y + plot_h - 1) py = plot_y + plot_h - 1;
        if (prev_px >= 0) SDL_RenderDrawLine(r, prev_px, prev_py, px, py);
        prev_px = px; prev_py = py;
    }

    full_set_color(r, COL_BORDER);
    full_draw_rect(r, x, y, w, h);

    char scale_str[16];
    snprintf(scale_str, sizeof(scale_str), "%.1fV", v_max);
    full_draw_text(c, c->font_small, scale_str, x + 3, y + 2, COL_SCOPE_LINE, 0);
    snprintf(scale_str, sizeof(scale_str), "%.1fV", v_min);
    full_draw_text(c, c->font_small, scale_str, x + 3, y + h - 14, COL_SCOPE_LINE, 0);
    snprintf(scale_str, sizeof(scale_str), "%.2fA", a_max);
    full_draw_text(c, c->font_small, scale_str, x + w - 5, y + 2, col_current, 2);
    snprintf(scale_str, sizeof(scale_str), "%.2fA", a_min);
    full_draw_text(c, c->font_small, scale_str, x + w - 5, y + h - 14, col_current, 2);
    full_draw_text(c, c->font_small, label, x + w / 2, y + h - 14, COL_TEXT_DIM, 1);

    full_set_color(r, COL_SCOPE_LINE);
    SDL_RenderDrawLine(r, x + 5, y + h - 26, x + 20, y + h - 26);
    full_draw_text(c, c->font_small, "V", x + 23, y + h - 30, COL_SCOPE_LINE, 0);
    full_set_color(r, col_current);
    SDL_RenderDrawLine(r, x + 35, y + h - 26, x + 50, y + h - 26);
    full_draw_text(c, c->font_small, "A", x + 53, y + h - 30, col_current, 0);
}

static void draw_vfd_display(full_ctx_t *c, int x, int y, int w, int h,
                             float voltage, float current, float power,
                             bool output_on) {
    SDL_Renderer *r = c->renderer;
    full_set_color(r, COL_VFD_BG);
    full_fill_rect(r, x, y, w, h);
    SDL_SetRenderDrawColor(r, 1, 6, 3, 255);
    full_fill_rect(r, x + 3, y + 3, w - 6, h - 6);

    SDL_SetRenderDrawColor(r, 0, 10, 5, 30);
    for (int ly = y + 5; ly < y + h - 5; ly += 2)
        SDL_RenderDrawLine(r, x + 5, ly, x + w - 5, ly);

    SDL_SetRenderDrawColor(r, 30, 40, 35, 255); full_draw_rect(r, x + 2, y + 2, w - 4, h - 4);
    SDL_SetRenderDrawColor(r, 20, 30, 25, 255); full_draw_rect(r, x + 1, y + 1, w - 2, h - 2);
    full_set_color(r, COL_BORDER);              full_draw_rect(r, x, y, w, h);

    full_color_t on_col    = output_on ? COL_VFD_ON : COL_VFD_OFF;
    full_color_t off_col   = {0, 20, 12, 255};
    full_color_t label_col = {200, 160, 60, 255};
    full_color_t unit_col  = output_on ? (full_color_t){0, 220, 110, 255} : (full_color_t){0, 45, 25, 255};

    int header_y = y + 8;
    int led_x = x + 15, led_y = header_y + 8, led_r = 5;
    if (output_on) {
        SDL_SetRenderDrawColor(r, 0, 255, 120, 30);
        for (int dy = -led_r - 4; dy <= led_r + 4; dy++)
            for (int dx = -led_r - 4; dx <= led_r + 4; dx++) {
                int dist2 = dx * dx + dy * dy;
                if (dist2 <= (led_r + 4) * (led_r + 4) && dist2 > led_r * led_r)
                    SDL_RenderDrawPoint(r, led_x + dx, led_y + dy);
            }
        full_set_color(r, COL_VFD_ON);
        for (int dy = -led_r; dy <= led_r; dy++)
            for (int dx = -led_r; dx <= led_r; dx++)
                if (dx * dx + dy * dy <= led_r * led_r)
                    SDL_RenderDrawPoint(r, led_x + dx, led_y + dy);
        SDL_SetRenderDrawColor(r, 150, 255, 200, 255);
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx * dx + dy * dy <= 4)
                    SDL_RenderDrawPoint(r, led_x + dx, led_y + dy);
    } else {
        full_set_color(r, (full_color_t){0, 25, 15, 255});
        for (int dy = -led_r; dy <= led_r; dy++)
            for (int dx = -led_r; dx <= led_r; dx++)
                if (dx * dx + dy * dy <= led_r * led_r)
                    SDL_RenderDrawPoint(r, led_x + dx, led_y + dy);
    }

    full_color_t out_text_col = output_on ? COL_VFD_ON : (full_color_t){0, 40, 25, 255};
    full_draw_text(c, c->font_medium, "OUTPUT", x + 28, header_y, out_text_col, 0);
    const char *status_str = output_on ? "ON" : "OFF";
    full_color_t status_col = output_on ? (full_color_t){0, 255, 120, 255}
                                        : (full_color_t){80, 0, 0, 255};
    full_draw_text(c, c->font_medium, status_str, x + 95, header_y, status_col, 0);

    SDL_SetRenderDrawColor(r, 0, 50, 30, 255);
    SDL_RenderDrawLine(r, x + 8, header_y + 22, x + w - 8, header_y + 22);

    int content_y = header_y + 28;
    int content_h = h - (content_y - y) - 8;
    int row_h = content_h / 3;
    int label_x = x + 10;

    int dot_size = 2, dot_gap = 1, char_gap = 5;
    int dot_spacing = dot_size * 2 + dot_gap;
    int char_h = 7 * dot_spacing;
    int num_x = x + 95;

    /* 6 decimals → µV / µA / µW resolution. Format width 9 ("XX.XXXXXX")
     * is space-padded so values stay right-aligned as magnitude changes. */
    char buf[24];
    int row1_y = content_y + (row_h - char_h) / 2;
    full_draw_text(c, c->font_medium, "VOLTAGE", label_x, row1_y + char_h / 2 - 6, label_col, 0);
    snprintf(buf, sizeof(buf), "%9.6f", voltage);
    int vw = draw_vfd_number(r, num_x, row1_y, buf, dot_size, dot_gap, char_gap, on_col, off_col, true);
    full_draw_text(c, c->font_large, "V", num_x + vw + 8, row1_y + char_h / 2 - 8, unit_col, 0);

    int row2_y = content_y + row_h + (row_h - char_h) / 2;
    full_draw_text(c, c->font_medium, "CURRENT", label_x, row2_y + char_h / 2 - 6, label_col, 0);
    snprintf(buf, sizeof(buf), "%9.6f", current);
    int aw = draw_vfd_number(r, num_x, row2_y, buf, dot_size, dot_gap, char_gap, on_col, off_col, true);
    full_draw_text(c, c->font_large, "A", num_x + aw + 8, row2_y + char_h / 2 - 8, unit_col, 0);

    int row3_y = content_y + 2 * row_h + (row_h - char_h) / 2;
    full_draw_text(c, c->font_medium, "POWER", label_x, row3_y + char_h / 2 - 6, label_col, 0);
    snprintf(buf, sizeof(buf), "%10.6f", power);
    int pw = draw_vfd_number(r, num_x, row3_y, buf, dot_size, dot_gap, char_gap, on_col, off_col, true);
    full_draw_text(c, c->font_large, "W", num_x + pw + 8, row3_y + char_h / 2 - 8, unit_col, 0);
}

static void draw_input_field(full_ctx_t *c, int x, int y, int w, int h,
                             const char *value, const char *label,
                             bool active, int input_idx) {
    SDL_Renderer *r = c->renderer;
    full_draw_text(c, c->font_small, label, x, y - 16, COL_LABEL, 0);
    full_set_color(r, COL_INPUT_BG);
    full_fill_rect(r, x, y, w, h);
    full_set_color(r, active ? COL_INPUT_FOCUS : COL_INPUT_BORDER);
    full_draw_rect(r, x, y, w, h);
    if (active) full_draw_rect(r, x + 1, y + 1, w - 2, h - 2);
    full_draw_text(c, c->font_medium, value, x + 8, y + (h - 14) / 2, COL_TEXT, 0);
    if (input_idx >= 0 && input_idx < 4)
        c->inputs[input_idx] = (SDL_Rect){x, y, w, h};
}

void full_draw_channel_panel(full_ctx_t *c, int ch_idx, int x, int y,
                             int panel_w, int panel_h) {
    SDL_Renderer *r = c->renderer;
    full_channel_t      *ch = &c->ch[ch_idx];
    psu_channel_state_t *st = &ch->state;

    full_set_color(r, COL_BG_PANEL);
    full_fill_rounded(r, x, y, panel_w, panel_h, 4);
    full_set_color(r, COL_BORDER);
    full_draw_rect(r, x, y, panel_w, panel_h);

    full_set_color(r, COL_BG_WIDGET);
    full_fill_rect(r, x + 1, y + 1, panel_w - 2, 32);

    char title[32];
    snprintf(title, sizeof(title), "OUTPUT %d", ch_idx + 1);
    full_draw_text(c, c->font_medium, title, x + 12, y + 8, COL_TEXT, 0);

    /* Header range label: show this channel's driver-reported limits. */
    char limits[32];
    snprintf(limits, sizeof(limits), "%.0fV / %.0fA",
             c->drv->caps.v_max, c->drv->caps.i_max);
    full_draw_text(c, c->font_small, limits, x + panel_w - 80, y + 10, COL_TEXT_DIM, 0);

    int vfd_x = x + 10, vfd_y = y + 40, vfd_w = panel_w - 20, vfd_h = 180;
    draw_vfd_display(c, vfd_x, vfd_y, vfd_w, vfd_h,
                     ch->disp_v, ch->disp_a, ch->disp_p, st->out_on);

    int ctrl_y = vfd_y + vfd_h + 15;
    int out_btn_id = (ch_idx == 0) ? FULL_BTN_CH1_OUTPUT : FULL_BTN_CH2_OUTPUT;
    full_draw_button(c, x + 15, ctrl_y, 90, 30, "OUTPUT",
                     st->out_on, c->hover_btn == out_btn_id, out_btn_id);
    full_draw_led(r, x + 120, ctrl_y + 15, 6, st->out_on, COL_SUCCESS);

    full_color_t status_col = st->valid ? COL_SUCCESS : COL_ERROR;
    full_draw_text(c, c->font_small, "STATUS:", x + 145, ctrl_y + 8, COL_LABEL, 0);
    full_draw_text(c, c->font_small, st->valid ? "OK" : "ERR",
                   x + 200, ctrl_y + 8, status_col, 0);

    int set_y = ctrl_y + 50;
    char v_str[24], a_str[24];
    snprintf(v_str, sizeof(v_str), "%.6f", st->set_v);
    snprintf(a_str, sizeof(a_str), "%.6f", st->set_a);

    int v_idx = ch_idx * 2, a_idx = ch_idx * 2 + 1;
    bool v_active = (c->active_input == v_idx + 1);
    bool a_active = (c->active_input == a_idx + 1);

    draw_input_field(c, x + 15,  set_y, 100, 28,
                     v_active ? c->input_buf : v_str, "SET VOLTAGE (V)", v_active, v_idx);
    int set_v_btn = (ch_idx == 0) ? FULL_BTN_CH1_SET_V : FULL_BTN_CH2_SET_V;
    full_draw_button(c, x + 120, set_y, 50, 28, "SET",
                     false, c->hover_btn == set_v_btn, set_v_btn);

    draw_input_field(c, x + 200, set_y, 100, 28,
                     a_active ? c->input_buf : a_str, "SET CURRENT (A)", a_active, a_idx);
    int set_a_btn = (ch_idx == 0) ? FULL_BTN_CH1_SET_A : FULL_BTN_CH2_SET_A;
    full_draw_button(c, x + 305, set_y, 50, 28, "SET",
                     false, c->hover_btn == set_a_btn, set_a_btn);

    /* CV/CC indicator. psu_channel_state_t exposes cv_mode (true = CV). */
    const char *mode_str = st->cv_mode ? "CV" : "CC";
    full_color_t mode_col = st->cv_mode ? COL_SUCCESS : COL_WARNING;
    full_draw_text(c, c->font_small, "MODE:", x + 380, ctrl_y + 8, COL_LABEL, 0);
    full_draw_text(c, c->font_small, mode_str, x + 420, ctrl_y + 8, mode_col, 0);

    int meter_y = set_y + 50;
    int meter_w = (panel_w - 45) / 2;
    int meter_h = 55;
    draw_bar_meter(c, x + 15,            meter_y, meter_w, meter_h,
                   st->out_v, c->drv->caps.v_max, "VOLTAGE", "V");
    draw_bar_meter(c, x + 25 + meter_w,  meter_y, meter_w, meter_h,
                   st->out_a, c->drv->caps.i_max, "CURRENT", "A");

    int temp_y = meter_y + meter_h + 10;
    draw_temp_gauge(c, x + 15, temp_y, panel_w - 30, 70, st->temp_c, true);

    int scope_y = temp_y + 80;
    char scope_label[16];
    snprintf(scope_label, sizeof(scope_label), "CH%d", ch_idx + 1);
    draw_scope(c, x + 15, scope_y, panel_w - 30, 90, &ch->trace, scope_label);
}

void full_draw_keypad_btn(full_ctx_t *c, int x, int y, int w, int h,
                          const char *label, bool highlight, int id) {
    bool hover = (c->hover_btn == id);
    full_color_t bg     = highlight ? COL_ACCENT
                                    : (hover ? COL_BTN_HOVER : (full_color_t){50, 50, 55, 255});
    full_color_t border = highlight ? COL_ACCENT : COL_BORDER_LIGHT;
    full_color_t text_col = highlight ? (full_color_t){255, 255, 255, 255} : COL_TEXT;

    full_set_color(c->renderer, bg);
    full_fill_rounded(c->renderer, x, y, w, h, 4);
    full_set_color(c->renderer, border);
    full_draw_rect(c->renderer, x, y, w, h);
    full_draw_text_centered(c, c->font_medium, label, x + w / 2, y + h / 2, text_col);
    full_add_button(c, x, y, w, h, id);
}

/* ---- state plumbing ---- */

static void update_display_values(full_ctx_t *c, int ch_idx) {
    full_channel_t      *ch = &c->ch[ch_idx];
    psu_channel_state_t *st = &ch->state;

    float new_v, new_a, new_p;
    if (st->out_on) {
        new_v = st->out_v;
        new_a = st->out_a;
        new_p = st->out_p;
    } else {
        new_v = st->set_v;
        new_a = st->set_a;
        new_p = 0.0f;
    }
    if (!st->out_on || (new_v > 0.01f || new_a > 0.001f)) {
        ch->disp_v = new_v;
        ch->disp_a = new_a;
        ch->disp_p = new_p;
    }
}

void full_update_from_driver(full_ctx_t *c) {
    int n = c->drv->caps.n_channels;
    if (n > 2) n = 2;
    for (int i = 0; i < n; i++) {
        psu_channel_state_t st;
        c->drv->get_channel(c->drv, i + 1, &st);
        if (st.valid) {
            c->ch[i].state = st;
            update_display_values(c, i);
            if (st.out_v > 0 || st.out_a > 0) {
                full_trace_t *tr = &c->ch[i].trace;
                tr->voltage[tr->head] = st.out_v;
                tr->current[tr->head] = st.out_a;
                tr->head = (tr->head + 1) % FULL_TRACE_LEN;
                if (tr->count < FULL_TRACE_LEN) tr->count++;
            }
        }
    }
}

void full_tick_fps(full_ctx_t *c) {
    c->frame_count++;
    uint32_t now = SDL_GetTicks();
    if (now - c->last_fps_time >= 1000) {
        c->fps = c->frame_count;
        c->frame_count = 0;
        c->last_fps_time = now;
    }
}

/* ---- input handling ---- */

void full_keypad_append(full_ctx_t *c, char ch) {
    size_t len = strlen(c->keypad_value);
    /* Cap total length so "100.123456" still fits comfortably. */
    if (len + 1 >= sizeof(c->keypad_value) || len >= 12) return;
    if (ch == '.' && strchr(c->keypad_value, '.') != NULL) return;
    char *dot = strchr(c->keypad_value, '.');
    if (dot) {
        int dec = (int)len - (int)(dot - c->keypad_value) - 1;
        /* µV / µA = 6 decimal places. */
        if (dec >= 6) return;
    }
    c->keypad_value[len]     = ch;
    c->keypad_value[len + 1] = '\0';
}

void full_keypad_apply(full_ctx_t *c) {
    if (strlen(c->keypad_value) == 0) return;
    float val = (float)atof(c->keypad_value);
    int   ch  = c->keypad_channel + 1;

    if (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) {
        if (val >= 0 && val <= c->drv->caps.v_max)
            c->drv->set_voltage(c->drv, ch, val);
    } else {
        if (val >= 0 && val <= c->drv->caps.i_max)
            c->drv->set_current(c->drv, ch, val);
    }
    c->keypad_value[0] = '\0';
}

bool full_try_click_input_field(full_ctx_t *c, int mx, int my) {
    for (int i = 0; i < 4; i++) {
        if (full_point_in_rect(mx, my, &c->inputs[i])) {
            c->active_input = i + 1;
            int ch = i / 2;
            float val = (i % 2 == 0) ? c->ch[ch].state.set_v : c->ch[ch].state.set_a;
            snprintf(c->input_buf, sizeof(c->input_buf), "%.6f", val);
            return true;
        }
    }
    return false;
}

bool full_handle_panel_button(full_ctx_t *c, int btn) {
    switch (btn) {
        case FULL_BTN_TRACKING:
            c->tracking = !c->tracking;
            if (c->drv->set_tracking)
                c->drv->set_tracking(c->drv, c->tracking);
            return true;
        case FULL_BTN_REFRESH:
            return true;
        case FULL_BTN_CH1_OUTPUT:
        case FULL_BTN_CH2_OUTPUT: {
            int ch = (btn == FULL_BTN_CH1_OUTPUT) ? 1 : 2;
            bool on = !c->ch[ch - 1].state.out_on;
            c->drv->set_output(c->drv, ch, on);
            return true;
        }
        case FULL_BTN_CH1_SET_V:
        case FULL_BTN_CH2_SET_V: {
            int ch = (btn == FULL_BTN_CH1_SET_V) ? 1 : 2;
            int exp = (ch - 1) * 2 + 1;
            if (c->active_input == exp) {
                float v = (float)atof(c->input_buf);
                if (v >= 0 && v <= c->drv->caps.v_max)
                    c->drv->set_voltage(c->drv, ch, v);
                c->active_input = 0;
            }
            return true;
        }
        case FULL_BTN_CH1_SET_A:
        case FULL_BTN_CH2_SET_A: {
            int ch = (btn == FULL_BTN_CH1_SET_A) ? 1 : 2;
            int exp = (ch - 1) * 2 + 2;
            if (c->active_input == exp) {
                float a = (float)atof(c->input_buf);
                if (a >= 0 && a <= c->drv->caps.i_max)
                    c->drv->set_current(c->drv, ch, a);
                c->active_input = 0;
            }
            return true;
        }
    }
    return false;
}

bool full_handle_keypad_button(full_ctx_t *c, int btn) {
    switch (btn) {
        case FULL_BTN_KEY_0: full_keypad_append(c, '0'); return true;
        case FULL_BTN_KEY_1: full_keypad_append(c, '1'); return true;
        case FULL_BTN_KEY_2: full_keypad_append(c, '2'); return true;
        case FULL_BTN_KEY_3: full_keypad_append(c, '3'); return true;
        case FULL_BTN_KEY_4: full_keypad_append(c, '4'); return true;
        case FULL_BTN_KEY_5: full_keypad_append(c, '5'); return true;
        case FULL_BTN_KEY_6: full_keypad_append(c, '6'); return true;
        case FULL_BTN_KEY_7: full_keypad_append(c, '7'); return true;
        case FULL_BTN_KEY_8: full_keypad_append(c, '8'); return true;
        case FULL_BTN_KEY_9: full_keypad_append(c, '9'); return true;
        case FULL_BTN_KEY_DOT: full_keypad_append(c, '.'); return true;
        case FULL_BTN_KEY_CLR: c->keypad_value[0] = '\0'; return true;
        case FULL_BTN_KEY_BACK: {
            size_t len = strlen(c->keypad_value);
            if (len > 0) c->keypad_value[len - 1] = '\0';
            return true;
        }
        case FULL_BTN_KEY_ENTER:   full_keypad_apply(c); return true;
        case FULL_BTN_KEY_CH_TOG:  c->keypad_channel = !c->keypad_channel; return true;
        case FULL_BTN_KEY_MODE:
            c->keypad_mode = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE)
                             ? FULL_KEYPAD_MODE_CURRENT : FULL_KEYPAD_MODE_VOLTAGE;
            c->keypad_value[0] = '\0';
            return true;
    }
    return false;
}

void full_handle_key(full_ctx_t *c, SDL_Keycode key) {
    /* Shortcuts that work in any state. */
    if (key == SDLK_TAB) { c->keypad_channel = !c->keypad_channel; return; }
    if (key == SDLK_v) {
        if (c->keypad_mode != FULL_KEYPAD_MODE_VOLTAGE) {
            c->keypad_mode = FULL_KEYPAD_MODE_VOLTAGE;
            c->keypad_value[0] = '\0';
        }
        return;
    }
    if (key == SDLK_a) {
        if (c->keypad_mode != FULL_KEYPAD_MODE_CURRENT) {
            c->keypad_mode = FULL_KEYPAD_MODE_CURRENT;
            c->keypad_value[0] = '\0';
        }
        return;
    }

    if (c->active_input == 0) {
        size_t klen = strlen(c->keypad_value);
        if (key == SDLK_BACKSPACE && klen > 0) {
            c->keypad_value[klen - 1] = '\0';
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { full_keypad_apply(c); return; }
        if (key == SDLK_ESCAPE || key == SDLK_c)        { c->keypad_value[0] = '\0'; return; }
        return;
    }

    size_t len = strlen(c->input_buf);
    if (key == SDLK_BACKSPACE && len > 0) {
        c->input_buf[len - 1] = '\0';
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        int btn = 0;
        switch (c->active_input) {
            case 1: btn = FULL_BTN_CH1_SET_V; break;
            case 2: btn = FULL_BTN_CH1_SET_A; break;
            case 3: btn = FULL_BTN_CH2_SET_V; break;
            case 4: btn = FULL_BTN_CH2_SET_A; break;
        }
        if (btn) full_handle_panel_button(c, btn);
    } else if (key == SDLK_ESCAPE) {
        c->active_input = 0;
    }
}

void full_handle_text(full_ctx_t *c, const char *text) {
    if (c->active_input == 0) {
        for (const char *p = text; *p; p++)
            if ((*p >= '0' && *p <= '9') || *p == '.')
                full_keypad_append(c, *p);
        return;
    }
    for (const char *p = text; *p; p++) {
        if ((*p >= '0' && *p <= '9') || *p == '.') {
            size_t len = strlen(c->input_buf);
            if (len < sizeof(c->input_buf) - 1) {
                c->input_buf[len]     = *p;
                c->input_buf[len + 1] = '\0';
            }
        }
    }
}

/* ---- lifecycle ---- */

bool full_open_fonts(full_ctx_t *c) {
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        NULL
    };
    const char *path = NULL;
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) { fclose(f); path = paths[i]; break; }
    }
    if (!path) return false;

    c->font_title     = TTF_OpenFont(path, 20);
    c->font_large     = TTF_OpenFont(path, 18);
    c->font_medium    = TTF_OpenFont(path, 13);
    c->font_small     = TTF_OpenFont(path, 11);
    c->font_vfd       = TTF_OpenFont(path, 26);
    c->font_vfd_small = TTF_OpenFont(path, 16);
    return c->font_title && c->font_large && c->font_medium &&
           c->font_small && c->font_vfd && c->font_vfd_small;
}

void full_close_fonts(full_ctx_t *c) {
    if (c->font_title)     TTF_CloseFont(c->font_title);
    if (c->font_large)     TTF_CloseFont(c->font_large);
    if (c->font_medium)    TTF_CloseFont(c->font_medium);
    if (c->font_small)     TTF_CloseFont(c->font_small);
    if (c->font_vfd)       TTF_CloseFont(c->font_vfd);
    if (c->font_vfd_small) TTF_CloseFont(c->font_vfd_small);
}
