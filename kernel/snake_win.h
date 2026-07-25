#ifndef SNAKE_WIN_H
#define SNAKE_WIN_H
#include "compositor.h"

// Snake as an in-kernel GUI window (v5.9.42). Grid-based; the compositor's ~30fps
// game-tick (window on_tick) steps the snake every few frames. Arrows / WASD steer,
// eating food grows the snake and scores, and hitting a wall or itself ends the game
// (R restarts). Same in-kernel pattern as Pong ([[pong_win]]), reusing on_tick.
#define SNAKE_CELL 16
#define SNAKE_COLS 22
#define SNAKE_ROWS 16
#define SNAKE_HDR  22                                    // score strip at the top
#define SNAKE_W    (SNAKE_COLS * SNAKE_CELL)             // 352 (== window client width)
#define SNAKE_H    (SNAKE_HDR + SNAKE_ROWS * SNAKE_CELL) // 278 (== window client height)

void* snake_create_ctx(void);
void  snake_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void  snake_win_key(window_t* win, int key);
void  snake_win_tick(window_t* win);

#endif
