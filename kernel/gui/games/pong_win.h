#ifndef PONG_WIN_H
#define PONG_WIN_H
#include "../core/compositor.h"

// Pong as an in-kernel GUI window (v5.9.39). The court fills the client area; the
// compositor's ~30fps game-tick calls pong_win_tick to advance the ball + AI paddle.
// The player's paddle tracks the mouse (classic paddle control); arrows / W-S also work.
#define PONG_W 480      // court width  (== window client width)
#define PONG_H 320      // court height (== window client height)

void* pong_create_ctx(void);
void  pong_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void  pong_win_key(window_t* win, int key);
void  pong_win_mousemove(window_t* win, int mx, int my, int btns);
int   pong_win_tick(window_t* win);

#endif
