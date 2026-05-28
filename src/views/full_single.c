/**
 * Full GUI — single channel.
 *
 * Compact 1-channel variant: title "DC POWER SUPPLY", CONTROL toolbar (no
 * TRACKING), one OUTPUT panel, collapsible keypad on the right. The window
 * resizes when the keypad is hidden/shown.
 *
 * Shares everything heavy with full_common.c — only the layout, the
 * toolbar/header, and the keypad's narrow layout are local.
 */

#include "full_common.h"
#include "views.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_H              698
#define HEADER_H            40
#define TOOLBAR_H           36
#define PANEL_W            480
#define PANEL_H            600
#define KEYPAD_W           180
#define KEYPAD_COLLAPSED_W  40
#define SINGLE_MARGIN_X     10
#define SINGLE_GAP           6

/* Special internal button id for the keypad show/hide tab. */
#define BTN_SINGLE_KEYPAD_TOGGLE 200

typedef struct {
    full_ctx_t  c;
    bool        keypad_expanded;
} single_app_t;

static int single_window_width(const single_app_t *a) {
    int kw = a->keypad_expanded ? KEYPAD_W : KEYPAD_COLLAPSED_W;
    return SINGLE_MARGIN_X + PANEL_W + SINGLE_GAP + kw + SINGLE_MARGIN_X;
}

static void single_sync_window_size(single_app_t *a) {
    if (!a->c.window) return;
    int w = single_window_width(a);
    int cur_w = 0, cur_h = 0;
    SDL_GetWindowSize(a->c.window, &cur_w, &cur_h);
    if (cur_w != w || cur_h != WIN_H)
        SDL_SetWindowSize(a->c.window, w, WIN_H);
}

static void draw_header(single_app_t *a) {
    full_ctx_t *c = &a->c;
    SDL_Renderer *r = c->renderer;
    int win_w = single_window_width(a);

    full_set_color(r, COL_HEADER);
    full_fill_rect(r, 0, 0, win_w, HEADER_H);
    full_set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, win_w, HEADER_H - 1);

    full_draw_text(c, c->font_title, "DC POWER SUPPLY",
                   SINGLE_MARGIN_X, 10, COL_TEXT, 0);

    full_set_color(r, COL_ACCENT);
    full_fill_rounded(r, 200, 10, 70, 24, 3);
    char range[32];
    snprintf(range, sizeof(range), "%.0fV/%.0fA",
             c->drv->caps.v_max, c->drv->caps.i_max);
    full_draw_text_centered(c, c->font_small, range, 235, 22,
                            (full_color_t){255, 255, 255, 255});

    bool connected = c->drv->is_connected(c->drv);
    full_color_t status_col = connected ? COL_SUCCESS : COL_ERROR;
    const char *status_txt = connected ? "ONLINE" : "OFFLINE";
    full_draw_led(r, win_w - 92, 22, 5, true, status_col);
    full_draw_text(c, c->font_medium, status_txt, win_w - 80, 14, status_col, 0);

    char stats[64] = {0};
    if (c->drv->get_stats) {
        uint32_t rx = 0, err = 0;
        c->drv->get_stats(c->drv, &rx, &err);
        snprintf(stats, sizeof(stats), "FPS:%d RX:%u", c->fps, rx);
    } else {
        snprintf(stats, sizeof(stats), "FPS:%d", c->fps);
    }
    full_draw_text(c, c->font_small, stats, win_w - 168, 30, COL_TEXT_DIM, 0);
}

static void draw_toolbar(single_app_t *a) {
    full_ctx_t *c = &a->c;
    SDL_Renderer *r = c->renderer;
    int win_w = single_window_width(a);
    int y = HEADER_H;
    full_set_color(r, COL_BG_WIDGET);
    full_fill_rect(r, 0, y, win_w, TOOLBAR_H);
    full_set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, y + TOOLBAR_H - 1, win_w, y + TOOLBAR_H - 1);

    full_draw_text(c, c->font_small, "CONTROL", SINGLE_MARGIN_X, y + 12, COL_LABEL, 0);
    full_draw_button(c, win_w - 88, y + 8, 78, 26, "REFRESH",
                     false, c->hover_btn == FULL_BTN_REFRESH, FULL_BTN_REFRESH);
}

static void draw_keypad_toggle_tab(single_app_t *a, int x, int y, int w, int h,
                                   bool expanded) {
    full_ctx_t *c = &a->c;
    full_set_color(c->renderer, COL_BG_PANEL);
    full_fill_rounded(c->renderer, x, y, w, h, 4);
    full_set_color(c->renderer, COL_BORDER);
    full_draw_rect(c->renderer, x, y, w, h);
    const char *lbl = expanded ? "<" : ">";
    full_draw_text_centered(c, c->font_medium, lbl, x + w / 2, y + h / 2, COL_TEXT);
    full_add_button(c, x, y, w, h, BTN_SINGLE_KEYPAD_TOGGLE);
}

/* Narrow single-channel keypad: no CH toggle, just a wider mode toggle. */
static void draw_keypad(single_app_t *a, int x, int y, int w, int h) {
    full_ctx_t *c = &a->c;
    SDL_Renderer *r = c->renderer;
    int margin = 6;
    int inner_w = w - margin * 2;

    full_set_color(r, COL_BG_PANEL);
    full_fill_rounded(r, x, y, w, h, 4);
    full_set_color(r, COL_BORDER);
    full_draw_rect(r, x, y, w, h);

    int tab_h = 26;
    int body_y = y + tab_h;
    draw_keypad_toggle_tab(a, x, y, w, tab_h, true);

    full_set_color(r, COL_BG_WIDGET);
    full_fill_rect(r, x + 1, body_y + 1, w - 2, 26);
    full_draw_text(c, c->font_medium, "KEYPAD", x + margin, body_y + 6, COL_TEXT, 0);

    int cur_y = body_y + 32;
    const char *mode_label = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) ? "VOLTS" : "AMPS";
    full_color_t mode_col  = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE)
                             ? (full_color_t){100, 200, 100, 255}
                             : (full_color_t){255, 200, 100, 255};
    full_draw_keypad_btn(c, x + margin, cur_y, inner_w, 26,
                         mode_label, false, FULL_BTN_KEY_MODE);
    cur_y += 34;

    int disp_h = 36;
    full_set_color(r, (full_color_t){10, 15, 12, 255});
    full_fill_rect(r, x + margin, cur_y, inner_w, disp_h);
    full_set_color(r, COL_BORDER);
    full_draw_rect(r, x + margin, cur_y, inner_w, disp_h);

    const char *disp_val = c->keypad_value[0] ? c->keypad_value : "0.00";
    full_color_t val_col = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE)
                           ? COL_VFD_ON : (full_color_t){255, 200, 100, 255};
    full_draw_text(c, c->font_large, disp_val, x + w - margin - 6, cur_y + 6, val_col, 2);

    const char *unit = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) ? "V" : "A";
    full_draw_text(c, c->font_medium, unit, x + margin + 4, cur_y + 8, mode_col, 0);
    cur_y += disp_h + 4;

    full_draw_text(c, c->font_small,
                   (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) ? "Vset" : "Iset",
                   x + margin, cur_y, COL_TEXT_DIM, 0);
    cur_y += 18;

    int bottom_info_h = 58;
    int avail_h = (y + h) - cur_y - bottom_info_h - margin;
    int btn_rows = 4, btn_gap = 4;
    int btn_h = (avail_h - (btn_rows - 1) * btn_gap) / btn_rows;
    if (btn_h < 32) btn_h = 32;
    if (btn_h > 48) btn_h = 48;

    int btn_cols = 4;
    int btn_w = (inner_w - (btn_cols - 1) * btn_gap) / btn_cols;
    int pad_x = x + margin;

    full_draw_keypad_btn(c, pad_x + 0 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "7", false, FULL_BTN_KEY_7);
    full_draw_keypad_btn(c, pad_x + 1 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "8", false, FULL_BTN_KEY_8);
    full_draw_keypad_btn(c, pad_x + 2 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "9", false, FULL_BTN_KEY_9);
    full_draw_keypad_btn(c, pad_x + 3 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "C", false, FULL_BTN_KEY_CLR);
    cur_y += btn_h + btn_gap;
    full_draw_keypad_btn(c, pad_x + 0 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "4", false, FULL_BTN_KEY_4);
    full_draw_keypad_btn(c, pad_x + 1 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "5", false, FULL_BTN_KEY_5);
    full_draw_keypad_btn(c, pad_x + 2 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "6", false, FULL_BTN_KEY_6);
    full_draw_keypad_btn(c, pad_x + 3 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "<", false, FULL_BTN_KEY_BACK);
    cur_y += btn_h + btn_gap;
    full_draw_keypad_btn(c, pad_x + 0 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "1", false, FULL_BTN_KEY_1);
    full_draw_keypad_btn(c, pad_x + 1 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "2", false, FULL_BTN_KEY_2);
    full_draw_keypad_btn(c, pad_x + 2 * (btn_w + btn_gap), cur_y, btn_w, btn_h, "3", false, FULL_BTN_KEY_3);

    int enter_h = btn_h * 2 + btn_gap;
    full_draw_keypad_btn(c, pad_x + 3 * (btn_w + btn_gap), cur_y, btn_w, enter_h, "OK", true, FULL_BTN_KEY_ENTER);
    cur_y += btn_h + btn_gap;
    int wide_btn = btn_w * 2 + btn_gap;
    full_draw_keypad_btn(c, pad_x,                      cur_y, wide_btn, btn_h, "0", false, FULL_BTN_KEY_0);
    full_draw_keypad_btn(c, pad_x + wide_btn + btn_gap, cur_y, btn_w,    btn_h, ".", false, FULL_BTN_KEY_DOT);
    cur_y += btn_h + 8;

    full_draw_text(c, c->font_small, "SETPOINTS:", x + margin, cur_y, COL_LABEL, 0);
    cur_y += 16;
    char val_str[48];
    psu_channel_state_t *st1 = &c->ch[0].state;
    snprintf(val_str, sizeof(val_str), "%.6fV / %.6fA", st1->set_v, st1->set_a);
    full_draw_text(c, c->font_small, val_str, x + margin, cur_y, COL_SUCCESS, 0);
}

static void draw_keypad_collapsed(single_app_t *a, int x, int y, int w, int h) {
    draw_keypad_toggle_tab(a, x, y, w, h, false);
}

static void render(single_app_t *a) {
    full_ctx_t *c = &a->c;
    full_set_color(c->renderer, COL_BG_DARK);
    SDL_RenderClear(c->renderer);
    c->num_buttons = 0;
    memset(c->inputs, 0, sizeof(c->inputs));

    draw_header(a);
    draw_toolbar(a);

    int panels_y = HEADER_H + TOOLBAR_H + 4;
    int avail_h  = WIN_H - panels_y - 6;
    int start_x  = SINGLE_MARGIN_X;
    int keypad_x = start_x + PANEL_W + SINGLE_GAP;

    full_draw_channel_panel(c, 0, start_x, panels_y, PANEL_W, PANEL_H);

    if (a->keypad_expanded)
        draw_keypad(a, keypad_x, panels_y, KEYPAD_W, avail_h);
    else
        draw_keypad_collapsed(a, keypad_x, panels_y, KEYPAD_COLLAPSED_W, avail_h);

    SDL_RenderPresent(c->renderer);
}

static void handle_click(single_app_t *a, int mx, int my) {
    full_ctx_t *c = &a->c;
    int hit = full_button_at(c, mx, my);
    if (hit == BTN_SINGLE_KEYPAD_TOGGLE) {
        a->keypad_expanded = !a->keypad_expanded;
        single_sync_window_size(a);
        return;
    }

    if (full_try_click_input_field(c, mx, my)) return;
    if (hit == 0) { c->active_input = 0; return; }
    if (full_handle_panel_button(c, hit)) return;
    full_handle_keypad_button(c, hit);
}

int view_full_single_run(psu_driver_t *drv) {
    if (!drv) return 1;

    single_app_t a;
    memset(&a, 0, sizeof(a));
    a.c.drv             = drv;
    a.c.running         = true;
    a.c.keypad_channel  = 0;        /* single-channel view always drives ch1 */
    a.c.keypad_mode     = FULL_KEYPAD_MODE_VOLTAGE;
    a.keypad_expanded   = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }

    int initial_w = single_window_width(&a);
    a.c.window = SDL_CreateWindow("PSU Control",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  initial_w, WIN_H, SDL_WINDOW_SHOWN);
    if (!a.c.window) { TTF_Quit(); SDL_Quit(); return 1; }

    a.c.renderer = SDL_CreateRenderer(a.c.window, -1,
                                      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a.c.renderer) {
        SDL_DestroyWindow(a.c.window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(a.c.renderer, SDL_BLENDMODE_BLEND);

    if (!full_open_fonts(&a.c)) {
        SDL_DestroyRenderer(a.c.renderer);
        SDL_DestroyWindow(a.c.window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    SDL_StartTextInput();
    a.c.last_fps_time = SDL_GetTicks();

    while (a.c.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: a.c.running = false; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        handle_click(&a, ev.button.x, ev.button.y);
                    a.c.keypad_channel = 0;
                    break;
                case SDL_MOUSEMOTION:
                    a.c.hover_btn = full_button_at(&a.c, ev.motion.x, ev.motion.y);
                    break;
                case SDL_KEYDOWN:
                    full_handle_key(&a.c, ev.key.keysym.sym);
                    a.c.keypad_channel = 0;
                    break;
                case SDL_TEXTINPUT: full_handle_text(&a.c, ev.text.text); break;
            }
        }

        full_update_from_driver(&a.c);
        a.c.keypad_channel = 0;
        render(&a);
        full_tick_fps(&a.c);
        SDL_Delay(8);
    }

    SDL_StopTextInput();
    full_close_fonts(&a.c);
    SDL_DestroyRenderer(a.c.renderer);
    SDL_DestroyWindow(a.c.window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
