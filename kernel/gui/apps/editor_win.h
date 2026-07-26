#ifndef EDITOR_WIN_H
#define EDITOR_WIN_H

#include "../../core/kernel.h"
#include "../core/compositor.h"

#define EDITOR_MAX_LINES 512
#define EDITOR_LINE_LEN 256

typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_LINE_LEN];
    int line_count;
    int cursor_x, cursor_y;
    int scroll_x, scroll_y;
    char filename[64];
    int modified;
    char status[64];
    uint32_t cursor_tick;
} editor_win_t;

editor_win_t* editor_create_ctx(void);
void editor_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void editor_win_click(window_t* win, int mx, int my, int btn);
void editor_win_key(window_t* win, int key);
void editor_load_file(editor_win_t* ed, const char* path);  // load `path` into this editor

#endif
