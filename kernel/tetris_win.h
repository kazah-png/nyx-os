#ifndef TETRIS_WIN_H
#define TETRIS_WIN_H
#include "compositor.h"

// Tetris as an in-kernel GUI window (v5.9.43). Reuses the compositor's on_tick
// game-tick (like Pong/Snake) for gravity. Left/Right move, Up (or W) rotates,
// Down soft-drops, Space hard-drops, R restarts. A 10x20 well plus a side panel
// showing the NEXT piece, SCORE and LINES.
#define TET_COLS    10
#define TET_ROWS    20
#define TET_CELL    16
#define TET_MARGIN  8
#define TET_PANEL   92
#define TET_BOARD_W (TET_COLS * TET_CELL)   // 160
#define TET_BOARD_H (TET_ROWS * TET_CELL)   // 320
#define TET_W (TET_MARGIN + TET_BOARD_W + 10 + TET_PANEL + TET_MARGIN)  // 278
#define TET_H (TET_MARGIN + TET_BOARD_H + TET_MARGIN)                   // 336

void* tetris_create_ctx(void);
void  tetris_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void  tetris_win_key(window_t* win, int key);
int   tetris_win_tick(window_t* win);

#endif
