#ifndef PAINT_WIN_H
#define PAINT_WIN_H

#include "kernel.h"
#include "compositor.h"

#define PAINT_CANVAS_W 512
#define PAINT_CANVAS_H 384
#define PAINT_TOOLBAR_H 32
#define PAINT_STATUS_H 18
#define PAINT_MIN_BRUSH 1
#define PAINT_MAX_BRUSH 20
#define PAINT_NUM_COLORS 10
#define BUTTONS_WIDTH 60

typedef struct {
    uint32_t canvas[PAINT_CANVAS_W * PAINT_CANVAS_H];
    int brush_size;
    uint32_t brush_color;
    int brush_style;
    int last_x, last_y;
    char status[64];
} paint_win_t;

paint_win_t* paint_create_ctx(void);
void paint_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void paint_win_click(window_t* win, int mx, int my, int btn);
void paint_win_key(window_t* win, int key);
void paint_win_pressed(window_t* win, int mx, int my, int btns);

#endif
