/**
 * Launcher window — driver/view/port picker + LAUNCH button.
 *
 * Layout (sections on each side, greying driven by selected driver kind):
 *
 *   ┌──────────────────────────────────────────────────────┐
 *   │ Bench Instrument Launcher                            │
 *   ├──────────────────────────────┬───────────────────────┤
 *   │ DRIVER                       │ PORT [/dev/ttyUSB0]   │
 *   │ ── PSU ──────────────────    │                       │
 *   │ ● modbus-bridge              │ VIEW                  │
 *   │ ○ siglent-spd                │ ── PSU ────────────   │
 *   │ ○ rigol-dp832                │ ● toolbar-single      │
 *   │ ...                          │ ○ toolbar-dual        │
 *   │ ○ korad-ka                   │ ○ full-single         │
 *   │ ○ demo                       │ ○ full-dual           │
 *   │ ── DMM ──────────────────    │ ── DMM ────────────   │
 *   │ ○ owon-xdm                   │ ○ dmm-toolbar         │
 *   │ ○ dmm-demo                   │ ○ dmm-full            │
 *   ├──────────────────────────────┴───────────────────────┤
 *   │ status: ready                       [LAUNCH] [QUIT]  │
 *   └──────────────────────────────────────────────────────┘
 *
 * The selected driver dictates which view section is enabled: pick a PSU
 * driver and DMM views grey out, pick a DMM driver and PSU views grey out.
 *
 * Clicking LAUNCH fork/execs psu_app with --driver/--view/--port flags so
 * each window runs in its own process. The launcher stays open for more.
 */

#include "launcher.h"

#include "drivers/registry.h"
#include "views/views.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define WIN_W       760
#define WIN_H       720
#define MAX_HITS    128

typedef enum {
    KIND_PSU = 0,
    KIND_DMM = 1,
} kind_t;

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG       = {22, 22, 24, 255};
static const Color COL_PANEL    = {32, 32, 36, 255};
static const Color COL_BORDER   = {55, 55, 60, 255};
static const Color COL_SECTION  = {200, 160, 60, 255};
static const Color COL_TEXT     = {220, 220, 225, 255};
static const Color COL_DIM      = {110, 110, 118, 255};
static const Color COL_DISABLED = {70, 70, 75, 255};
static const Color COL_HEADER   = {20, 20, 22, 255};
static const Color COL_SEL_ROW  = {38, 38, 44, 255};
static const Color COL_SEL      = {0, 140, 170, 255};
static const Color COL_BTN      = {50, 50, 55, 255};
static const Color COL_BTN_HOV  = {68, 68, 75, 255};
static const Color COL_BTN_HI   = {0, 140, 170, 255};
static const Color COL_BTN_PRI  = {0, 170, 200, 255};
static const Color COL_OK       = {0, 200, 100, 255};
static const Color COL_INPUT_BG = {18, 18, 20, 255};
static const Color COL_FOCUS    = {0, 180, 220, 255};

enum {
    BTN_NONE              = 0,
    BTN_LAUNCH            = 1,
    BTN_QUIT              = 2,
    BTN_PORT              = 3,
    BTN_PSU_DRIVER_BASE   = 100,    /* + index into PSU drivers */
    BTN_DMM_DRIVER_BASE   = 200,    /* + index into DMM drivers */
    BTN_PSU_VIEW_BASE     = 300,    /* + index into PSU views   */
    BTN_DMM_VIEW_BASE     = 400,    /* + index into DMM views   */
};

typedef struct {
    SDL_Rect rect;
    int id;
    bool enabled;
} hitbox_t;

typedef struct {
    SDL_Window   *win;
    SDL_Renderer *r;
    TTF_Font     *font_title;
    TTF_Font     *font_label;
    TTF_Font     *font_small;

    kind_t sel_kind;    /* which driver kind is currently selected */
    int    sel_psu_drv; /* -1 if none */
    int    sel_dmm_drv;
    int    sel_psu_view;
    int    sel_dmm_view;

    char port[128];
    bool port_focused;

    char  status[200];
    Color status_col;

    bool running;
    int  hover;

    hitbox_t hits[MAX_HITS];
    int      num_hits;

    /* Scroll state: rect = on-screen viewport, content_h = full content height,
     * scroll_y = current offset. Updated each frame by the draw functions and
     * read by the mouse-wheel handler. */
    SDL_Rect driver_panel_rect;
    int      driver_content_h;
    int      driver_scroll_y;

    SDL_Rect view_panel_rect;
    int      view_content_h;
    int      view_scroll_y;

    /* When set (w > 0), add_hit() intersects every new hitbox with this
     * rect so rows scrolled out of the panel don't catch clicks. */
    SDL_Rect hit_clip;

    /* Cached registries (lifetime = program). */
    const psu_driver_factory_t *const *psu_drv;   size_t n_psu_drv;
    const dmm_driver_factory_t *const *dmm_drv;   size_t n_dmm_drv;
    const view_def_t           *const *psu_view;  size_t n_psu_view;
    const dmm_view_def_t       *const *dmm_view;  size_t n_dmm_view;

    char self_exe[1024];
} launcher_t;

/* ---- low-level draw helpers ---- */

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}
static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderFillRect(r, &rc);
}
static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h}; SDL_RenderDrawRect(r, &rc);
}
static int draw_text(SDL_Renderer *r, TTF_Font *font, const char *text,
                     int x, int y, Color color) {
    if (!font || !text || !*text) return 0;
    SDL_Color c = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, c);
    if (!surf) return 0;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    int tw = surf->w;
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    return tw;
}

static int add_hit(launcher_t *L, int x, int y, int w, int h, int id, bool enabled) {
    if (L->num_hits >= MAX_HITS) return -1;
    if (L->hit_clip.w > 0) {
        /* Intersect the proposed hit rect with the current clip rect; drop
         * it entirely if there's no overlap. */
        int x1 = (x > L->hit_clip.x) ? x : L->hit_clip.x;
        int y1 = (y > L->hit_clip.y) ? y : L->hit_clip.y;
        int x2 = (x + w < L->hit_clip.x + L->hit_clip.w) ? x + w : L->hit_clip.x + L->hit_clip.w;
        int y2 = (y + h < L->hit_clip.y + L->hit_clip.h) ? y + h : L->hit_clip.y + L->hit_clip.h;
        if (x2 <= x1 || y2 <= y1) return -1;
        x = x1; y = y1; w = x2 - x1; h = y2 - y1;
    }
    L->hits[L->num_hits] = (hitbox_t){ .rect = {x, y, w, h}, .id = id, .enabled = enabled };
    return L->num_hits++;
}
static int hit_at(launcher_t *L, int mx, int my) {
    for (int i = 0; i < L->num_hits; i++) {
        if (!L->hits[i].enabled) continue;
        SDL_Rect *r = &L->hits[i].rect;
        if (mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h)
            return L->hits[i].id;
    }
    return BTN_NONE;
}

static void draw_radio(SDL_Renderer *r, int cx, int cy, bool selected, bool enabled) {
    Color outline = enabled ? COL_TEXT : COL_DISABLED;
    set_color(r, outline);
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 >= 16 && d2 <= 25) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    if (selected) {
        Color fill = enabled ? COL_SEL : COL_DISABLED;
        set_color(r, fill);
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                if (dx*dx + dy*dy <= 4) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
    }
}

/* Vertical scrollbar drawn at the right edge of `panel`. content_h is the
 * full content height in pixels; scroll_y is the current offset. No-op when
 * content fits. */
static void draw_scrollbar(launcher_t *L, SDL_Rect panel, int content_h, int scroll_y) {
    if (content_h <= panel.h) return;
    int bar_x = panel.x + panel.w - 6;
    int bar_w = 4;
    set_color(L->r, (Color){50, 50, 55, 255});
    fill_rect(L->r, bar_x, panel.y, bar_w, panel.h);

    int thumb_h = (int)((float)panel.h * (float)panel.h / (float)content_h);
    if (thumb_h < 24) thumb_h = 24;
    int track_room = panel.h - thumb_h;
    int max_scroll = content_h - panel.h;
    int thumb_y = panel.y + (max_scroll > 0
                             ? (int)((float)track_room * (float)scroll_y / (float)max_scroll)
                             : 0);
    set_color(L->r, COL_DIM);
    fill_rect(L->r, bar_x, thumb_y, bar_w, thumb_h);
}

static int clamp_scroll(int v, int content_h, int panel_h) {
    int max_s = content_h - panel_h;
    if (max_s < 0) max_s = 0;
    if (v < 0)      v = 0;
    if (v > max_s)  v = max_s;
    return v;
}

static void draw_button(launcher_t *L, int x, int y, int w, int h,
                        const char *label, int id, bool primary, bool enabled) {
    bool hov = enabled && (L->hover == id);
    Color bg;
    if (!enabled)     bg = COL_DISABLED;
    else if (primary) bg = hov ? COL_BTN_PRI : COL_BTN_HI;
    else              bg = hov ? COL_BTN_HOV : COL_BTN;
    set_color(L->r, bg);
    fill_rect(L->r, x, y, w, h);
    set_color(L->r, COL_BORDER);
    draw_rect(L->r, x, y, w, h);
    int tw = 0, th = 0;
    TTF_SizeText(L->font_label, label, &tw, &th);
    Color fg = enabled ? (primary ? (Color){255,255,255,255} : COL_TEXT) : COL_DIM;
    draw_text(L->r, L->font_label, label, x + (w - tw) / 2, y + (h - th) / 2, fg);
    add_hit(L, x, y, w, h, id, enabled);
}

/* ---- panel drawing ---- */

static void draw_top_header(launcher_t *L) {
    set_color(L->r, COL_HEADER);
    fill_rect(L->r, 0, 0, WIN_W, 40);
    set_color(L->r, COL_BORDER);
    SDL_RenderDrawLine(L->r, 0, 39, WIN_W, 39);
    draw_text(L->r, L->font_title, "Bench Instrument Launcher", 14, 10, COL_TEXT);
    draw_text(L->r, L->font_small,
              "Pick a driver, port, and view. LAUNCH opens a new window per click.",
              14, 26, COL_DIM);
}

/* Draws one driver row. Returns y advance. */
static int draw_drv_row(launcher_t *L, int x, int y, int w,
                        const char *name, bool sel, bool enabled, int btn_id) {
    int row_h = 24;
    if (sel) {
        set_color(L->r, COL_SEL_ROW);
        fill_rect(L->r, x + 2, y, w - 4, row_h - 2);
    }
    draw_radio(L->r, x + 18, y + row_h / 2 - 1, sel, enabled);
    Color tc = enabled ? COL_TEXT : COL_DIM;
    draw_text(L->r, L->font_label, name, x + 32, y + 4, tc);
    add_hit(L, x + 2, y, w - 4, row_h - 2, btn_id, enabled);
    return row_h;
}

static int draw_section_label(launcher_t *L, int x, int y, int w, const char *label) {
    int sh = 20;
    set_color(L->r, COL_PANEL);
    fill_rect(L->r, x + 2, y, w - 4, sh);
    set_color(L->r, COL_BORDER);
    SDL_RenderDrawLine(L->r, x + 8, y + sh - 1, x + w - 8, y + sh - 1);
    draw_text(L->r, L->font_small, label, x + 12, y + 4, COL_SECTION);
    return sh + 2;
}

static void draw_driver_list(launcher_t *L) {
    int x = 14, y = 56, w = 360;
    draw_text(L->r, L->font_small, "DRIVER", x, y, COL_DIM);
    y += 18;

    int panel_y = y;
    int panel_h = WIN_H - panel_y - 70;
    set_color(L->r, COL_PANEL);
    fill_rect(L->r, x, panel_y, w, panel_h);
    set_color(L->r, COL_BORDER);
    draw_rect(L->r, x, panel_y, w, panel_h);

    L->driver_panel_rect = (SDL_Rect){ x, panel_y, w, panel_h };

    /* Clip drawing AND hit-tests to the panel interior so scrolled-out rows
     * neither draw nor catch stray clicks. */
    SDL_Rect clip = { x + 1, panel_y + 1, w - 2, panel_h - 2 };
    SDL_RenderSetClipRect(L->r, &clip);
    L->hit_clip = clip;

    int rx = x;
    int content_start = panel_y + 4 - L->driver_scroll_y;
    int ry = content_start;

    /* PSU section. */
    ry += draw_section_label(L, rx, ry, w, "PSU");
    for (size_t i = 0; i < L->n_psu_drv; i++) {
        bool sel = (L->sel_kind == KIND_PSU) && ((int)i == L->sel_psu_drv);
        ry += draw_drv_row(L, rx, ry, w, L->psu_drv[i]->display_name,
                           sel, true, BTN_PSU_DRIVER_BASE + (int)i);
    }

    /* DMM section. */
    ry += 4;
    ry += draw_section_label(L, rx, ry, w, "DMM");
    for (size_t i = 0; i < L->n_dmm_drv; i++) {
        bool sel = (L->sel_kind == KIND_DMM) && ((int)i == L->sel_dmm_drv);
        ry += draw_drv_row(L, rx, ry, w, L->dmm_drv[i]->display_name,
                           sel, true, BTN_DMM_DRIVER_BASE + (int)i);
    }

    L->driver_content_h = ry - content_start;

    SDL_RenderSetClipRect(L->r, NULL);
    L->hit_clip = (SDL_Rect){0, 0, 0, 0};
    draw_scrollbar(L, L->driver_panel_rect, L->driver_content_h, L->driver_scroll_y);
}

static void draw_port_field(launcher_t *L) {
    int x = 388, y = 56, w = WIN_W - x - 14, h = 34;
    draw_text(L->r, L->font_small, "PORT", x, y, COL_DIM);
    y += 18;
    set_color(L->r, COL_INPUT_BG);
    fill_rect(L->r, x, y, w, h);
    set_color(L->r, L->port_focused ? COL_FOCUS : COL_BORDER);
    draw_rect(L->r, x, y, w, h);
    int tw = 0, th = 0;
    TTF_SizeText(L->font_label, L->port, &tw, &th);
    draw_text(L->r, L->font_label, L->port, x + 8, y + (h - th) / 2, COL_TEXT);
    if (L->port_focused) {
        int cx = x + 8 + tw + 1;
        set_color(L->r, COL_FOCUS);
        SDL_RenderDrawLine(L->r, cx, y + 6, cx, y + h - 6);
    }
    add_hit(L, x, y, w, h, BTN_PORT, true);
}

/* Returns y advance after drawing a view row. */
static int draw_view_row(launcher_t *L, int x, int y, int w,
                         const char *name, const char *suffix,
                         bool sel, bool enabled, int btn_id) {
    int row_h = 26;
    if (sel) {
        set_color(L->r, COL_SEL_ROW);
        fill_rect(L->r, x + 2, y, w - 4, row_h - 2);
    }
    draw_radio(L->r, x + 18, y + row_h / 2 - 1, sel, enabled);
    Color tc = enabled ? COL_TEXT : COL_DIM;
    char buf[160];
    if (suffix && *suffix) snprintf(buf, sizeof(buf), "%s   %s", name, suffix);
    else                   snprintf(buf, sizeof(buf), "%s", name);
    draw_text(L->r, L->font_label, buf, x + 32, y + 5, tc);
    add_hit(L, x + 2, y, w - 4, row_h - 2, btn_id, enabled);
    return row_h;
}

static void draw_view_list(launcher_t *L) {
    int x = 388, y = 112, w = WIN_W - x - 14;
    draw_text(L->r, L->font_small, "VIEW", x, y, COL_DIM);
    y += 18;

    int panel_y = y;
    int panel_h = WIN_H - panel_y - 70;
    set_color(L->r, COL_PANEL);
    fill_rect(L->r, x, panel_y, w, panel_h);
    set_color(L->r, COL_BORDER);
    draw_rect(L->r, x, panel_y, w, panel_h);

    L->view_panel_rect = (SDL_Rect){ x, panel_y, w, panel_h };

    SDL_Rect clip = { x + 1, panel_y + 1, w - 2, panel_h - 2 };
    SDL_RenderSetClipRect(L->r, &clip);
    L->hit_clip = clip;

    bool psu_active = (L->sel_kind == KIND_PSU);
    bool dmm_active = (L->sel_kind == KIND_DMM);

    int driver_ch = (L->sel_psu_drv >= 0)
                    ? L->psu_drv[L->sel_psu_drv]->n_channels_hint : 2;

    int content_start = panel_y + 4 - L->view_scroll_y;
    int ry = content_start;

    /* PSU views. */
    ry += draw_section_label(L, x, ry, w, "PSU");
    for (size_t i = 0; i < L->n_psu_view; i++) {
        const view_def_t *v = L->psu_view[i];
        bool ported = (v->run != NULL);
        bool fits   = (v->min_channels <= driver_ch);
        bool ok     = psu_active && ported && fits;
        bool sel    = ok && ((int)i == L->sel_psu_view);
        char suffix[64] = "";
        if (!ported)              snprintf(suffix, sizeof(suffix), "(not yet ported)");
        else if (psu_active && !fits)
                                  snprintf(suffix, sizeof(suffix), "(needs %d ch)", v->min_channels);
        ry += draw_view_row(L, x, ry, w, v->display_name, suffix,
                            sel, ok, BTN_PSU_VIEW_BASE + (int)i);
    }

    /* DMM views. */
    ry += 4;
    ry += draw_section_label(L, x, ry, w, "DMM");
    for (size_t i = 0; i < L->n_dmm_view; i++) {
        const dmm_view_def_t *v = L->dmm_view[i];
        bool ok  = dmm_active && (v->run != NULL);
        bool sel = ok && ((int)i == L->sel_dmm_view);
        ry += draw_view_row(L, x, ry, w, v->display_name, "",
                            sel, ok, BTN_DMM_VIEW_BASE + (int)i);
    }

    L->view_content_h = ry - content_start;

    SDL_RenderSetClipRect(L->r, NULL);
    L->hit_clip = (SDL_Rect){0, 0, 0, 0};
    draw_scrollbar(L, L->view_panel_rect, L->view_content_h, L->view_scroll_y);
}

/* Used by the footer to figure out if LAUNCH should be enabled. */
static bool can_launch(launcher_t *L) {
    if (L->port[0] == '\0') return false;
    if (L->sel_kind == KIND_PSU) {
        if (L->sel_psu_drv  < 0 || L->sel_psu_view < 0) return false;
        const view_def_t *v = L->psu_view[L->sel_psu_view];
        if (!v->run) return false;
        int ch = L->psu_drv[L->sel_psu_drv]->n_channels_hint;
        if (v->min_channels > ch) return false;
        return true;
    }
    if (L->sel_dmm_drv < 0 || L->sel_dmm_view < 0) return false;
    const dmm_view_def_t *v = L->dmm_view[L->sel_dmm_view];
    return v->run != NULL;
}

static void draw_footer(launcher_t *L) {
    int fy = WIN_H - 56;
    set_color(L->r, COL_HEADER);
    fill_rect(L->r, 0, fy, WIN_W, 56);
    set_color(L->r, COL_BORDER);
    SDL_RenderDrawLine(L->r, 0, fy, WIN_W, fy);

    draw_text(L->r, L->font_small, "status:", 14, fy + 14, COL_DIM);
    draw_text(L->r, L->font_label, L->status, 70, fy + 12, L->status_col);

    draw_button(L, WIN_W - 220, fy + 12, 100, 32, "LAUNCH",
                BTN_LAUNCH, true,  can_launch(L));
    draw_button(L, WIN_W - 110, fy + 12,  96, 32, "QUIT",
                BTN_QUIT,   false, true);
}

static void render(launcher_t *L) {
    set_color(L->r, COL_BG);
    SDL_RenderClear(L->r);
    L->num_hits = 0;
    draw_top_header(L);
    draw_driver_list(L);
    draw_port_field(L);
    draw_view_list(L);
    draw_footer(L);
    SDL_RenderPresent(L->r);
}

/* ---- actions ---- */

static void set_status(launcher_t *L, Color c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(L->status, sizeof(L->status), fmt, ap);
    va_end(ap);
    L->status_col = c;
}

static void select_psu_driver(launcher_t *L, int idx) {
    L->sel_kind     = KIND_PSU;
    L->sel_psu_drv  = idx;
    /* Drop the PSU view selection if the new driver can't satisfy its
     * min_channels. */
    if (L->sel_psu_view >= 0) {
        const view_def_t *v = L->psu_view[L->sel_psu_view];
        int ch = L->psu_drv[idx]->n_channels_hint;
        if (!v->run || v->min_channels > ch) L->sel_psu_view = -1;
    }
    /* Default-select the first compatible ported PSU view if none picked. */
    if (L->sel_psu_view < 0) {
        int ch = L->psu_drv[idx]->n_channels_hint;
        for (size_t i = 0; i < L->n_psu_view; i++) {
            const view_def_t *v = L->psu_view[i];
            if (v->run && v->min_channels <= ch) { L->sel_psu_view = (int)i; break; }
        }
    }
    set_status(L, COL_DIM, "%s", L->psu_drv[idx]->description);
}

static void select_dmm_driver(launcher_t *L, int idx) {
    L->sel_kind    = KIND_DMM;
    L->sel_dmm_drv = idx;
    if (L->sel_dmm_view < 0) {
        for (size_t i = 0; i < L->n_dmm_view; i++) {
            if (L->dmm_view[i]->run) { L->sel_dmm_view = (int)i; break; }
        }
    }
    set_status(L, COL_DIM, "%s", L->dmm_drv[idx]->description);
}

static int spawn_instance(launcher_t *L) {
    const char *drv_id = NULL;
    const char *view_id = NULL;
    int baud = 0;

    if (L->sel_kind == KIND_PSU) {
        if (L->sel_psu_drv < 0 || L->sel_psu_view < 0) return -1;
        drv_id  = L->psu_drv [L->sel_psu_drv ]->id;
        view_id = L->psu_view[L->sel_psu_view]->id;
        baud    = L->psu_drv [L->sel_psu_drv ]->default_baud;
    } else {
        if (L->sel_dmm_drv < 0 || L->sel_dmm_view < 0) return -1;
        drv_id  = L->dmm_drv [L->sel_dmm_drv ]->id;
        view_id = L->dmm_view[L->sel_dmm_view]->id;
        baud    = L->dmm_drv [L->sel_dmm_drv ]->default_baud;
    }

    char arg_driver[64], arg_view[64], arg_port[160], arg_baud[32];
    snprintf(arg_driver, sizeof(arg_driver), "--driver=%s", drv_id);
    snprintf(arg_view,   sizeof(arg_view),   "--view=%s",   view_id);
    snprintf(arg_port,   sizeof(arg_port),   "--port=%s",
             L->port[0] ? L->port : "-");
    snprintf(arg_baud,   sizeof(arg_baud),   "--baud=%d",   baud);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = {
            (char *)L->self_exe,
            arg_driver, arg_view, arg_port, arg_baud,
            NULL,
        };
        execv(L->self_exe, argv);
        _exit(127);
    }
    set_status(L, COL_OK, "launched %s + %s  (pid %d)", drv_id, view_id, (int)pid);
    return 0;
}

static void handle_click(launcher_t *L, int mx, int my) {
    int id = hit_at(L, mx, my);

    L->port_focused = (id == BTN_PORT);

    if (id == BTN_NONE)   return;
    if (id == BTN_QUIT)   { L->running = false; return; }
    if (id == BTN_LAUNCH) { spawn_instance(L);  return; }
    if (id == BTN_PORT)   { return; }

    if (id >= BTN_PSU_DRIVER_BASE && id < BTN_PSU_DRIVER_BASE + (int)L->n_psu_drv) {
        select_psu_driver(L, id - BTN_PSU_DRIVER_BASE);
        return;
    }
    if (id >= BTN_DMM_DRIVER_BASE && id < BTN_DMM_DRIVER_BASE + (int)L->n_dmm_drv) {
        select_dmm_driver(L, id - BTN_DMM_DRIVER_BASE);
        return;
    }
    if (id >= BTN_PSU_VIEW_BASE && id < BTN_PSU_VIEW_BASE + (int)L->n_psu_view) {
        L->sel_psu_view = id - BTN_PSU_VIEW_BASE;
        return;
    }
    if (id >= BTN_DMM_VIEW_BASE && id < BTN_DMM_VIEW_BASE + (int)L->n_dmm_view) {
        L->sel_dmm_view = id - BTN_DMM_VIEW_BASE;
        return;
    }
}

static void handle_key(launcher_t *L, SDL_Keycode k) {
    if (k == SDLK_ESCAPE) { L->running = false; return; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { spawn_instance(L); return; }
    if (!L->port_focused) return;
    size_t n = strlen(L->port);
    if (k == SDLK_BACKSPACE && n > 0) L->port[n - 1] = '\0';
}

static bool point_in_sdl_rect(int x, int y, const SDL_Rect *r) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void handle_wheel(launcher_t *L, int wheel_y, int mx, int my) {
    /* Each notch = 32 px of scroll. Negative wheel_y = wheel down = scroll down. */
    int step = -wheel_y * 32;
    if (point_in_sdl_rect(mx, my, &L->driver_panel_rect)) {
        L->driver_scroll_y = clamp_scroll(L->driver_scroll_y + step,
                                          L->driver_content_h,
                                          L->driver_panel_rect.h);
    } else if (point_in_sdl_rect(mx, my, &L->view_panel_rect)) {
        L->view_scroll_y = clamp_scroll(L->view_scroll_y + step,
                                        L->view_content_h,
                                        L->view_panel_rect.h);
    }
}

static void handle_text(launcher_t *L, const char *t) {
    if (!L->port_focused) return;
    size_t n = strlen(L->port);
    for (const char *p = t; *p && n < sizeof(L->port) - 1; p++) {
        if (*p < 0x20) continue;
        L->port[n++] = *p;
    }
    L->port[n] = '\0';
}

/* ---- setup ---- */

static bool open_fonts(launcher_t *L) {
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
    L->font_title = TTF_OpenFont(path, 14);
    L->font_label = TTF_OpenFont(path, 12);
    L->font_small = TTF_OpenFont(path, 10);
    return L->font_title && L->font_label && L->font_small;
}

static void cleanup(launcher_t *L) {
    if (L->font_title) TTF_CloseFont(L->font_title);
    if (L->font_label) TTF_CloseFont(L->font_label);
    if (L->font_small) TTF_CloseFont(L->font_small);
    if (L->r)   SDL_DestroyRenderer(L->r);
    if (L->win) SDL_DestroyWindow(L->win);
    TTF_Quit();
    SDL_Quit();
}

int launcher_run(const char *self_exe_path) {
    if (!self_exe_path || !*self_exe_path) {
        fprintf(stderr, "launcher: missing self-exe path\n");
        return 1;
    }

    signal(SIGCHLD, SIG_IGN);  /* auto-reap spawned instances */

    launcher_t L;
    memset(&L, 0, sizeof(L));
    L.psu_drv  = psu_drivers_list (&L.n_psu_drv);
    L.dmm_drv  = dmm_drivers_list (&L.n_dmm_drv);
    L.psu_view = views_list       (&L.n_psu_view);
    L.dmm_view = dmm_views_list   (&L.n_dmm_view);
    L.sel_kind     = KIND_PSU;
    L.sel_psu_drv  = (L.n_psu_drv > 0) ? 0 : -1;
    L.sel_dmm_drv  = -1;
    L.sel_psu_view = -1;
    L.sel_dmm_view = -1;
    if (L.sel_psu_drv >= 0) select_psu_driver(&L, L.sel_psu_drv);
    snprintf(L.port, sizeof(L.port), "-");
    snprintf(L.self_exe, sizeof(L.self_exe), "%s", self_exe_path);
    if (!L.status[0]) set_status(&L, COL_DIM, "ready");
    L.running = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }
    L.win = SDL_CreateWindow("Bench Instrument Launcher",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!L.win) { cleanup(&L); return 1; }
    L.r = SDL_CreateRenderer(L.win, -1,
                             SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!L.r) { cleanup(&L); return 1; }
    SDL_SetRenderDrawBlendMode(L.r, SDL_BLENDMODE_BLEND);
    if (!open_fonts(&L)) { cleanup(&L); return 1; }

    SDL_StartTextInput();
    while (L.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: L.running = false; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        handle_click(&L, ev.button.x, ev.button.y);
                    break;
                case SDL_MOUSEMOTION:
                    L.hover = hit_at(&L, ev.motion.x, ev.motion.y);
                    break;
                case SDL_KEYDOWN: handle_key(&L, ev.key.keysym.sym); break;
                case SDL_TEXTINPUT: handle_text(&L, ev.text.text); break;
                case SDL_MOUSEWHEEL: {
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    handle_wheel(&L, ev.wheel.y, mx, my);
                    break;
                }
            }
        }
        render(&L);
        SDL_Delay(12);
    }
    SDL_StopTextInput();
    cleanup(&L);
    return 0;
}
