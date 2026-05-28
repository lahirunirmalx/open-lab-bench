/**
 * Toolbar GUI — single channel.
 *
 * Compact strip: OUT label | V/A readouts | status flags | SET popup | OUT toggle.
 * One channel; on dual-channel drivers we always drive channel 1.
 *
 * This view talks only to psu_driver_t — no transport / no globals.
 */

#include "views.h"

#include "platform/platform.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W           500
#define WIN_H_STRIP     80
#define HEADER_H        18
#define STRIP_BLOCK_H   (WIN_H_STRIP - HEADER_H - 8)
#define STRIP_TOP_Y     (HEADER_H + 4)
#define STRIP_BOTTOM_Y  (STRIP_TOP_Y + STRIP_BLOCK_H)
#define POPUP_PW           300
#define POPUP_PH           204
#define POPUP_BELOW_STRIP  8
#define POPUP_PAD_BOTTOM   12
#define WIN_H_POPUP        (STRIP_BOTTOM_Y + POPUP_BELOW_STRIP + POPUP_PH + POPUP_PAD_BOTTOM)

#define MAX_BUTTONS 24

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG_DARK   = {22, 22, 24, 255};
static const Color COL_BG_STRIP  = {32, 32, 36, 255};
static const Color COL_HEADER    = {20, 20, 22, 255};
static const Color COL_BORDER    = {55, 55, 60, 255};
static const Color COL_V_BRIGHT  = {0, 255, 140, 255};
static const Color COL_A_BRIGHT  = {120, 220, 255, 255};
static const Color COL_ON        = {0, 255, 100, 255};
static const Color COL_OFF       = {90, 35, 35, 255};
static const Color COL_CV        = {100, 255, 140, 255};
static const Color COL_CC        = {255, 200, 60, 255};
static const Color COL_ERR       = {255, 70, 70, 255};
static const Color COL_DIM       = {100, 100, 108, 255};
static const Color COL_BTN       = {50, 50, 55, 255};
static const Color COL_BTN_HOVER = {68, 68, 75, 255};
static const Color COL_BTN_HI    = {0, 140, 170, 255};
static const Color COL_POPUP_BG  = {40, 40, 44, 250};
static const Color COL_INPUT_BG  = {18, 18, 20, 255};
static const Color COL_INPUT_FG  = {220, 220, 225, 255};
static const Color COL_FOCUS     = {0, 180, 220, 255};

typedef struct {
    SDL_Rect rect;
    int id;
} button_t;

enum {
    BTN_CH_OUT = 10,
    BTN_CH_SET = 11,
    BTN_POP_APPLY = 30,
    BTN_POP_CANCEL = 31,
    BTN_POP_FIELD_V = 32,
    BTN_POP_FIELD_A = 33,
};

typedef struct {
    psu_driver_t          *drv;

    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font_label;
    TTF_Font     *font_num;
    TTF_Font     *font_stat;
    TTF_Font     *font_pop;

    psu_channel_state_t ch;
    bool   running;
    int    hover_btn;

    bool   popup_open;
    int    popup_focus;        /* 0 = V field, 1 = A field */
    char   popup_v[16];
    char   popup_a[16];

    button_t buttons[MAX_BUTTONS];
    int      num_buttons;
} app_t;

static void set_color(SDL_Renderer *r, Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void draw_rect(SDL_Renderer *r, int x, int y, int w, int h) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderDrawRect(r, &rect);
}

static void draw_text(SDL_Renderer *r, TTF_Font *font, const char *text,
                      int x, int y, Color color, int align) {
    if (!font || !text || !*text) return;
    SDL_Color c = {color.r, color.g, color.b, color.a};
    SDL_Surface *surf = TTF_RenderText_Blended(font, text, c);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) {
        SDL_FreeSurface(surf);
        return;
    }
    SDL_Rect dst = {x, y, surf->w, surf->h};
    if (align == 1) dst.x = x - surf->w / 2;
    else if (align == 2) dst.x = x - surf->w;
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static int add_button(app_t *a, int x, int y, int w, int h, int id) {
    if (a->num_buttons >= MAX_BUTTONS) return -1;
    a->buttons[a->num_buttons] = (button_t){ .rect = {x, y, w, h}, .id = id };
    return a->num_buttons++;
}

static int button_at(app_t *a, int mx, int my) {
    for (int i = 0; i < a->num_buttons; i++) {
        SDL_Rect *r = &a->buttons[i].rect;
        if (mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h)
            return a->buttons[i].id;
    }
    return 0;
}

static void draw_led(SDL_Renderer *r, int cx, int cy, int rad, bool on, Color on_col) {
    Color col = on ? on_col : COL_DIM;
    set_color(r, col);
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            if (dx * dx + dy * dy <= rad * rad)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
        }
    }
}

static void draw_btn(app_t *a, int x, int y, int w, int h, const char *t, int id, bool primary) {
    bool hov = (id >= 0 && a->hover_btn == id);
    Color bg = primary ? COL_BTN_HI : (hov ? COL_BTN_HOVER : COL_BTN);
    Color fg = primary ? (Color){255, 255, 255, 255} : COL_INPUT_FG;
    set_color(a->renderer, bg);
    fill_rect(a->renderer, x, y, w, h);
    set_color(a->renderer, COL_BORDER);
    draw_rect(a->renderer, x, y, w, h);
    draw_text(a->renderer, a->font_label, t, x + w / 2, y + (h - 12) / 2, fg, 1);
    if (id >= 0) add_button(a, x, y, w, h, id);
}

static void draw_channel_strip(app_t *a, int x, int y, int w, int h, bool register_hits) {
    psu_channel_state_t *st = &a->ch;
    set_color(a->renderer, COL_BG_STRIP);
    fill_rect(a->renderer, x, y, w, h);
    set_color(a->renderer, COL_BORDER);
    draw_rect(a->renderer, x, y, w, h);

    draw_text(a->renderer, a->font_label, "OUT", x + 6, y + 4, COL_DIM, 0);

    char buf_v[20], buf_a[20];
    snprintf(buf_v, sizeof(buf_v), "%05.2f", st->out_v);
    snprintf(buf_a, sizeof(buf_a), "%05.3f", st->out_a);

    int btn_y = y + (h - 24) / 2;
    int set_x = x + w - 88;
    int out_x = x + w - 44;

    int num_x = x + 42;
    draw_text(a->renderer, a->font_num, buf_v, num_x, y + 16, st->valid ? COL_V_BRIGHT : COL_DIM, 0);
    int vw = 0, vh = 0;
    TTF_SizeText(a->font_num, buf_v, &vw, &vh);
    draw_text(a->renderer, a->font_label, "V", num_x + vw + 3, y + 28, COL_V_BRIGHT, 0);

    int ax = num_x + vw + 22;
    draw_text(a->renderer, a->font_num, buf_a, ax, y + 16, st->valid ? COL_A_BRIGHT : COL_DIM, 0);
    int aw = 0;
    TTF_SizeText(a->font_num, buf_a, &aw, &vh);
    draw_text(a->renderer, a->font_label, "A", ax + aw + 3, y + 28, COL_A_BRIGHT, 0);

    int stat_x = x + w - 196;
    if (stat_x < ax + aw + 50)
        stat_x = ax + aw + 50;
    const char *out_s = st->out_on ? "ON" : "OFF";
    Color out_c = st->out_on ? COL_ON : COL_OFF;
    draw_text(a->renderer, a->font_stat, out_s, stat_x, y + 3, out_c, 0);
    int ow = 0;
    TTF_SizeText(a->font_stat, out_s, &ow, &vh);
    const char *mode_s = st->cv_mode ? "CV" : "CC";
    Color mode_c = st->cv_mode ? COL_CV : COL_CC;
    draw_text(a->renderer, a->font_stat, mode_s, stat_x + ow + 12, y + 6, mode_c, 0);

    if (!st->valid) {
        draw_text(a->renderer, a->font_label, "ERR", stat_x + ow + 54, y + 7, COL_ERR, 0);
    }

    if (register_hits) {
        draw_btn(a, set_x, btn_y, 40, 24, "SET", BTN_CH_SET, false);
        draw_btn(a, out_x, btn_y, 36, 24, "OUT", BTN_CH_OUT, st->out_on);
    } else {
        draw_btn(a, set_x, btn_y, 40, 24, "SET", -1, false);
        draw_btn(a, out_x, btn_y, 36, 24, "OUT", -1, st->out_on);
    }
}

static void popup_close(app_t *a) {
    if (!a->popup_open) return;
    a->popup_open = false;
    SDL_SetWindowSize(a->window, WIN_W, WIN_H_STRIP);
}

static void popup_open_for(app_t *a) {
    a->popup_open = true;
    a->popup_focus = 0;
    snprintf(a->popup_v, sizeof(a->popup_v), "%.2f", a->ch.set_v);
    snprintf(a->popup_a, sizeof(a->popup_a), "%.3f", a->ch.set_a);
    SDL_SetWindowSize(a->window, WIN_W, WIN_H_POPUP);
}

static void popup_apply(app_t *a) {
    float fv = (float)atof(a->popup_v);
    float fa = (float)atof(a->popup_a);

    if (fv >= 0.0f && fv <= a->drv->caps.v_max)
        a->drv->set_voltage(a->drv, 1, fv);
    if (fa >= 0.0f && fa <= a->drv->caps.i_max)
        a->drv->set_current(a->drv, 1, fa);
    popup_close(a);
}

static void popup_get_panel_rect(int winw, SDL_Rect *r) {
    r->w = POPUP_PW;
    r->h = POPUP_PH;
    r->x = (winw - r->w) / 2;
    r->y = STRIP_BOTTOM_Y + POPUP_BELOW_STRIP;
}

static void draw_popup(app_t *a) {
    if (!a->popup_open) return;

    int winw = WIN_W, winh = WIN_H_POPUP;
    SDL_GetWindowSize(a->window, &winw, &winh);

    if (winh > STRIP_BOTTOM_Y) {
        set_color(a->renderer, (Color){0, 0, 0, 170});
        fill_rect(a->renderer, 0, STRIP_BOTTOM_Y, winw, winh - STRIP_BOTTOM_Y);
    }

    SDL_Rect pr;
    popup_get_panel_rect(winw, &pr);
    int px = pr.x, py = pr.y, pw = pr.w, ph = pr.h;

    set_color(a->renderer, COL_POPUP_BG);
    fill_rect(a->renderer, px, py, pw, ph);
    set_color(a->renderer, COL_FOCUS);
    draw_rect(a->renderer, px, py, pw, ph);
    draw_rect(a->renderer, px + 1, py + 1, pw - 2, ph - 2);

    draw_text(a->renderer, a->font_pop, "SET OUTPUT", px + 14, py + 12, COL_INPUT_FG, 0);

    int fw = pw - 28;
    int fh = 30;
    int field_gap = 18;
    int fy_v = py + 44;
    int fy_a = fy_v + fh + field_gap;

    bool fv = (a->popup_focus == 0);
    bool fa = (a->popup_focus == 1);
    set_color(a->renderer, COL_INPUT_BG);
    fill_rect(a->renderer, px + 14, fy_v, fw, fh);
    set_color(a->renderer, fv ? COL_FOCUS : COL_BORDER);
    draw_rect(a->renderer, px + 14, fy_v, fw, fh);
    draw_text(a->renderer, a->font_pop, "V", px + 14, fy_v - 14, COL_DIM, 0);
    draw_text(a->renderer, a->font_pop, a->popup_v, px + 22, fy_v + 6, COL_V_BRIGHT, 0);
    add_button(a, px + 14, fy_v, fw, fh, BTN_POP_FIELD_V);

    fill_rect(a->renderer, px + 14, fy_a, fw, fh);
    set_color(a->renderer, fa ? COL_FOCUS : COL_BORDER);
    draw_rect(a->renderer, px + 14, fy_a, fw, fh);
    draw_text(a->renderer, a->font_pop, "A", px + 14, fy_a - 14, COL_DIM, 0);
    draw_text(a->renderer, a->font_pop, a->popup_a, px + 22, fy_a + 6, COL_A_BRIGHT, 0);
    add_button(a, px + 14, fy_a, fw, fh, BTN_POP_FIELD_A);

    int by = fy_a + fh + 18;
    draw_btn(a, px + 14, by, 130, 30, "APPLY", BTN_POP_APPLY, true);
    draw_btn(a, px + pw - 144, by, 130, 30, "CANCEL", BTN_POP_CANCEL, false);
}

static void render(app_t *a) {
    SDL_Renderer *r = a->renderer;
    int winw = WIN_W, winh = WIN_H_STRIP;
    SDL_GetWindowSize(a->window, &winw, &winh);

    set_color(r, COL_BG_DARK);
    SDL_RenderClear(r);
    a->num_buttons = 0;

    set_color(a->renderer, COL_HEADER);
    fill_rect(a->renderer, 0, 0, winw, HEADER_H);
    set_color(a->renderer, COL_BORDER);
    SDL_RenderDrawLine(a->renderer, 0, HEADER_H - 1, winw, HEADER_H - 1);

    draw_text(a->renderer, a->font_label, "PSU", 8, 5, COL_DIM, 0);

    bool ok = a->drv->is_connected(a->drv);
    Color dot = ok ? COL_ON : COL_ERR;
    draw_led(a->renderer, winw - 12, HEADER_H / 2, 4, true, dot);

    int y0 = STRIP_TOP_Y;
    int gap = 6;
    int col_w = winw - gap * 2;
    bool hits = !a->popup_open;
    draw_channel_strip(a, gap, y0, col_w, STRIP_BLOCK_H, hits);

    draw_popup(a);

    SDL_RenderPresent(r);
}

static bool point_in_popup_backdrop(app_t *a, int mx, int my) {
    if (!a->popup_open) return false;
    int winw = WIN_W;
    SDL_GetWindowSize(a->window, &winw, NULL);
    SDL_Rect pr;
    popup_get_panel_rect(winw, &pr);
    return mx < pr.x || mx >= pr.x + pr.w || my < pr.y || my >= pr.y + pr.h;
}

static void handle_click(app_t *a, int mx, int my) {
    if (a->popup_open) {
        if (point_in_popup_backdrop(a, mx, my)) {
            popup_close(a);
            return;
        }
        int b = button_at(a, mx, my);
        switch (b) {
            case BTN_POP_FIELD_V: a->popup_focus = 0; return;
            case BTN_POP_FIELD_A: a->popup_focus = 1; return;
            case BTN_POP_APPLY:   popup_apply(a); return;
            case BTN_POP_CANCEL:  popup_close(a); return;
            default: return;
        }
    }

    int btn = button_at(a, mx, my);
    switch (btn) {
        case BTN_CH_SET: popup_open_for(a); break;
        case BTN_CH_OUT: a->drv->set_output(a->drv, 1, !a->ch.out_on); break;
        default: break;
    }
}

static void append_to_popup(app_t *a, const char *text) {
    for (const char *p = text; *p; p++) {
        if ((*p < '0' || *p > '9') && *p != '.') continue;
        char *buf = (a->popup_focus == 0) ? a->popup_v : a->popup_a;
        size_t len = strlen(buf);
        if (len >= sizeof(a->popup_v) - 1) continue;
        if (*p == '.' && strchr(buf, '.') != NULL) continue;
        buf[len] = *p;
        buf[len + 1] = '\0';
    }
}

static void handle_key(app_t *a, SDL_Keycode key) {
    if (!a->popup_open) return;
    char *buf = (a->popup_focus == 0) ? a->popup_v : a->popup_a;
    size_t len = strlen(buf);

    if (key == SDLK_TAB) {
        a->popup_focus = 1 - a->popup_focus;
        return;
    }
    if (key == SDLK_BACKSPACE && len > 0) {
        buf[len - 1] = '\0';
        return;
    }
    if (key == SDLK_ESCAPE) {
        popup_close(a);
        return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        popup_apply(a);
        return;
    }
}

static bool open_fonts(app_t *a) {
    const char *path = pl_find_monospace_font();
    if (!path) {
        fprintf(stderr, "toolbar_single: no monospace TTF available on this system\n");
        return false;
    }

    a->font_label = TTF_OpenFont(path, 12);
    a->font_num   = TTF_OpenFont(path, 28);
    a->font_stat  = TTF_OpenFont(path, 16);
    a->font_pop   = TTF_OpenFont(path, 15);
    return a->font_label && a->font_num && a->font_stat && a->font_pop;
}

static void cleanup(app_t *a) {
    if (a->font_label) TTF_CloseFont(a->font_label);
    if (a->font_num)   TTF_CloseFont(a->font_num);
    if (a->font_stat)  TTF_CloseFont(a->font_stat);
    if (a->font_pop)   TTF_CloseFont(a->font_pop);
    if (a->renderer)   SDL_DestroyRenderer(a->renderer);
    if (a->window)     SDL_DestroyWindow(a->window);
    TTF_Quit();
    SDL_Quit();
}

int view_toolbar_single_run(psu_driver_t *drv) {
    if (!drv) return 1;

    app_t a;
    memset(&a, 0, sizeof(a));
    a.drv = drv;
    a.running = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }

    a.window = SDL_CreateWindow("Open LabBench — PSU",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIN_W, WIN_H_STRIP, SDL_WINDOW_SHOWN);
    if (!a.window) { cleanup(&a); return 1; }

    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, 0);
    if (!a.renderer) { cleanup(&a); return 1; }
    SDL_SetRenderDrawBlendMode(a.renderer, SDL_BLENDMODE_BLEND);

    if (!open_fonts(&a)) { cleanup(&a); return 1; }

    SDL_StartTextInput();

    while (a.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    a.running = false;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        handle_click(&a, ev.button.x, ev.button.y);
                    break;
                case SDL_MOUSEMOTION:
                    a.hover_btn = button_at(&a, ev.motion.x, ev.motion.y);
                    break;
                case SDL_KEYDOWN:
                    handle_key(&a, ev.key.keysym.sym);
                    break;
                case SDL_TEXTINPUT:
                    if (a.popup_open) append_to_popup(&a, ev.text.text);
                    break;
            }
        }

        a.drv->get_channel(a.drv, 1, &a.ch);
        render(&a);
        SDL_Delay(12);
    }

    SDL_StopTextInput();
    cleanup(&a);
    return 0;
}
