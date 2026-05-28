/**
 * Launcher window — driver/view/port picker + Launch button.
 *
 * Layout:
 *   ┌─────────────────────────────────────────────────────┐
 *   │ PSU Control Panel                                   │   header
 *   ├──────────────────────┬──────────────────────────────┤
 *   │ DRIVER               │ PORT  [/dev/ttyUSB0_______]  │
 *   │ ● modbus-bridge      │                              │
 *   │ ○ siglent-spd        │ VIEW                         │
 *   │ ○ demo               │ ● toolbar-single             │
 *   │                      │ ○ toolbar-dual               │
 *   │                      │ ○ full-single  (needs 1ch)   │
 *   │                      │ ○ full-dual    (needs 2ch)   │
 *   ├──────────────────────┴──────────────────────────────┤
 *   │ status: ready                       [LAUNCH] [QUIT] │
 *   └─────────────────────────────────────────────────────┘
 *
 * Clicking LAUNCH fork/execs self with --driver/--view/--port flags so each
 * window runs in its own process. The launcher stays open for more launches.
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

#define WIN_W   720
#define WIN_H   480
#define MAX_BTNS 64

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG       = {22, 22, 24, 255};
static const Color COL_PANEL    = {32, 32, 36, 255};
static const Color COL_BORDER   = {55, 55, 60, 255};
static const Color COL_TEXT     = {220, 220, 225, 255};
static const Color COL_DIM      = {110, 110, 118, 255};
static const Color COL_DISABLED = {70, 70, 75, 255};
static const Color COL_HEADER   = {20, 20, 22, 255};
static const Color COL_SEL      = {0, 140, 170, 255};
static const Color COL_BTN      = {50, 50, 55, 255};
static const Color COL_BTN_HOV  = {68, 68, 75, 255};
static const Color COL_BTN_HI   = {0, 140, 170, 255};
static const Color COL_OK       = {0, 200, 100, 255};
static const Color COL_INPUT_BG = {18, 18, 20, 255};
static const Color COL_FOCUS    = {0, 180, 220, 255};

enum {
    BTN_NONE      = 0,
    BTN_LAUNCH    = 1,
    BTN_QUIT      = 2,
    BTN_PORT      = 3,
    /* per-driver: BTN_DRIVER_BASE + index */
    BTN_DRIVER_BASE = 100,
    /* per-view:   BTN_VIEW_BASE   + index */
    BTN_VIEW_BASE   = 200,
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

    int  sel_driver;   /* index into drivers[] */
    int  sel_view;     /* index into views[]   */
    char port[128];
    bool port_focused;

    char status[160];
    Color status_col;

    bool running;
    int  hover;

    hitbox_t hits[MAX_BTNS];
    int      num_hits;

    /* cached registries */
    const psu_driver_factory_t *const *drivers;
    size_t                             n_drivers;
    const view_def_t           *const *views;
    size_t                             n_views;

    char self_exe[1024];
} launcher_t;

/* -------------- low-level draw helpers -------------- */

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderDrawRect(r, &rc);
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
    if (L->num_hits >= MAX_BTNS) return -1;
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
    for (int dy = -6; dy <= 6; dy++)
        for (int dx = -6; dx <= 6; dx++) {
            int d2 = dx*dx + dy*dy;
            if (d2 >= 25 && d2 <= 36) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    if (selected) {
        Color fill = enabled ? COL_SEL : COL_DISABLED;
        set_color(r, fill);
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++)
                if (dx*dx + dy*dy <= 9) SDL_RenderDrawPoint(r, cx + dx, cy + dy);
    }
}

static void draw_button(launcher_t *L, int x, int y, int w, int h,
                        const char *label, int id, bool primary, bool enabled) {
    bool hov = enabled && (L->hover == id);
    Color bg;
    if (!enabled)       bg = COL_DISABLED;
    else if (primary)   bg = hov ? (Color){0, 170, 200, 255} : COL_BTN_HI;
    else                bg = hov ? COL_BTN_HOV : COL_BTN;
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

/* -------------- panel drawing -------------- */

static void draw_header(launcher_t *L) {
    set_color(L->r, COL_HEADER);
    fill_rect(L->r, 0, 0, WIN_W, 40);
    set_color(L->r, COL_BORDER);
    SDL_RenderDrawLine(L->r, 0, 39, WIN_W, 39);
    draw_text(L->r, L->font_title, "PSU Control Panel", 14, 10, COL_TEXT);
    draw_text(L->r, L->font_small,
              "Pick a driver, port, and view. Click LAUNCH to open a new window.",
              14, 26, COL_DIM);
}

static void draw_driver_list(launcher_t *L) {
    int x = 14, y = 56, w = 330;
    draw_text(L->r, L->font_small, "DRIVER", x, y, COL_DIM);
    y += 18;

    set_color(L->r, COL_PANEL);
    fill_rect(L->r, x, y, w, WIN_H - y - 70);
    set_color(L->r, COL_BORDER);
    draw_rect(L->r, x, y, w, WIN_H - y - 70);

    int row_h = 56;
    for (size_t i = 0; i < L->n_drivers; i++) {
        int row_y = y + 8 + (int)i * row_h;
        bool sel = ((int)i == L->sel_driver);
        if (sel) {
            set_color(L->r, (Color){38, 38, 44, 255});
            fill_rect(L->r, x + 2, row_y, w - 4, row_h - 4);
        }
        draw_radio(L->r, x + 18, row_y + 14, sel, true);
        draw_text(L->r, L->font_label, L->drivers[i]->display_name,
                  x + 36, row_y + 6, sel ? COL_TEXT : COL_TEXT);
        draw_text(L->r, L->font_small, L->drivers[i]->description,
                  x + 36, row_y + 26, COL_DIM);
        add_hit(L, x + 2, row_y, w - 4, row_h - 4,
                BTN_DRIVER_BASE + (int)i, true);
    }
}

static void draw_port_field(launcher_t *L) {
    int x = 360, y = 56, w = WIN_W - x - 14, h = 34;
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

static void draw_view_list(launcher_t *L) {
    int x = 360, y = 112, w = WIN_W - x - 14;
    draw_text(L->r, L->font_small, "VIEW", x, y, COL_DIM);
    y += 18;

    int driver_ch = (L->sel_driver >= 0)
        ? L->drivers[L->sel_driver]->n_channels_hint
        : 2;

    int row_h = 46;
    int rows_h = (int)L->n_views * row_h + 16;
    set_color(L->r, COL_PANEL);
    fill_rect(L->r, x, y, w, rows_h);
    set_color(L->r, COL_BORDER);
    draw_rect(L->r, x, y, w, rows_h);

    for (size_t i = 0; i < L->n_views; i++) {
        int row_y = y + 8 + (int)i * row_h;
        const view_def_t *v = L->views[i];
        bool ported   = (v->run != NULL);
        bool fits     = (v->min_channels <= driver_ch);
        bool enabled  = ported && fits;
        bool sel      = ((int)i == L->sel_view) && enabled;
        if (sel) {
            set_color(L->r, (Color){38, 38, 44, 255});
            fill_rect(L->r, x + 2, row_y, w - 4, row_h - 4);
        }
        draw_radio(L->r, x + 18, row_y + 12, sel, enabled);

        char title[128];
        if (!ported)
            snprintf(title, sizeof(title), "%s   (not yet ported)", v->display_name);
        else if (!fits)
            snprintf(title, sizeof(title), "%s   (needs %d ch)",
                     v->display_name, v->min_channels);
        else
            snprintf(title, sizeof(title), "%s", v->display_name);
        Color title_c = enabled ? COL_TEXT : COL_DIM;
        draw_text(L->r, L->font_label, title, x + 36, row_y + 4, title_c);
        draw_text(L->r, L->font_small, v->description, x + 36, row_y + 22, COL_DIM);
        add_hit(L, x + 2, row_y, w - 4, row_h - 4,
                BTN_VIEW_BASE + (int)i, enabled);
    }
}

static void draw_footer(launcher_t *L) {
    int fy = WIN_H - 56;
    set_color(L->r, COL_HEADER);
    fill_rect(L->r, 0, fy, WIN_W, 56);
    set_color(L->r, COL_BORDER);
    SDL_RenderDrawLine(L->r, 0, fy, WIN_W, fy);

    draw_text(L->r, L->font_small, "status:", 14, fy + 14, COL_DIM);
    draw_text(L->r, L->font_label, L->status, 70, fy + 12, L->status_col);

    bool can_launch = (L->sel_driver >= 0) &&
                      (L->sel_view   >= 0) &&
                      L->views[L->sel_view]->run != NULL &&
                      L->port[0] != '\0';

    draw_button(L, WIN_W - 220, fy + 12, 100, 32, "LAUNCH",
                BTN_LAUNCH, true,  can_launch);
    draw_button(L, WIN_W - 110, fy + 12,  96, 32, "QUIT",
                BTN_QUIT,   false, true);
}

static void render(launcher_t *L) {
    set_color(L->r, COL_BG);
    SDL_RenderClear(L->r);
    L->num_hits = 0;
    draw_header(L);
    draw_driver_list(L);
    draw_port_field(L);
    draw_view_list(L);
    draw_footer(L);
    SDL_RenderPresent(L->r);
}

/* -------------- actions -------------- */

static void set_status(launcher_t *L, Color c, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(L->status, sizeof(L->status), fmt, ap);
    va_end(ap);
    L->status_col = c;
}

static void on_driver_selected(launcher_t *L, int idx) {
    L->sel_driver = idx;
    int driver_ch = L->drivers[idx]->n_channels_hint;
    /* Drop the view selection if the new driver can't satisfy its min_channels. */
    if (L->sel_view >= 0 && L->views[L->sel_view]->min_channels > driver_ch)
        L->sel_view = -1;
}

static int spawn_instance(launcher_t *L) {
    if (L->sel_driver < 0 || L->sel_view < 0) return -1;
    const psu_driver_factory_t *f = L->drivers[L->sel_driver];
    const view_def_t           *v = L->views  [L->sel_view];
    if (!v->run) return -1;

    char arg_driver[64], arg_view[64], arg_port[160], arg_baud[32];
    snprintf(arg_driver, sizeof(arg_driver), "--driver=%s", f->id);
    snprintf(arg_view,   sizeof(arg_view),   "--view=%s",   v->id);
    snprintf(arg_port,   sizeof(arg_port),   "--port=%s",
             L->port[0] ? L->port : "-");
    snprintf(arg_baud,   sizeof(arg_baud),   "--baud=%d",   f->default_baud);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* child: replace with a fresh psu_app */
        char *argv[] = {
            (char *)L->self_exe,
            arg_driver, arg_view, arg_port, arg_baud,
            NULL,
        };
        execv(L->self_exe, argv);
        _exit(127);
    }
    set_status(L, COL_OK, "launched %s + %s  (pid %d)", f->id, v->id, (int)pid);
    return 0;
}

static void handle_click(launcher_t *L, int mx, int my) {
    int id = hit_at(L, mx, my);

    /* Click outside the port field — defocus. */
    L->port_focused = (id == BTN_PORT);

    if (id == BTN_NONE) return;
    if (id == BTN_QUIT)   { L->running = false; return; }
    if (id == BTN_LAUNCH) { spawn_instance(L);  return; }
    if (id == BTN_PORT)   { return; /* focus handled above */ }

    if (id >= BTN_DRIVER_BASE && id < BTN_DRIVER_BASE + (int)L->n_drivers) {
        on_driver_selected(L, id - BTN_DRIVER_BASE);
        return;
    }
    if (id >= BTN_VIEW_BASE && id < BTN_VIEW_BASE + (int)L->n_views) {
        L->sel_view = id - BTN_VIEW_BASE;
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

static void handle_text(launcher_t *L, const char *t) {
    if (!L->port_focused) return;
    size_t n = strlen(L->port);
    for (const char *p = t; *p && n < sizeof(L->port) - 1; p++) {
        if (*p < 0x20) continue;          /* no control chars */
        L->port[n++] = *p;
    }
    L->port[n] = '\0';
}

/* -------------- setup -------------- */

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

    /* Auto-reap launched children so they don't become zombies. */
    signal(SIGCHLD, SIG_IGN);

    launcher_t L;
    memset(&L, 0, sizeof(L));
    L.drivers = psu_drivers_list(&L.n_drivers);
    L.views   = views_list      (&L.n_views);
    L.sel_driver = (L.n_drivers > 0) ? 0 : -1;
    L.sel_view   = -1;
    /* Default-select the first ported view if any. */
    for (size_t i = 0; i < L.n_views; i++) {
        if (L.views[i]->run) { L.sel_view = (int)i; break; }
    }
    snprintf(L.port, sizeof(L.port), "-");
    snprintf(L.self_exe, sizeof(L.self_exe), "%s", self_exe_path);
    set_status(&L, COL_DIM, "ready");
    L.running = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }
    L.win = SDL_CreateWindow("PSU Control Panel",
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
            }
        }
        render(&L);
        SDL_Delay(12);
    }
    SDL_StopTextInput();
    cleanup(&L);
    return 0;
}
