/**
 * Dot-matrix VFD digit renderer — shared by PSU full views and DMM views.
 *
 * Renders numeric characters (0-9, ".", "-", " ") as 5×7 dot-matrix glyphs
 * with glow + bright-center highlights. The same primitive lights up the
 * "VFD" panels in the PSU full GUI and the DMM views.
 */

#ifndef VIEWS_VFD_DOTMATRIX_H
#define VIEWS_VFD_DOTMATRIX_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct { Uint8 r, g, b, a; } vfd_color_t;

/**
 * Draw a numeric string at (x, y) using the dot-matrix font.
 *
 * dot_size:  pixel radius of each on/off dot. 2 is the usual choice.
 * dot_gap:   spacing between dots within a character (1 is usual).
 * char_gap:  extra horizontal pixels between adjacent characters.
 * on_col:    glow color used for lit dots.
 * off_col:   color used for "ghost" off-dots when show_off is true.
 * show_off:  if true, off-dots are drawn dimly (the classic VFD look).
 *
 * Returns the rendered pixel width.
 */
int vfd_draw_number(SDL_Renderer *r, int x, int y, const char *str,
                    int dot_size, int dot_gap, int char_gap,
                    vfd_color_t on_col, vfd_color_t off_col, bool show_off);

/* Geometry helpers — let callers size their VFD panels precisely. */
int vfd_char_width (int dot_size, int dot_gap);
int vfd_char_height(int dot_size, int dot_gap);

#endif
