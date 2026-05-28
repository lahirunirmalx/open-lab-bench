/**
 * Shared "full GUI" toolkit — VFD readouts, bar meters, temperature gauge,
 * dual-trace scope, keypad widgets, input fields. Used by views/full_dual.c
 * and views/full_single.c.
 *
 * Everything lives on a per-instance full_ctx_t (no file-scope state); each
 * view owns its own context.
 */

#ifndef VIEWS_FULL_COMMON_H
#define VIEWS_FULL_COMMON_H

#include "psu_driver.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <stdint.h>

#define FULL_TRACE_LEN  150
#define FULL_MAX_BTNS   80

#define FULL_KEYPAD_MODE_VOLTAGE 0
#define FULL_KEYPAD_MODE_CURRENT 1

typedef struct { Uint8 r, g, b, a; } full_color_t;

typedef struct {
    float voltage[FULL_TRACE_LEN];
    float current[FULL_TRACE_LEN];
    int   head;
    int   count;
} full_trace_t;

typedef struct {
    psu_channel_state_t state;      /* snapshot from driver (SI units) */
    full_trace_t        trace;
    /* Cached display values — prevent jumps when output toggles. */
    float disp_v;
    float disp_a;
    float disp_p;
} full_channel_t;

typedef struct {
    SDL_Rect rect;
    int      id;
} full_button_t;

typedef struct {
    psu_driver_t *drv;

    SDL_Window   *window;
    SDL_Renderer *renderer;

    TTF_Font *font_title;
    TTF_Font *font_large;
    TTF_Font *font_medium;
    TTF_Font *font_small;
    TTF_Font *font_vfd;
    TTF_Font *font_vfd_small;

    full_channel_t ch[2];           /* full_single only uses ch[0] */

    bool tracking;
    bool running;

    int  active_input;              /* 0 = none; 1..4 = input fields (V/A per channel) */
    char input_buf[16];
    int  hover_btn;

    int  keypad_channel;
    int  keypad_mode;
    char keypad_value[16];

    uint32_t frame_count;
    uint32_t last_fps_time;
    int      fps;

    full_button_t buttons[FULL_MAX_BTNS];
    int           num_buttons;
    SDL_Rect      inputs[4];        /* hit rects for input fields */
} full_ctx_t;

/* ---- button IDs (shared across views) ---- */

enum {
    FULL_BTN_TRACKING    = 1,
    FULL_BTN_REFRESH     = 2,

    FULL_BTN_CH1_OUTPUT  = 10,
    FULL_BTN_CH1_SET_V   = 11,
    FULL_BTN_CH1_SET_A   = 12,
    FULL_BTN_CH2_OUTPUT  = 20,
    FULL_BTN_CH2_SET_V   = 21,
    FULL_BTN_CH2_SET_A   = 22,

    FULL_BTN_KEY_0       = 100,
    FULL_BTN_KEY_1       = 101,
    FULL_BTN_KEY_2       = 102,
    FULL_BTN_KEY_3       = 103,
    FULL_BTN_KEY_4       = 104,
    FULL_BTN_KEY_5       = 105,
    FULL_BTN_KEY_6       = 106,
    FULL_BTN_KEY_7       = 107,
    FULL_BTN_KEY_8       = 108,
    FULL_BTN_KEY_9       = 109,
    FULL_BTN_KEY_DOT     = 110,
    FULL_BTN_KEY_CLR     = 111,
    FULL_BTN_KEY_BACK    = 112,
    FULL_BTN_KEY_ENTER   = 113,
    FULL_BTN_KEY_CH_TOG  = 114,
    FULL_BTN_KEY_MODE    = 115,
};

/* ---- color palette ---- */

extern const full_color_t COL_BG_DARK;
extern const full_color_t COL_BG_PANEL;
extern const full_color_t COL_BG_WIDGET;
extern const full_color_t COL_HEADER;
extern const full_color_t COL_BORDER;
extern const full_color_t COL_BORDER_LIGHT;
extern const full_color_t COL_TEXT;
extern const full_color_t COL_TEXT_DIM;
extern const full_color_t COL_LABEL;
extern const full_color_t COL_ACCENT;
extern const full_color_t COL_SUCCESS;
extern const full_color_t COL_WARNING;
extern const full_color_t COL_ERROR;
extern const full_color_t COL_VFD_ON;
extern const full_color_t COL_BTN_NORMAL;
extern const full_color_t COL_BTN_HOVER;
extern const full_color_t COL_BTN_ACTIVE;

/* ---- low-level draw primitives (used by both views' headers/toolbars) ---- */

void full_set_color   (SDL_Renderer *r, full_color_t c);
void full_fill_rect   (SDL_Renderer *r, int x, int y, int w, int h);
void full_draw_rect   (SDL_Renderer *r, int x, int y, int w, int h);
void full_fill_rounded(SDL_Renderer *r, int x, int y, int w, int h, int radius);
void full_draw_text   (full_ctx_t *c, TTF_Font *font, const char *text,
                       int x, int y, full_color_t color, int align);
void full_draw_text_centered(full_ctx_t *c, TTF_Font *font, const char *text,
                             int cx, int cy, full_color_t color);
void full_draw_led    (SDL_Renderer *r, int cx, int cy, int radius, bool on,
                       full_color_t on_col);
void full_draw_button (full_ctx_t *c, int x, int y, int w, int h, const char *text,
                       bool active, bool hover, int id);
int  full_add_button  (full_ctx_t *c, int x, int y, int w, int h, int id);
int  full_button_at   (full_ctx_t *c, int mx, int my);
bool full_point_in_rect(int x, int y, const SDL_Rect *rect);

/* ---- composite widgets ---- */

/* Big channel panel: VFD readout + OUTPUT button + status + set V/A fields +
 * bar meters + temperature + scope. */
void full_draw_channel_panel(full_ctx_t *c, int ch_idx, int x, int y,
                             int panel_w, int panel_h);

/* Keypad cell button (rounded, hover, optional accent). */
void full_draw_keypad_btn(full_ctx_t *c, int x, int y, int w, int h,
                          const char *label, bool highlight, int id);

/* ---- state plumbing ---- */

/* Pull latest channel state from driver, update cached display values + trace. */
void full_update_from_driver(full_ctx_t *c);

/* FPS counter — call once per frame. */
void full_tick_fps(full_ctx_t *c);

/* ---- input handling helpers ---- */

/* Returns true if the click hit an input field (and made it active). */
bool full_try_click_input_field(full_ctx_t *c, int mx, int my);

/* Handle a CH/TRACKING/SET button click. Returns true if the id was handled. */
bool full_handle_panel_button(full_ctx_t *c, int btn);

/* Handle a keypad-button click (digit/dot/clr/back/enter/mode/ch-tog).
 * Returns true if handled. */
bool full_handle_keypad_button(full_ctx_t *c, int btn);

/* Keyboard / text input — common parts (input fields + keypad value editing). */
void full_handle_key(full_ctx_t *c, SDL_Keycode key);
void full_handle_text(full_ctx_t *c, const char *text);

void full_keypad_append(full_ctx_t *c, char ch);
void full_keypad_apply (full_ctx_t *c);

/* ---- lifecycle ---- */

bool full_open_fonts(full_ctx_t *c);
void full_close_fonts(full_ctx_t *c);

#endif
