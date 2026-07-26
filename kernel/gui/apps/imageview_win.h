#ifndef IMAGEVIEW_WIN_H
#define IMAGEVIEW_WIN_H

#include "../../core/kernel.h"
#include "../core/compositor.h"

typedef struct {
    uint8_t* pixels;
    uint32_t img_w, img_h;
    int offset_x, offset_y;
    float zoom;
    char filename[64];
    char status[64];
} imageview_win_t;

imageview_win_t* imageview_create_ctx(void);
void imageview_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void imageview_win_key(window_t* win, int key);

#endif
