#ifndef WALLPAPER_WIN_H
#define WALLPAPER_WIN_H

#include "../../core/kernel.h"
#include "compositor.h"

// Desktop wallpaper picker: 11 base colors (index 0 = the NyxOS brand purple).
// draw_background() builds a vertical gradient from the chosen base color.
#define WALLPAPER_COUNT 11

uint32_t wallpaper_base_color(void);   // current base color, for the compositor background
void wallpaper_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void wallpaper_win_click(window_t* win, int mx, int my, int btn);

#endif // WALLPAPER_WIN_H
