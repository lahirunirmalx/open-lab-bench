/**
 * Full GUI — dual channel.
 *
 * Layout: header bar | system-control toolbar (TRACKING) | [CH1 panel] [CH2
 * panel] [shared keypad].
 *
 * Most of the heavy lifting (VFD, bar meters, scope, channel panel, keypad
 * helpers, input/key handling) lives in full_common.c; this file owns the
 * specific layout, the dual-channel toolbar, the dual-channel keypad, and
 * the main event loop.
 */

#include "full_common.h"
#include "views.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W      1240
#define WIN_H       720
#define HEADER_H     40
#define TOOLBAR_H    36
#define PANEL_W     480
#define PANEL_H     600
#define PANEL_GAP    12
#define KEYPAD_W    180

static void draw_header(full_ctx_t *c) {
    SDL_Renderer *r = c->renderer;
    full_set_color(r, COL_HEADER);
    full_fill_rect(r, 0, 0, WIN_W, HEADER_H);
    full_set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, WIN_W, HEADER_H - 1);

    full_draw_text(c, c->font_title, "DUAL OUTPUT DC POWER SUPPLY",
                   20, 10, COL_TEXT, 0);

    full_set_color(r, COL_ACCENT);
    full_fill_rounded(r, 420, 10, 70, 24, 3);
    char range[32];
    snprintf(range, sizeof(range), "%.0fV/%.0fA",
             c->drv->caps.v_max, c->drv->caps.i_max);
    full_draw_text_centered(c, c->font_small, range, 455, 22,
                            (full_color_t){255, 255, 255, 255});

    bool connected = c->drv->is_connected(c->drv);
    full_color_t status_col = connected ? COL_SUCCESS : COL_ERROR;
    const char *status_txt = connected ? "ONLINE" : "OFFLINE";
    full_draw_led(r, WIN_W - 100, 22, 5, true, status_col);
    full_draw_text(c, c->font_medium, status_txt, WIN_W - 88, 14, status_col, 0);

    char stats[64] = {0};
    if (c->drv->get_stats) {
        uint32_t rx = 0, err = 0;
        c->drv->get_stats(c->drv, &rx, &err);
        snprintf(stats, sizeof(stats), "FPS:%d RX:%u", c->fps, rx);
    } else {
        snprintf(stats, sizeof(stats), "FPS:%d", c->fps);
    }
    full_draw_text(c, c->font_small, stats, WIN_W - 200, 30, COL_TEXT_DIM, 0);
}

static void draw_toolbar(full_ctx_t *c) {
    SDL_Renderer *r = c->renderer;
    int y = HEADER_H;
    full_set_color(r, COL_BG_WIDGET);
    full_fill_rect(r, 0, y, WIN_W, TOOLBAR_H);
    full_set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, y + TOOLBAR_H - 1, WIN_W, y + TOOLBAR_H - 1);

    full_draw_text(c, c->font_small, "SYSTEM CONTROL", 20, y + 12, COL_LABEL, 0);

    bool tracking_supported = (c->drv->set_tracking != NULL);
    full_draw_button(c, 140, y + 8, 100, 26, "TRACKING",
                     c->tracking,
                     tracking_supported && c->hover_btn == FULL_BTN_TRACKING,
                     tracking_supported ? FULL_BTN_TRACKING : -1);
    full_draw_led(r, 250, y + 21, 5, c->tracking, COL_SUCCESS);

    full_draw_button(c, WIN_W - 100, y + 8, 80, 26, "REFRESH",
                     false, c->hover_btn == FULL_BTN_REFRESH, FULL_BTN_REFRESH);
}

/* Dual-channel keypad: CH1/CH2 toggle, V/A toggle, 0-9 . C < OK */
static void draw_keypad(full_ctx_t *c, int x, int y, int w, int h) {
    int margin = 8;
    int inner_w = w - margin * 2;
    SDL_Renderer *r = c->renderer;

    full_set_color(r, COL_BG_PANEL);
    full_fill_rounded(r, x, y, w, h, 4);
    full_set_color(r, COL_BORDER);
    full_draw_rect(r, x, y, w, h);

    full_set_color(r, COL_BG_WIDGET);
    full_fill_rect(r, x + 1, y + 1, w - 2, 30);
    full_draw_text(c, c->font_medium, "KEYPAD", x + margin, y + 8, COL_TEXT, 0);

    int cur_y = y + 38;

    const char *ch_label = (c->keypad_channel == 0) ? "CH1" : "CH2";
    full_color_t ch_col  = (c->keypad_channel == 0) ? COL_SUCCESS : COL_ACCENT;
    int tog_gap = 6;
    int tog_btn_w = (inner_w - tog_gap) / 2;
    full_draw_keypad_btn(c, x + margin, cur_y, tog_btn_w, 28,
                         ch_label, false, FULL_BTN_KEY_CH_TOG);
    full_draw_led(r, x + margin + tog_btn_w - 8, cur_y + 14, 4, true, ch_col);

    const char *mode_label = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) ? "VOLTS" : "AMPS";
    full_color_t mode_col  = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE)
                             ? (full_color_t){100, 200, 100, 255}
                             : (full_color_t){255, 200, 100, 255};
    full_draw_keypad_btn(c, x + margin + tog_btn_w + tog_gap, cur_y, tog_btn_w, 28,
                         mode_label, false, FULL_BTN_KEY_MODE);

    cur_y += 42;

    int disp_h = 42;
    full_set_color(r, (full_color_t){10, 15, 12, 255});
    full_fill_rect(r, x + margin, cur_y, inner_w, disp_h);
    full_set_color(r, COL_BORDER);
    full_draw_rect(r, x + margin, cur_y, inner_w, disp_h);

    const char *disp_val = c->keypad_value;
    if (!disp_val[0]) disp_val = "0.00";
    full_color_t val_col = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE)
                           ? COL_VFD_ON : (full_color_t){255, 200, 100, 255};
    full_draw_text(c, c->font_large, disp_val,
                   x + w - margin - 8, cur_y + 8, val_col, 2);

    const char *unit = (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) ? "V" : "A";
    full_draw_text(c, c->font_medium, unit, x + margin + 5, cur_y + 12, mode_col, 0);

    cur_y += disp_h + 6;

    char target[32];
    snprintf(target, sizeof(target), "-> CH%d %s", c->keypad_channel + 1,
             (c->keypad_mode == FULL_KEYPAD_MODE_VOLTAGE) ? "Vset" : "Iset");
    full_draw_text(c, c->font_small, target, x + margin, cur_y, COL_TEXT_DIM, 0);
    cur_y += 22;

    int bottom_info_h = 70;
    int avail_h = (y + h) - cur_y - bottom_info_h - margin;
    int btn_rows = 4, btn_gap = 6;
    int btn_h = (avail_h - (btn_rows - 1) * btn_gap) / btn_rows;
    if (btn_h < 36) btn_h = 36;
    if (btn_h > 52) btn_h = 52;

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
    full_draw_keypad_btn(c, pad_x,                       cur_y, wide_btn, btn_h, "0", false, FULL_BTN_KEY_0);
    full_draw_keypad_btn(c, pad_x + wide_btn + btn_gap,  cur_y, btn_w,    btn_h, ".", false, FULL_BTN_KEY_DOT);

    cur_y += btn_h + 12;
    full_draw_text(c, c->font_small, "SETPOINTS:", x + margin, cur_y, COL_LABEL, 0);
    cur_y += 18;

    char val_str[48];
    psu_channel_state_t *st1 = &c->ch[0].state;
    snprintf(val_str, sizeof(val_str), "CH1: %.6fV / %.6fA", st1->set_v, st1->set_a);
    full_color_t c1 = (c->keypad_channel == 0) ? COL_SUCCESS : COL_TEXT_DIM;
    full_draw_text(c, c->font_small, val_str, x + margin, cur_y, c1, 0);

    cur_y += 16;
    psu_channel_state_t *st2 = &c->ch[1].state;
    snprintf(val_str, sizeof(val_str), "CH2: %.6fV / %.6fA", st2->set_v, st2->set_a);
    full_color_t c2 = (c->keypad_channel == 1) ? COL_ACCENT : COL_TEXT_DIM;
    full_draw_text(c, c->font_small, val_str, x + margin, cur_y, c2, 0);
}

static void render(full_ctx_t *c) {
    full_set_color(c->renderer, COL_BG_DARK);
    SDL_RenderClear(c->renderer);
    c->num_buttons = 0;
    memset(c->inputs, 0, sizeof(c->inputs));

    draw_header(c);
    draw_toolbar(c);

    int panels_y = HEADER_H + TOOLBAR_H + 10;
    int avail_h  = WIN_H - panels_y - 10;
    int total_w  = PANEL_W * 2 + PANEL_GAP + KEYPAD_W + PANEL_GAP;
    int start_x  = (WIN_W - total_w) / 2;

    full_draw_channel_panel(c, 0, start_x,                       panels_y, PANEL_W, PANEL_H);
    full_draw_channel_panel(c, 1, start_x + PANEL_W + PANEL_GAP, panels_y, PANEL_W, PANEL_H);

    int keypad_x = start_x + PANEL_W * 2 + PANEL_GAP * 2;
    draw_keypad(c, keypad_x, panels_y, KEYPAD_W, avail_h);

    SDL_RenderPresent(c->renderer);
}

static void handle_click(full_ctx_t *c, int mx, int my) {
    if (full_try_click_input_field(c, mx, my)) return;
    int btn = full_button_at(c, mx, my);
    if (btn == 0) { c->active_input = 0; return; }
    if (full_handle_panel_button(c, btn)) return;
    full_handle_keypad_button(c, btn);
}

int view_full_dual_run(psu_driver_t *drv) {
    if (!drv) return 1;
    if (drv->caps.n_channels < 2) return 2;

    full_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.drv             = drv;
    c.running         = true;
    c.keypad_channel  = 0;
    c.keypad_mode     = FULL_KEYPAD_MODE_VOLTAGE;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }

    c.window = SDL_CreateWindow("Open LabBench — PSU",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!c.window) { TTF_Quit(); SDL_Quit(); return 1; }

    c.renderer = SDL_CreateRenderer(c.window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!c.renderer) {
        SDL_DestroyWindow(c.window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(c.renderer, SDL_BLENDMODE_BLEND);

    if (!full_open_fonts(&c)) {
        SDL_DestroyRenderer(c.renderer);
        SDL_DestroyWindow(c.window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    SDL_StartTextInput();
    c.last_fps_time = SDL_GetTicks();

    while (c.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: c.running = false; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT)
                        handle_click(&c, ev.button.x, ev.button.y);
                    break;
                case SDL_MOUSEMOTION:
                    c.hover_btn = full_button_at(&c, ev.motion.x, ev.motion.y);
                    break;
                case SDL_KEYDOWN: full_handle_key(&c, ev.key.keysym.sym); break;
                case SDL_TEXTINPUT: full_handle_text(&c, ev.text.text); break;
            }
        }

        full_update_from_driver(&c);
        render(&c);
        full_tick_fps(&c);
        SDL_Delay(8);
    }

    SDL_StopTextInput();
    full_close_fonts(&c);
    SDL_DestroyRenderer(c.renderer);
    SDL_DestroyWindow(c.window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
