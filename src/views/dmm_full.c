/**
 * DMM full view — big primary readout + mode/range/rate controls + recent
 * trace + simple statistics.
 *
 * Visual style follows the PSU full GUI (dark panels, accent blue, big
 * monochrome readout) so a side-by-side PSU + DMM workflow looks coherent.
 *
 * Self-contained — does not include full_common.h to avoid coupling DMM
 * views to PSU-shaped helpers. Inlines its own primitives.
 */

#include "views.h"
#include "vfd_dotmatrix.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W       900
#define WIN_H       620
#define HEADER_H     40
#define TOOLBAR_H    36
#define MAX_BTNS    64
#define TRACE_LEN  240

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG       = {30, 30, 32, 255};
static const Color COL_PANEL    = {42, 42, 46, 255};
static const Color COL_BG_WIDGET= {28, 28, 30, 255};
static const Color COL_HEADER   = {24, 24, 26, 255};
static const Color COL_BORDER   = {60, 60, 65, 255};
static const Color COL_BORDER_L = {80, 80, 88, 255};
static const Color COL_TEXT     = {200, 200, 205, 255};
static const Color COL_DIM      = {120, 120, 128, 255};
static const Color COL_LABEL    = {160, 160, 168, 255};
static const Color COL_ACCENT   = {0, 180, 220, 255};
static const Color COL_OK       = {50, 205, 100, 255};
static const Color COL_ERR      = {220, 60, 60, 255};
static const Color COL_VAL      = {0, 255, 140, 255};
static const Color COL_UNIT     = {120, 220, 255, 255};
static const Color COL_BTN      = {55, 55, 60, 255};
static const Color COL_BTN_HOV  = {72, 72, 78, 255};
static const Color COL_BTN_ON   = {0, 150, 180, 255};
static const Color COL_SCOPE_BG = {10, 20, 15, 255};
static const Color COL_SCOPE_LN = {80, 255, 120, 255};
static const Color COL_SCOPE_GR = {30, 50, 35, 255};

enum {
    BTN_NONE = 0,
    BTN_MODE_BASE = 100,        /* + dmm_mode_t */
    BTN_RANGE_AUTO = 200,
    BTN_RATE_SLOW  = 300,
    BTN_RATE_MED   = 301,
    BTN_RATE_FAST  = 302,
    BTN_RESET_STATS = 400,
};

typedef struct { SDL_Rect rect; int id; } btn_t;

typedef struct {
    dmm_driver_t *drv;

    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font_title;
    TTF_Font     *font_label;
    TTF_Font     *font_medium;
    TTF_Font     *font_big;
    TTF_Font     *font_huge;     /* primary readout */

    dmm_reading_t reading;
    bool          running;
    int           hover;

    /* Statistics over recorded samples. */
    float    stat_min, stat_max, stat_sum;
    uint32_t stat_n;
    dmm_mode_t stat_mode;   /* reset when mode changes */

    /* Recent-value ring buffer for the scope. */
    float    trace[TRACE_LEN];
    int      trace_head;
    int      trace_count;

    btn_t buttons[MAX_BTNS];
    int   num_buttons;
} app_t;

/* ---- primitives ---- */

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderFillRect(r, &rc);
}
static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderDrawRect(r, &rc);
}
static void fill_rounded(SDL_Renderer *r, int x, int y, int w, int h, int radius) {
    fill_rect(r, x + radius, y, w - 2 * radius, h);
    fill_rect(r, x, y + radius, w, h - 2 * radius);
    for (int corner = 0; corner < 4; corner++) {
        int cx = (corner % 2 == 0) ? x + radius : x + w - radius;
        int cy = (corner < 2)     ? y + radius : y + h - radius;
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++)
                if (dx * dx + dy * dy <= radius * radius)
                    SDL_RenderDrawPoint(r, cx + dx, cy + dy);
    }
}
static int draw_text(SDL_Renderer *r, TTF_Font *f, const char *t, int x, int y, Color col) {
    if (!f || !t || !*t) return 0;
    SDL_Color c = {col.r, col.g, col.b, col.a};
    SDL_Surface *s = TTF_RenderUTF8_Blended(f, t, c);
    if (!s) return 0;
    SDL_Texture *tx = SDL_CreateTextureFromSurface(r, s);
    int w = s->w;
    if (tx) { SDL_Rect d = {x, y, s->w, s->h}; SDL_RenderCopy(r, tx, NULL, &d); SDL_DestroyTexture(tx); }
    SDL_FreeSurface(s);
    return w;
}
static int add_btn(app_t *a, int x, int y, int w, int h, int id) {
    if (a->num_buttons >= MAX_BTNS) return -1;
    a->buttons[a->num_buttons] = (btn_t){ .rect = {x, y, w, h}, .id = id };
    return a->num_buttons++;
}
static int btn_at(app_t *a, int mx, int my) {
    for (int i = 0; i < a->num_buttons; i++) {
        SDL_Rect *r = &a->buttons[i].rect;
        if (mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h)
            return a->buttons[i].id;
    }
    return BTN_NONE;
}

static void draw_btn(app_t *a, int x, int y, int w, int h, const char *label,
                     bool active, int id, bool enabled) {
    bool hov = enabled && (a->hover == id);
    Color bg = !enabled ? (Color){50, 50, 55, 255}
             : active   ? COL_BTN_ON
             : hov      ? COL_BTN_HOV : COL_BTN;
    set_color(a->renderer, bg);
    fill_rounded(a->renderer, x, y, w, h, 3);
    set_color(a->renderer, active ? COL_ACCENT : COL_BORDER_L);
    draw_rect(a->renderer, x, y, w, h);
    int tw = 0, th = 0;
    TTF_SizeText(a->font_label, label, &tw, &th);
    Color fg = !enabled ? (Color){90, 90, 95, 255}
             : active   ? (Color){255, 255, 255, 255} : COL_TEXT;
    draw_text(a->renderer, a->font_label, label, x + (w - tw) / 2, y + (h - th) / 2, fg);
    if (enabled) add_btn(a, x, y, w, h, id);
}

/* ---- statistics ---- */

static void stats_reset(app_t *a) {
    a->stat_min = INFINITY;
    a->stat_max = -INFINITY;
    a->stat_sum = 0.0f;
    a->stat_n   = 0;
    a->trace_head = 0;
    a->trace_count = 0;
}

static void stats_update(app_t *a, dmm_mode_t mode, float value, bool valid) {
    if (!valid || a->reading.overload) return;
    if (a->stat_mode != mode || a->stat_n == 0) {
        a->stat_mode = mode;
        stats_reset(a);
        a->stat_mode = mode;
    }
    if (value < a->stat_min) a->stat_min = value;
    if (value > a->stat_max) a->stat_max = value;
    a->stat_sum += value;
    a->stat_n++;
    a->trace[a->trace_head] = value;
    a->trace_head = (a->trace_head + 1) % TRACE_LEN;
    if (a->trace_count < TRACE_LEN) a->trace_count++;
}

/* ---- engineering-prefix formatter ---- */

static float eng_scale(float value, const char **prefix) {
    static const struct { float scale; const char *p; } table[] = {
        {1e12f, "T"}, {1e9f, "G"}, {1e6f, "M"}, {1e3f, "k"},
        {1.0f,  ""},  {1e-3f, "m"}, {1e-6f, "µ"}, {1e-9f, "n"}, {1e-12f, "p"},
    };
    if (value == 0.0f) { *prefix = ""; return 0.0f; }
    float abs_v = fabsf(value);
    int idx = (int)(sizeof(table) / sizeof(table[0])) - 1;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (abs_v >= table[i].scale) { idx = (int)i; break; }
    }
    *prefix = table[idx].p;
    return value / table[idx].scale;
}

/* ---- panel drawing ---- */

static void draw_header(app_t *a) {
    SDL_Renderer *r = a->renderer;
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, WIN_W, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, WIN_W, HEADER_H - 1);

    draw_text(r, a->font_title, "DIGITAL MULTIMETER", 20, 10, COL_TEXT);
    draw_text(r, a->font_label, a->drv->caps.model_name, 260, 14, COL_DIM);

    bool ok = a->drv->is_connected(a->drv);
    Color status_col = ok ? COL_OK : COL_ERR;
    const char *status_txt = ok ? "ONLINE" : "OFFLINE";
    set_color(r, status_col);
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++)
            if (dx * dx + dy * dy <= 25) SDL_RenderDrawPoint(r, WIN_W - 100 + dx, 21 + dy);
    draw_text(r, a->font_medium, status_txt, WIN_W - 88, 14, status_col);
}

/* Replicates the PSU VFD panel treatment: dark green-black background,
 * inner bezel, subtle scan lines, triple-line border. Same dot-matrix
 * dimensions (dot_size=2, char_gap=5) so DMM and PSU readouts match size +
 * look side by side. */
static void draw_readout_panel(app_t *a, int x, int y, int w, int h) {
    SDL_Renderer *r = a->renderer;
    set_color(r, COL_PANEL);
    fill_rounded(r, x, y, w, h, 4);
    set_color(r, COL_BORDER);
    draw_rect(r, x, y, w, h);

    /* Inner "VFD" region with PSU-style decoration. */
    int ix = x + 12, iy = y + 12, iw = w - 24, ih = h - 24;
    set_color(r, (Color){8, 18, 12, 255});
    fill_rect(r, ix, iy, iw, ih);
    SDL_SetRenderDrawColor(r, 1, 6, 3, 255);
    fill_rect(r, ix + 3, iy + 3, iw - 6, ih - 6);

    /* Faint scan lines. */
    SDL_SetRenderDrawColor(r, 0, 10, 5, 30);
    for (int ly = iy + 5; ly < iy + ih - 5; ly += 2)
        SDL_RenderDrawLine(r, ix + 5, ly, ix + iw - 5, ly);

    /* Triple-line border for depth. */
    SDL_SetRenderDrawColor(r, 30, 40, 35, 255); draw_rect(r, ix + 2, iy + 2, iw - 4, ih - 4);
    SDL_SetRenderDrawColor(r, 20, 30, 25, 255); draw_rect(r, ix + 1, iy + 1, iw - 2, ih - 2);
    set_color(r, COL_BORDER);                   draw_rect(r, ix, iy, iw, ih);

    /* Header strip — LED + mode label (amber, PSU style). */
    int header_y = iy + 8;
    int led_x = ix + 15, led_y = header_y + 8, led_r = 5;
    Color led_col = a->reading.valid ? COL_OK : COL_ERR;
    SDL_SetRenderDrawColor(r, led_col.r, led_col.g, led_col.b, 30);
    for (int dy = -led_r - 4; dy <= led_r + 4; dy++)
        for (int dx = -led_r - 4; dx <= led_r + 4; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= (led_r + 4) * (led_r + 4) && d2 > led_r * led_r)
                SDL_RenderDrawPoint(r, led_x + dx, led_y + dy);
        }
    set_color(r, led_col);
    for (int dy = -led_r; dy <= led_r; dy++)
        for (int dx = -led_r; dx <= led_r; dx++)
            if (dx * dx + dy * dy <= led_r * led_r)
                SDL_RenderDrawPoint(r, led_x + dx, led_y + dy);

    Color label_col = {200, 160, 60, 255};
    draw_text(r, a->font_medium, dmm_mode_label(a->reading.mode),
              ix + 30, header_y, label_col);

    /* Range / rate / OL badges on the right edge of the header strip. */
    int bx = ix + iw - 12;
    if (a->reading.overload) {
        const char *lbl = "OL";
        int tw = 0, th = 0;
        TTF_SizeText(a->font_label, lbl, &tw, &th);
        int bw = tw + 14;
        set_color(r, (Color){180, 30, 30, 255});
        fill_rounded(r, bx - bw, header_y - 2, bw, 20, 3);
        draw_text(r, a->font_label, lbl, bx - bw + 7, header_y, (Color){255, 255, 255, 255});
        bx -= bw + 6;
    }
    if (a->reading.range == 0.0f) {
        const char *lbl = "AUTO";
        int tw = 0, th = 0;
        TTF_SizeText(a->font_label, lbl, &tw, &th);
        int bw = tw + 14;
        set_color(r, COL_BG_WIDGET);
        fill_rounded(r, bx - bw, header_y - 2, bw, 20, 3);
        set_color(r, COL_BORDER_L);
        draw_rect(r, bx - bw, header_y - 2, bw, 20);
        draw_text(r, a->font_label, lbl, bx - bw + 7, header_y, COL_TEXT);
        bx -= bw + 6;
    }
    {
        const char *lbl = (a->reading.rate == DMM_RATE_SLOW) ? "SLOW"
                        : (a->reading.rate == DMM_RATE_FAST) ? "FAST" : "MED";
        int tw = 0, th = 0;
        TTF_SizeText(a->font_label, lbl, &tw, &th);
        int bw = tw + 14;
        set_color(r, COL_BG_WIDGET);
        fill_rounded(r, bx - bw, header_y - 2, bw, 20, 3);
        set_color(r, COL_BORDER_L);
        draw_rect(r, bx - bw, header_y - 2, bw, 20);
        draw_text(r, a->font_label, lbl, bx - bw + 7, header_y, COL_DIM);
    }

    /* Separator line under the header. */
    SDL_SetRenderDrawColor(r, 0, 50, 30, 255);
    SDL_RenderDrawLine(r, ix + 8, header_y + 22, ix + iw - 8, header_y + 22);

    /* Primary readout — dot-matrix VFD, sized to match the PSU full GUI
     * (dot_size=2, dot_gap=1, char_gap=5). 8½-digit display: 9 significant
     * digits, decimals chosen by engineering-scaled magnitude so the field
     * always shows the same visual width. */
    {
        int dot_size = 2;
        int dot_gap  = 1;
        int char_gap = 5;
        int char_h   = vfd_char_height(dot_size, dot_gap);
        int char_w   = vfd_char_width (dot_size, dot_gap);
        vfd_color_t on  = { COL_VAL.r, COL_VAL.g, COL_VAL.b, COL_VAL.a };
        vfd_color_t off = { 0, 20, 12, 255 };

        int content_y = header_y + 28;
        int content_h = ih - (content_y - iy) - 8;
        int row_y     = content_y + (content_h - char_h) / 2;

        char buf[32];
        if (a->reading.overload) {
            snprintf(buf, sizeof(buf), " -OL- ");
            vfd_color_t err = { COL_ERR.r, COL_ERR.g, COL_ERR.b, COL_ERR.a };
            int approx_w = 6 * (char_w + char_gap);
            int sx = ix + iw / 2 - approx_w / 2;
            vfd_draw_number(r, sx, row_y, buf, dot_size, dot_gap, char_gap, err, off, true);
        } else if (!a->reading.valid) {
            snprintf(buf, sizeof(buf), " --------- ");
            vfd_color_t dim = { COL_DIM.r, COL_DIM.g, COL_DIM.b, COL_DIM.a };
            int approx_w = 11 * (char_w + char_gap);
            int sx = ix + iw / 2 - approx_w / 2 - 40;
            vfd_draw_number(r, sx, row_y, buf, dot_size, dot_gap, char_gap, dim, off, true);
        } else {
            const char *prefix = "";
            float scaled = eng_scale(a->reading.value, &prefix);

            /* 8½-digit display: 9 significant digits arranged so the field
             * fits whatever magnitude the engineering-scaled value lands at. */
            float abs_v = fabsf(scaled);
            int decimals = (abs_v >= 100.0f) ? 6
                         : (abs_v >= 10.0f)  ? 7
                                             : 8;
            snprintf(buf, sizeof(buf), "%.*f", decimals, scaled);

            int approx_w = 9 * (char_w + char_gap);

            char unit[16];
            snprintf(unit, sizeof(unit), "%s%s", prefix, dmm_mode_unit(a->reading.mode));
            int unit_w = 0, _h = 0;
            TTF_SizeText(a->font_big, unit, &unit_w, &_h);

            int total_w = approx_w + 12 + unit_w;
            int sx = ix + iw / 2 - total_w / 2;
            vfd_draw_number(r, sx, row_y, buf, dot_size, dot_gap, char_gap, on, off, true);
            draw_text(r, a->font_big, unit, sx + approx_w + 12,
                      row_y + char_h / 2 - 12, COL_UNIT);
        }
    }

    /* Statistics row at the bottom of the VFD panel. */
    int sy = iy + ih - 26;
    char sbuf[80];
    if (a->stat_n > 0) {
        float avg = a->stat_sum / (float)a->stat_n;
        const char *pmn = "", *pmx = "", *pav = "";
        float vmn = eng_scale(a->stat_min, &pmn);
        float vmx = eng_scale(a->stat_max, &pmx);
        float vav = eng_scale(avg,         &pav);
        const char *u = dmm_mode_unit(a->reading.mode);
        snprintf(sbuf, sizeof(sbuf),
                 "min %.4f %s%s   max %.4f %s%s   avg %.4f %s%s   n=%u",
                 vmn, pmn, u, vmx, pmx, u, vav, pav, u, a->stat_n);
    } else {
        snprintf(sbuf, sizeof(sbuf), "min ---   max ---   avg ---   n=0");
    }
    draw_text(r, a->font_label, sbuf, ix + 14, sy, COL_DIM);
}

static void draw_trace(app_t *a, int x, int y, int w, int h) {
    SDL_Renderer *r = a->renderer;
    set_color(r, COL_SCOPE_BG);
    fill_rect(r, x, y, w, h);
    set_color(r, COL_BORDER);
    draw_rect(r, x, y, w, h);

    int n = a->trace_count;
    if (n < 2) {
        draw_text(r, a->font_label, "NO DATA", x + w / 2 - 30, y + h / 2 - 6, COL_DIM);
        return;
    }
    float mn = INFINITY, mx = -INFINITY;
    for (int i = 0; i < n; i++) {
        int idx = (a->trace_head - n + i + TRACE_LEN) % TRACE_LEN;
        float v = a->trace[idx];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    float range = mx - mn;
    if (range < 1e-12f) range = 1.0f;
    mn -= range * 0.1f; mx += range * 0.1f; range = mx - mn;

    set_color(r, COL_SCOPE_GR);
    for (int i = 1; i < 4; i++) {
        int gy = y + h * i / 4;
        SDL_RenderDrawLine(r, x, gy, x + w, gy);
    }

    set_color(r, COL_SCOPE_LN);
    int prev_px = -1, prev_py = -1;
    for (int i = 0; i < n; i++) {
        int idx = (a->trace_head - n + i + TRACE_LEN) % TRACE_LEN;
        float v = a->trace[idx];
        int px = x + 2 + i * (w - 4) / TRACE_LEN;
        int py = y + h - 2 - (int)((v - mn) / range * (h - 4));
        if (prev_px >= 0) SDL_RenderDrawLine(r, prev_px, prev_py, px, py);
        prev_px = px; prev_py = py;
    }

    const char *pmn = "", *pmx = "";
    float vmn = eng_scale(mn, &pmn);
    float vmx = eng_scale(mx, &pmx);
    char buf[24];
    snprintf(buf, sizeof(buf), "%.3f %s", vmx, pmx);
    draw_text(r, a->font_label, buf, x + 4, y + 2, COL_SCOPE_LN);
    snprintf(buf, sizeof(buf), "%.3f %s", vmn, pmn);
    draw_text(r, a->font_label, buf, x + 4, y + h - 14, COL_SCOPE_LN);
}

static void draw_controls(app_t *a, int x, int y, int w, int h) {
    SDL_Renderer *r = a->renderer;
    set_color(r, COL_PANEL);
    fill_rounded(r, x, y, w, h, 4);
    set_color(r, COL_BORDER);
    draw_rect(r, x, y, w, h);

    set_color(r, COL_BG_WIDGET);
    fill_rect(r, x + 1, y + 1, w - 2, 28);
    draw_text(r, a->font_medium, "CONTROL", x + 12, y + 8, COL_TEXT);

    /* Mode grid: 3 columns × 4 rows. */
    int gx = x + 10;
    int gy = y + 38;
    int btn_w = (w - 30) / 3;
    int btn_h = 30;
    static const dmm_mode_t order[12] = {
        DMM_MODE_DC_VOLTS, DMM_MODE_AC_VOLTS, DMM_MODE_DC_AMPS,
        DMM_MODE_AC_AMPS,  DMM_MODE_OHMS_2W,  DMM_MODE_OHMS_4W,
        DMM_MODE_CAPACITANCE, DMM_MODE_FREQUENCY, DMM_MODE_PERIOD,
        DMM_MODE_DIODE, DMM_MODE_CONTINUITY, DMM_MODE_TEMPERATURE,
    };
    for (int i = 0; i < 12; i++) {
        int col = i % 3, row = i / 3;
        int bx = gx + col * (btn_w + 5);
        int by = gy + row * (btn_h + 5);
        dmm_mode_t m = order[i];
        bool active = (a->reading.mode == m);
        bool enabled = a->drv->caps.supports_mode[m];
        draw_btn(a, bx, by, btn_w, btn_h, dmm_mode_label(m),
                 active, BTN_MODE_BASE + (int)m, enabled);
    }

    /* RANGE row. */
    int ry = gy + 4 * (btn_h + 5) + 8;
    draw_text(r, a->font_label, "RANGE", x + 12, ry + 8, COL_LABEL);
    int rbtn_w = (w - 30 - 55) / 1;
    draw_btn(a, x + 65, ry, rbtn_w, 28,
             a->reading.range == 0.0f ? "AUTO ★" : "AUTO",
             a->reading.range == 0.0f, BTN_RANGE_AUTO,
             a->drv->caps.supports_range_control);

    /* RATE row. */
    ry += 36;
    draw_text(r, a->font_label, "RATE", x + 12, ry + 8, COL_LABEL);
    int rt_w = (w - 30 - 55) / 3 - 4;
    bool en_rate = a->drv->caps.supports_rate_control;
    draw_btn(a, x + 65 + 0 * (rt_w + 4), ry, rt_w, 28, "SLOW",
             a->reading.rate == DMM_RATE_SLOW, BTN_RATE_SLOW, en_rate);
    draw_btn(a, x + 65 + 1 * (rt_w + 4), ry, rt_w, 28, "MED",
             a->reading.rate == DMM_RATE_MEDIUM, BTN_RATE_MED, en_rate);
    draw_btn(a, x + 65 + 2 * (rt_w + 4), ry, rt_w, 28, "FAST",
             a->reading.rate == DMM_RATE_FAST, BTN_RATE_FAST, en_rate);

    /* RESET stats. */
    ry += 38;
    draw_btn(a, x + 12, ry, w - 24, 28, "RESET STATS / TRACE",
             false, BTN_RESET_STATS, true);
}

static void render(app_t *a) {
    set_color(a->renderer, COL_BG);
    SDL_RenderClear(a->renderer);
    a->num_buttons = 0;

    draw_header(a);

    int margin = 10;
    int left_x = margin;
    int left_y = HEADER_H + margin;
    int left_w = 580;
    int readout_h = 320;
    int trace_y = left_y + readout_h + margin;
    int trace_h = WIN_H - trace_y - margin;

    draw_readout_panel(a, left_x, left_y, left_w, readout_h);
    draw_trace        (a, left_x, trace_y, left_w, trace_h);

    int ctrl_x = left_x + left_w + margin;
    int ctrl_w = WIN_W - ctrl_x - margin;
    int ctrl_h = WIN_H - HEADER_H - 2 * margin;
    draw_controls(a, ctrl_x, left_y, ctrl_w, ctrl_h);

    SDL_RenderPresent(a->renderer);
}

/* ---- input ---- */

static void handle_click(app_t *a, int mx, int my) {
    int id = btn_at(a, mx, my);
    if (id == BTN_NONE) return;

    if (id >= BTN_MODE_BASE && id < BTN_MODE_BASE + DMM_MODE_COUNT) {
        a->drv->set_mode(a->drv, (dmm_mode_t)(id - BTN_MODE_BASE));
        stats_reset(a);
        return;
    }
    if (id == BTN_RANGE_AUTO) { a->drv->set_range(a->drv, 0.0f); return; }
    if (id == BTN_RATE_SLOW)  { a->drv->set_rate(a->drv, DMM_RATE_SLOW);   return; }
    if (id == BTN_RATE_MED)   { a->drv->set_rate(a->drv, DMM_RATE_MEDIUM); return; }
    if (id == BTN_RATE_FAST)  { a->drv->set_rate(a->drv, DMM_RATE_FAST);   return; }
    if (id == BTN_RESET_STATS){ stats_reset(a); return; }
}

/* ---- lifecycle ---- */

static bool open_fonts(app_t *a) {
    const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
        NULL,
    };
    const char *path = NULL;
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) { fclose(f); path = paths[i]; break; }
    }
    if (!path) return false;
    a->font_title  = TTF_OpenFont(path, 20);
    a->font_label  = TTF_OpenFont(path, 12);
    a->font_medium = TTF_OpenFont(path, 14);
    a->font_big    = TTF_OpenFont(path, 24);
    a->font_huge   = TTF_OpenFont(path, 60);
    return a->font_title && a->font_label && a->font_medium && a->font_big && a->font_huge;
}

static void cleanup(app_t *a) {
    if (a->font_title)  TTF_CloseFont(a->font_title);
    if (a->font_label)  TTF_CloseFont(a->font_label);
    if (a->font_medium) TTF_CloseFont(a->font_medium);
    if (a->font_big)    TTF_CloseFont(a->font_big);
    if (a->font_huge)   TTF_CloseFont(a->font_huge);
    if (a->renderer)    SDL_DestroyRenderer(a->renderer);
    if (a->window)      SDL_DestroyWindow(a->window);
    TTF_Quit();
    SDL_Quit();
}

int view_dmm_full_run(dmm_driver_t *drv) {
    if (!drv) return 1;

    app_t a;
    memset(&a, 0, sizeof(a));
    a.drv     = drv;
    a.running = true;
    stats_reset(&a);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }

    a.window = SDL_CreateWindow("Open LabBench — DMM",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!a.window) { cleanup(&a); return 1; }

    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, 0);
    if (!a.renderer) { cleanup(&a); return 1; }
    SDL_SetRenderDrawBlendMode(a.renderer, SDL_BLENDMODE_BLEND);

    if (!open_fonts(&a)) { cleanup(&a); return 1; }

    while (a.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: a.running = false; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        handle_click(&a, ev.button.x, ev.button.y);
                    break;
                case SDL_MOUSEMOTION:
                    a.hover = btn_at(&a, ev.motion.x, ev.motion.y);
                    break;
                case SDL_KEYDOWN:
                    if (ev.key.keysym.sym == SDLK_ESCAPE) a.running = false;
                    break;
            }
        }
        a.drv->read(a.drv, &a.reading);
        stats_update(&a, a.reading.mode, a.reading.value, a.reading.valid);
        render(&a);
        SDL_Delay(16);
    }
    cleanup(&a);
    return 0;
}
