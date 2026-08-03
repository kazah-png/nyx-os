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

// The wallpaper render styles (the "scene" the compositor paints from the base
// color). "Nightfall" — the moon-and-stars scene — is the default: NyxOS is named
// for Nyx, the goddess of night, so the night sky IS the brand identity. The clean
// gradient and a flat solid stay one click away in the Wallpaper app.
static const char* style_names[WP_STYLE_COUNT] = { "Limpio", "Nightfall", "Plano", "Estrellas" };

static int g_wallpaper = 0;                    // selected base color; default = morado
static int g_style     = WP_STYLE_NIGHTFALL;   // selected render style; default = moon + stars

uint32_t wallpaper_base_color(void) {
    return fb_rgb(palette[g_wallpaper].r, palette[g_wallpaper].g, palette[g_wallpaper].b);
}

int wallpaper_style(void) {
    return g_style;
}

// Style-button row geometry (top of the content; shared by draw + click).
#define WP_STYLE_OX   16
#define WP_STYLE_OY   34
#define WP_STYLE_BW   78     // four styles now share the row, so the buttons are a touch narrower
#define WP_STYLE_BH   28
#define WP_STYLE_BGAP 6

// Color-swatch grid geometry (below the style row; shared by draw + click).
#define WP_COLS   4
#define WP_SW     74     // swatch width
#define WP_SH     54     // swatch height
#define WP_GAP    12
#define WP_OX     16     // grid origin x within the content
#define WP_OY     92     // grid origin y within the content (below the style row)
#define WP_ROW_H  (WP_SH + 22 + WP_GAP)   // swatch + label + gap

void wallpaper_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win;
    const uint32_t bg = fb_rgb(28, 28, 34);
    fb_fill_rect(cx, cy, cw, ch, bg);

    // --- Style row: pick how the desktop is painted -------------------------
    font_draw_string(cx + 12, cy + 14, "Estilo:", fb_rgb(230, 230, 240), bg);
    for (int i = 0; i < WP_STYLE_COUNT; i++) {
        int x = cx + WP_STYLE_OX + i * (WP_STYLE_BW + WP_STYLE_BGAP);
        int y = cy + WP_STYLE_OY;
        int sel = (i == g_style);
        uint32_t fill = sel ? fb_rgb(130, 90, 210) : fb_rgb(48, 48, 58);
        fb_fill_rect(x, y, WP_STYLE_BW, WP_STYLE_BH, fill);
        uint32_t fr = sel ? fb_rgb(255, 255, 255) : fb_rgb(80, 80, 92);
        fb_fill_rect(x, y, WP_STYLE_BW, 1, fr);                    // top
        fb_fill_rect(x, y + WP_STYLE_BH - 1, WP_STYLE_BW, 1, fr);  // bottom
        fb_fill_rect(x, y, 1, WP_STYLE_BH, fr);                    // left
        fb_fill_rect(x + WP_STYLE_BW - 1, y, 1, WP_STYLE_BH, fr);  // right
        int nlen = (int)strlen(style_names[i]) * FONT_WIDTH;
        font_draw_string(x + (WP_STYLE_BW - nlen) / 2, y + (WP_STYLE_BH - FONT_HEIGHT) / 2,
                         style_names[i], sel ? fb_rgb(255, 255, 255) : fb_rgb(200, 200, 210), fill);
    }

    // --- Color grid: pick the base hue --------------------------------------
    font_draw_string(cx + 12, cy + 72, "Color:", fb_rgb(230, 230, 240), bg);
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
                         i == g_wallpaper ? fb_rgb(255, 255, 255) : fb_rgb(175, 175, 185), bg);
    }
}

void wallpaper_win_click(window_t* win, int mx, int my, int btn) {
    (void)btn;
    int cx = win->x, cy = win->y + TITLE_H;   // content origin (matches draw)

    // Style row hit-test.
    for (int i = 0; i < WP_STYLE_COUNT; i++) {
        int x = cx + WP_STYLE_OX + i * (WP_STYLE_BW + WP_STYLE_BGAP);
        int y = cy + WP_STYLE_OY;
        if (mx >= x && mx < x + WP_STYLE_BW && my >= y && my < y + WP_STYLE_BH) {
            g_style = i;                      // the compositor repaints (incl. the background)
            return;
        }
    }

    // Color grid hit-test.
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
