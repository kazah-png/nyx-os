#include "../../core/kernel.h"
#include "compositor.h"
#include "wallpaper_win.h"
#include "../../drivers/video/font.h"

// The 11 wallpaper base colors. Index 0 is the NyxOS brand purple (morado) — the
// default. draw_background() (compositor.c) builds a vertical gradient from a darker
// to a lighter shade of whichever base color is selected here.
static const struct { uint8_t r, g, b; const char* name; } palette[WALLPAPER_COUNT] = {
    { 130,  90, 210, "Morado"   },   // 0 = brand purple (default)
    {  60, 110, 210, "Azul"     },
    {  40, 160, 175, "Turquesa" },
    {  55, 165,  95, "Verde"    },
    { 140, 165,  60, "Lima"     },
    { 220, 185,  70, "Oro"      },
    { 225, 130,  50, "Naranja"  },
    { 205,  70,  70, "Rojo"     },
    { 215,  95, 175, "Rosa"     },
    {  95, 105, 130, "Pizarra"  },
    {  45,  50,  70, "Carbon"   },
};

static int g_wallpaper = 0;   // selected index; default = morado

uint32_t wallpaper_base_color(void) {
    return fb_rgb(palette[g_wallpaper].r, palette[g_wallpaper].g, palette[g_wallpaper].b);
}

// Grid geometry (shared by draw + click so hit-testing matches the rendering).
#define WP_COLS   4
#define WP_SW     74     // swatch width
#define WP_SH     54     // swatch height
#define WP_GAP    12
#define WP_OX     16     // grid origin x within the content
#define WP_OY     40     // grid origin y within the content (below the title text)
#define WP_ROW_H  (WP_SH + 22 + WP_GAP)   // swatch + label + gap

void wallpaper_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win;
    fb_fill_rect(cx, cy, cw, ch, fb_rgb(28, 28, 34));
    font_draw_string(cx + 12, cy + 14, "Fondo de pantalla:", fb_rgb(230, 230, 240), fb_rgb(28, 28, 34));

    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        int col = i % WP_COLS, row = i / WP_COLS;
        int x = cx + WP_OX + col * (WP_SW + WP_GAP);
        int y = cy + WP_OY + row * WP_ROW_H;
        uint32_t c = fb_rgb(palette[i].r, palette[i].g, palette[i].b);
        // The selected swatch gets a white frame.
        if (i == g_wallpaper) fb_fill_rect(x - 3, y - 3, WP_SW + 6, WP_SH + 6, fb_rgb(255, 255, 255));
        fb_fill_rect(x, y, WP_SW, WP_SH, c);
        int nlen = (int)strlen(palette[i].name) * FONT_WIDTH;
        font_draw_string(x + (WP_SW - nlen) / 2, y + WP_SH + 4, palette[i].name,
                         i == g_wallpaper ? fb_rgb(255, 255, 255) : fb_rgb(175, 175, 185),
                         fb_rgb(28, 28, 34));
    }
}

void wallpaper_win_click(window_t* win, int mx, int my, int btn) {
    (void)btn;
    int cx = win->x, cy = win->y + TITLE_H;   // content origin (matches draw)
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        int col = i % WP_COLS, row = i / WP_COLS;
        int x = cx + WP_OX + col * (WP_SW + WP_GAP);
        int y = cy + WP_OY + row * WP_ROW_H;
        if (mx >= x && mx < x + WP_SW && my >= y && my < y + WP_SH) {
            g_wallpaper = i;                  // the compositor repaints (incl. the background)
            return;
        }
    }
}
