// The "Games" desktop folder (v5.9.40). See games_win.h. Draws Minesweeper, DOOM
// and Pong as three clickable emblems — each a tiny caricature of the game so the
// shelf reads at a glance — and launches the one you click. The launchers live in
// compositor.c: launch_minesweeper / launch_doom_windowed / launch_pong.
#include "games_win.h"
#include "../../drivers/video/font.h"

// Where game i's emblem sits, in absolute framebuffer coords, given the window's
// client origin. One column per game; the emblem is centred in its column.
static void icon_origin(int cx, int cy, int i, int* ix, int* iy) {
    int cell_x = cx + GAMES_MARGIN_X + i * GAMES_CELL_W;
    *ix = cell_x + (GAMES_CELL_W - GAMES_ICON) / 2;
    *iy = cy + GAMES_MARGIN_Y;
}

// ---- emblems: each fills a GAMES_ICON x GAMES_ICON square at (x, y) ----

static void draw_mines_emblem(int x, int y) {
    int s = GAMES_ICON;
    fb_fill_rect(x, y, s, s, fb_rgb(150, 155, 165));            // tile field
    uint32_t line = fb_rgb(90, 95, 105);
    for (int g = 1; g < 3; g++) {                              // 3x3 grid
        fb_fill_rect(x + g * s / 3, y, 2, s, line);
        fb_fill_rect(x, y + g * s / 3, s, 2, line);
    }
    int mx = x + s / 2, my = y + s / 2;                        // a mine, centre cell
    fb_fill_rect(mx - 8,  my - 8,  16, 16, fb_rgb(25, 25, 30));
    fb_fill_rect(mx - 1,  my - 13, 2,  26, fb_rgb(25, 25, 30));
    fb_fill_rect(mx - 13, my - 1,  26, 2,  fb_rgb(25, 25, 30));
    fb_fill_rect(mx - 5,  my - 5,  4,  4,  fb_rgb(200, 200, 210)); // glint
    fb_fill_rect(x + 7,   y + 5,   2,  12, fb_rgb(40, 40, 40));    // flag pole
    fb_fill_rect(x + 9,   y + 5,   8,  6,  fb_rgb(210, 50, 40));   // red flag
}

static void draw_doom_emblem(int x, int y) {
    int s = GAMES_ICON;
    fb_fill_rect(x, y, s, s, fb_rgb(150, 20, 15));             // hell red
    uint32_t bone = fb_rgb(230, 225, 215);
    uint32_t hole = fb_rgb(30, 10, 10);
    fb_fill_rect(x + 16, y + 12, 32, 26, bone);               // cranium
    fb_fill_rect(x + 20, y + 36, 24, 12, bone);               // jaw
    fb_fill_rect(x + 21, y + 18, 9,  9,  hole);               // eye sockets
    fb_fill_rect(x + 34, y + 18, 9,  9,  hole);
    fb_fill_rect(x + 30, y + 28, 4,  6,  hole);               // nose
    fb_fill_rect(x + 26, y + 40, 2,  8,  hole);               // teeth gaps
    fb_fill_rect(x + 31, y + 40, 2,  8,  hole);
    fb_fill_rect(x + 36, y + 40, 2,  8,  hole);
}

static void draw_pong_emblem(int x, int y) {
    int s = GAMES_ICON;
    fb_fill_rect(x, y, s, s, fb_rgb(10, 10, 15));             // court
    for (int ny = y + 4; ny < y + s - 4; ny += 10)           // dashed net
        fb_fill_rect(x + s / 2 - 1, ny, 2, 6, fb_rgb(70, 75, 90));
    fb_fill_rect(x + 6,      y + 14, 5, 22, fb_rgb(120, 200, 255)); // left paddle
    fb_fill_rect(x + s - 11, y + 30, 5, 22, fb_rgb(255, 140, 120)); // right paddle
    fb_fill_rect(x + s / 2 - 3, y + 34, 6, 6, fb_rgb(240, 240, 245)); // ball
}

static void draw_snake_emblem(int x, int y) {
    int s = GAMES_ICON;
    fb_fill_rect(x, y, s, s, fb_rgb(16, 20, 16));            // board
    for (int g = 1; g < 4; g++) {                           // faint grid
        fb_fill_rect(x + g * s / 4, y, 1, s, fb_rgb(24, 30, 24));
        fb_fill_rect(x, y + g * s / 4, s, 1, fb_rgb(24, 30, 24));
    }
    static const int gx[7] = { 1, 2, 3, 3, 3, 2, 1 };       // a coiled snake body
    static const int gy[7] = { 1, 1, 1, 2, 3, 3, 3 };
    int c = 12;
    for (int i = 0; i < 7; i++) {
        uint32_t col = (i == 6) ? fb_rgb(150, 240, 140) : fb_rgb(70, 190, 90); // head brighter
        fb_fill_rect(x + 4 + gx[i] * c, y + 4 + gy[i] * c, c - 2, c - 2, col);
    }
    fb_fill_rect(x + 4 + c + 3, y + 4 + 2 * c + 3, c - 6, c - 6, fb_rgb(235, 80, 70)); // food
}

static void draw_tetris_emblem(int x, int y) {
    int s = GAMES_ICON;
    fb_fill_rect(x, y, s, s, fb_rgb(18, 18, 28));           // well
    int cs = 10;
    uint32_t cols[4] = { fb_rgb(80,210,230), fb_rgb(235,150,50),
                         fb_rgb(90,200,100), fb_rgb(230,80,80) };
    for (int c = 0; c < 5; c++)                             // stacked rubble, two rows
        fb_fill_rect(x + 7 + c*cs, y + 44, cs - 1, cs - 1, cols[c % 4]);
    for (int c = 0; c < 3; c++)
        fb_fill_rect(x + 7 + c*cs, y + 34, cs - 1, cs - 1, cols[(c + 1) % 4]);
    uint32_t t = fb_rgb(180, 100, 220);                    // a falling T-piece
    fb_fill_rect(x + 17, y + 8,  cs - 1, cs - 1, t);
    fb_fill_rect(x + 7,  y + 18, cs - 1, cs - 1, t);
    fb_fill_rect(x + 17, y + 18, cs - 1, cs - 1, t);
    fb_fill_rect(x + 27, y + 18, cs - 1, cs - 1, t);
}

static const char* g_names[GAMES_COUNT] = { "Minesweeper", "DOOM", "Pong", "Snake", "Tetris" };

void games_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win;
    fb_fill_rect(cx, cy, cw, ch, fb_rgb(38, 42, 58));         // folder backdrop
    for (int i = 0; i < GAMES_COUNT; i++) {
        int ix, iy;
        icon_origin(cx, cy, i, &ix, &iy);
        fb_fill_rect(ix - 2, iy - 2, GAMES_ICON + 4, GAMES_ICON + 4, fb_rgb(70, 76, 96)); // frame
        switch (i) {
            case 0: draw_mines_emblem(ix, iy); break;
            case 1: draw_doom_emblem(ix, iy);  break;
            case 2: draw_pong_emblem(ix, iy);   break;
            case 3: draw_snake_emblem(ix, iy);  break;
            case 4: draw_tetris_emblem(ix, iy); break;
        }
        int cell_x = cx + GAMES_MARGIN_X + i * GAMES_CELL_W;
        int tw = (int)strlen(g_names[i]) * FONT_WIDTH;
        int tx = cell_x + (GAMES_CELL_W - tw) / 2;
        int ty = iy + GAMES_ICON + 6;
        fb_fill_rect(tx - 3, ty - 1, tw + 6, FONT_HEIGHT + 2, fb_rgb(20, 24, 34));
        font_draw_string(tx, ty, g_names[i], fb_rgb(232, 232, 238), fb_rgb(20, 24, 34));
    }
}

void games_win_click(window_t* win, int mx, int my, int btn) {
    if (btn != 1) return;                                     // left click launches
    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);
    for (int i = 0; i < GAMES_COUNT; i++) {
        int ix, iy;
        icon_origin(cx, cy, i, &ix, &iy);
        // Hit area: the emblem plus its label strip, for a forgiving target.
        if (mx >= ix && mx < ix + GAMES_ICON &&
            my >= iy && my < iy + GAMES_ICON + 6 + FONT_HEIGHT + 2) {
            switch (i) {
                case 0: launch_minesweeper();   break;
                case 1: launch_doom_windowed(); break;  // blocks (foreground pump) until DOOM quits
                case 2: launch_pong();          break;
                case 3: launch_snake();         break;
                case 4: launch_tetris();        break;
            }
            return;
        }
    }
}
