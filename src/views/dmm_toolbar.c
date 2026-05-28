/**
 * DMM toolbar — compact strip readout.
 *
 * One row: mode label (DCV / ACV / Ω / …), big primary reading scaled to
 * engineering units (mV / µA / kΩ / …), AUTO badge if auto-ranging, OL
 * badge on overload. Read-only: mode/range changes happen on the meter's
 * front panel or via the full DMM view.
 *
 * Visual style mirrors the PSU toolbar so a side-by-side window grid
 * looks coherent.
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

#define WIN_W           700
#define WIN_H           80
#define HEADER_H        18

typedef struct { Uint8 r, g, b, a; } Color;

static const Color COL_BG_DARK   = {22, 22, 24, 255};
static const Color COL_BG_STRIP  = {32, 32, 36, 255};
static const Color COL_HEADER    = {20, 20, 22, 255};
static const Color COL_BORDER    = {55, 55, 60, 255};
static const Color COL_VAL       = {0, 255, 140, 255};
static const Color COL_UNIT      = {120, 220, 255, 255};
static const Color COL_MODE      = {255, 200, 80, 255};
static const Color COL_DIM       = {100, 100, 108, 255};
static const Color COL_ON        = {0, 255, 100, 255};
static const Color COL_ERR       = {255, 70, 70, 255};
static const Color COL_BADGE_BG  = {50, 50, 55, 255};
static const Color COL_BADGE_HOT = {180, 30, 30, 255};
static const Color COL_TEXT      = {220, 220, 225, 255};

typedef struct {
    dmm_driver_t *drv;

    SDL_Window   *window;
    SDL_Renderer *renderer;
    TTF_Font     *font_label;
    TTF_Font     *font_num;
    TTF_Font     *font_mode;
    TTF_Font     *font_badge;

    dmm_reading_t reading;
    bool running;
} app_t;

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
    int w = surf->w;
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
    return w;
}
static void draw_led(SDL_Renderer *r, int cx, int cy, int rad, Color col) {
    set_color(r, col);
    for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++)
            if (dx * dx + dy * dy <= rad * rad)
                SDL_RenderDrawPoint(r, cx + dx, cy + dy);
}

/* Scale value into engineering range [1, 1000) and pick a prefix.
 * Returns scaled value; writes prefix (T/G/M/k/""/m/µ/n/p) into *prefix. */
static float engineering_scale(float value, const char **prefix) {
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

static void render(app_t *a) {
    SDL_Renderer *r = a->renderer;
    set_color(r, COL_BG_DARK);
    SDL_RenderClear(r);

    /* Header. */
    set_color(r, COL_HEADER);
    fill_rect(r, 0, 0, WIN_W, HEADER_H);
    set_color(r, COL_BORDER);
    SDL_RenderDrawLine(r, 0, HEADER_H - 1, WIN_W, HEADER_H - 1);
    draw_text(r, a->font_label, "DMM", 8, 4, COL_DIM);

    bool ok = a->drv->is_connected(a->drv);
    Color dot = ok ? COL_ON : COL_ERR;
    draw_led(r, WIN_W - 12, HEADER_H / 2, 4, dot);

    /* Strip body. */
    int sy = HEADER_H + 4;
    int sh = WIN_H - HEADER_H - 8;
    set_color(r, COL_BG_STRIP);
    fill_rect(r, 6, sy, WIN_W - 12, sh);
    set_color(r, COL_BORDER);
    draw_rect(r, 6, sy, WIN_W - 12, sh);

    /* Mode label. */
    int x = 16;
    int y = sy + 12;
    const char *mode_lbl = dmm_mode_label(a->reading.mode);
    int mw = draw_text(r, a->font_mode, mode_lbl, x, y, COL_MODE);
    x += mw + 16;

    /* Value + unit — dot-matrix VFD. */
    {
        int dot_size = 2;
        int dot_gap  = 1;
        int char_gap = 4;
        int char_h   = vfd_char_height(dot_size, dot_gap);
        int vfd_y    = sy + (sh - char_h) / 2;
        vfd_color_t vfd_off = { 0, 20, 12, 255 };

        if (a->reading.overload) {
            vfd_color_t vfd_err = { COL_ERR.r, COL_ERR.g, COL_ERR.b, COL_ERR.a };
            vfd_draw_number(r, x, vfd_y, " -OL- ",
                            dot_size, dot_gap, char_gap, vfd_err, vfd_off, true);
        } else if (!a->reading.valid) {
            vfd_color_t vfd_dim = { COL_DIM.r, COL_DIM.g, COL_DIM.b, COL_DIM.a };
            vfd_draw_number(r, x, vfd_y, " ----- ",
                            dot_size, dot_gap, char_gap, vfd_dim, vfd_off, true);
        } else {
            const char *prefix = "";
            float scaled = engineering_scale(a->reading.value, &prefix);
            char vbuf[24];
            snprintf(vbuf, sizeof(vbuf), "%9.5f", scaled);

            vfd_color_t vfd_on = { COL_VAL.r, COL_VAL.g, COL_VAL.b, COL_VAL.a };
            int vw = vfd_draw_number(r, x, vfd_y, vbuf,
                                     dot_size, dot_gap, char_gap, vfd_on, vfd_off, true);

            char unit[16];
            snprintf(unit, sizeof(unit), "%s%s", prefix, dmm_mode_unit(a->reading.mode));
            draw_text(r, a->font_mode, unit, x + vw + 10, vfd_y + char_h / 2 - 10, COL_UNIT);
        }
    }

    /* Badges in the right gutter — AUTO / OL / rate. */
    int bx = WIN_W - 14;
    int by = sy + 8;
    int bh = 22;
    if (a->reading.overload) {
        const char *lbl = "OL";
        int tw = 0, th = 0;
        TTF_SizeText(a->font_badge, lbl, &tw, &th);
        int bw = tw + 14;
        set_color(r, COL_BADGE_HOT);
        fill_rect(r, bx - bw, by, bw, bh);
        set_color(r, COL_BORDER);
        draw_rect(r, bx - bw, by, bw, bh);
        draw_text(r, a->font_badge, lbl, bx - bw + 7, by + (bh - th) / 2, (Color){255,255,255,255});
        bx -= bw + 6;
    }

    if (a->reading.range == 0.0f) {
        const char *lbl = "AUTO";
        int tw = 0, th = 0;
        TTF_SizeText(a->font_badge, lbl, &tw, &th);
        int bw = tw + 14;
        set_color(r, COL_BADGE_BG);
        fill_rect(r, bx - bw, by, bw, bh);
        set_color(r, COL_BORDER);
        draw_rect(r, bx - bw, by, bw, bh);
        draw_text(r, a->font_badge, lbl, bx - bw + 7, by + (bh - th) / 2, COL_TEXT);
        bx -= bw + 6;
    }

    /* Rate label (S/M/F). */
    {
        const char *lbl = (a->reading.rate == DMM_RATE_SLOW) ? "SLOW"
                        : (a->reading.rate == DMM_RATE_FAST) ? "FAST"
                                                             : "MED";
        int tw = 0, th = 0;
        TTF_SizeText(a->font_badge, lbl, &tw, &th);
        int bw = tw + 14;
        set_color(r, COL_BADGE_BG);
        fill_rect(r, bx - bw, by, bw, bh);
        set_color(r, COL_BORDER);
        draw_rect(r, bx - bw, by, bw, bh);
        draw_text(r, a->font_badge, lbl, bx - bw + 7, by + (bh - th) / 2, COL_DIM);
    }

    SDL_RenderPresent(r);
}

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
    a->font_label = TTF_OpenFont(path, 12);
    a->font_num   = TTF_OpenFont(path, 30);
    a->font_mode  = TTF_OpenFont(path, 18);
    a->font_badge = TTF_OpenFont(path, 11);
    return a->font_label && a->font_num && a->font_mode && a->font_badge;
}

static void cleanup(app_t *a) {
    if (a->font_label) TTF_CloseFont(a->font_label);
    if (a->font_num)   TTF_CloseFont(a->font_num);
    if (a->font_mode)  TTF_CloseFont(a->font_mode);
    if (a->font_badge) TTF_CloseFont(a->font_badge);
    if (a->renderer)   SDL_DestroyRenderer(a->renderer);
    if (a->window)     SDL_DestroyWindow(a->window);
    TTF_Quit();
    SDL_Quit();
}

int view_dmm_toolbar_run(dmm_driver_t *drv) {
    if (!drv) return 1;

    app_t a;
    memset(&a, 0, sizeof(a));
    a.drv     = drv;
    a.running = true;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    if (TTF_Init() < 0) { SDL_Quit(); return 1; }

    a.window = SDL_CreateWindow("DMM",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!a.window) { cleanup(&a); return 1; }

    a.renderer = SDL_CreateRenderer(a.window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a.renderer) { cleanup(&a); return 1; }
    SDL_SetRenderDrawBlendMode(a.renderer, SDL_BLENDMODE_BLEND);

    if (!open_fonts(&a)) { cleanup(&a); return 1; }

    while (a.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) a.running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                a.running = false;
        }
        a.drv->read(a.drv, &a.reading);
        render(&a);
        SDL_Delay(16);
    }

    cleanup(&a);
    return 0;
}
