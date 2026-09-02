#ifndef WALLPAPER_WIN_H
#define WALLPAPER_WIN_H

#include "../../core/kernel.h"
#include "compositor.h"

// Desktop wallpaper picker: 11 base colors (index 0 = the NyxOS brand purple) and
// 11 render styles. draw_background() builds the desktop from the chosen base color
// according to the chosen style.
#define WALLPAPER_COUNT 11

// Wallpaper render styles — how the compositor paints the desktop from the base
// color. New in the v6 era: a clean minimal desktop is the default.
enum {
    WP_STYLE_CLEAN = 0,   // clean vertical gradient (v6 default)
    WP_STYLE_NIGHTFALL,   // gradient + moon + stars (the classic scene)
    WP_STYLE_FLAT,        // a single flat solid color
    WP_STYLE_STARFIELD,   // Nightfall scene with an ANIMATED twinkling star field
    WP_STYLE_SHOOTINGSTAR,// Nightfall scene with a periodic ANIMATED shooting star
    WP_STYLE_AURORA,      // Nightfall scene with slow ANIMATED aurora curtains
    WP_STYLE_NEBULA,      // Nightfall scene with soft drifting ANIMATED nebula clouds
    WP_STYLE_LUCES,       // Nightfall scene with slow ANIMATED drifting glowing orbs (fireflies)
    WP_STYLE_ONDAS,       // Nightfall scene with slow ANIMATED moonlight ripples from the moon
    WP_STYLE_CONSTELACIONES, // Nightfall scene with a STATIC star map: constellations of stars joined by faint threads
    WP_STYLE_LLUVIA,      // Nightfall scene with an ANIMATED soft rain of falling lilac star-light
    WP_STYLE_CORDILLERA,  // Nightfall scene with a STATIC layered silhouette of night mountains on the horizon
    WP_STYLE_COUNT
};

uint32_t wallpaper_base_color(void);   // current base color, for the compositor background
uint32_t wallpaper_color_rgb(int idx); // rgb of ANY palette color by index (nyx.conf `border`)
int      wallpaper_style(void);        // current render style (WP_STYLE_*)
void     wallpaper_set_style(int style);            // set render style (clamped)
void     wallpaper_set_color(int idx);              // set base-color index (clamped)
int      wallpaper_color(void);        // current base-color index
const char* wallpaper_style_name(int i);   // index -> style name (default "Nightfall" if OOR)
const char* wallpaper_color_name(int i);   // index -> color name (default "Morado" if OOR)
int      wallpaper_style_from_name(const char* name);   // style name -> index, or -1
int      wallpaper_color_from_name(const char* name);   // color name -> index, or -1
void wallpaper_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void wallpaper_win_click(window_t* win, int mx, int my, int btn);

#endif // WALLPAPER_WIN_H
