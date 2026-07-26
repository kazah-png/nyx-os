#ifndef SOUNDTEST_WIN_H
#define SOUNDTEST_WIN_H

#include "../../core/kernel.h"
#include "../core/compositor.h"

typedef struct {
    char status[64];
    uint32_t last_trigger;
} soundtest_win_t;

soundtest_win_t* soundtest_create_ctx(void);
void soundtest_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void soundtest_win_click(window_t* win, int mx, int my, int btn);

#endif
