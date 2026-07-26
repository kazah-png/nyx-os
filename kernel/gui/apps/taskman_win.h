#ifndef TASKMAN_WIN_H
#define TASKMAN_WIN_H

#include "../../core/kernel.h"
#include "../core/compositor.h"

typedef struct {
    int scroll_offset;
} taskman_win_t;

taskman_win_t* taskman_create_ctx(void);
void taskman_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void taskman_win_click(window_t* win, int mx, int my, int btn);
void taskman_win_key(window_t* win, int key);

#endif
