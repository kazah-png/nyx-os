#ifndef GAMES_WIN_H
#define GAMES_WIN_H
#include "compositor.h"

// The "Games" desktop folder (v5.9.40, +Snake v5.9.42). A plain compositor window
// that visually lists the built-in games — Minesweeper, DOOM, Pong and Snake — as
// clickable icons, each drawn as a little preview of the game, and launches the one
// you click. It is a folder in spirit only: there is no filesystem behind it, just a
// fixed menu, which is exactly what a "games shelf" on the desktop needs to be.
#define GAMES_COUNT    4
#define GAMES_ICON     64          // emblem square, px
#define GAMES_CELL_W   120         // one column: emblem + label, centred
#define GAMES_CELL_H   96          // emblem + gap + label strip
#define GAMES_MARGIN_X 20
#define GAMES_MARGIN_Y 20

#define GAMES_WIN_W    (GAMES_COUNT * GAMES_CELL_W + 2 * GAMES_MARGIN_X)  // 400
#define GAMES_WIN_H    (GAMES_CELL_H + 2 * GAMES_MARGIN_Y)               // 136

void games_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void games_win_click(window_t* win, int mx, int my, int btn);

#endif
