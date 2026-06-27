#ifndef FILEMAN_WIN_H
#define FILEMAN_WIN_H

#include "kernel.h"
#include "compositor.h"

#define FILEMAN_MAX_ENTRIES 512

typedef struct {
    char cwd[256];
    char entries[FILEMAN_MAX_ENTRIES][64];
    int entry_types[FILEMAN_MAX_ENTRIES];
    int entry_count;
    int scroll_offset;
    char status[128];
    int sel_index;
    int input_mode;  // 0=none, 1=filename, 2=dirname, 3=rename
    char input_buf[64];
    int input_pos;
    uint32_t input_cursor_tick;
    // Context menu
    int ctx_open;
    int ctx_x, ctx_y;
    int ctx_hover;
    // Clipboard
    char clipboard_path[256];
    int clipboard_mode; // 0=none, 1=copy, 2=cut
} fileman_win_t;

fileman_win_t* fileman_create_ctx(void);
void fileman_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void fileman_win_click(window_t* win, int mx, int my, int btn);
void fileman_win_key(window_t* win, int key);
void fileman_win_mousemove(window_t* win, int mx, int my);
void fileman_refresh(fileman_win_t* fm);
void fileman_new_folder(fileman_win_t* fm);
void fileman_new_file(fileman_win_t* fm);

#endif
