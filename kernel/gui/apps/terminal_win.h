#ifndef TERMINAL_WIN_H
#define TERMINAL_WIN_H

#include "../../core/kernel.h"
#include "../core/compositor.h"

#define TERM_LINES      2000
#define TERM_COLS       80
#define TERM_INPUT_MAX  256
#define TERM_OUTPUT_MAX 4096

typedef struct {
    char lines[TERM_LINES][TERM_COLS];
    uint8_t colors[TERM_LINES][TERM_COLS];
    int line_count;
    int scroll_offset;
    char input[TERM_INPUT_MAX];
    int input_len;
    int cursor_pos;
    char prompt[64];
    int prompt_len;
    void* cwd;              // this shell's current directory (opaque vfs node)
    char output_buf[TERM_OUTPUT_MAX];
    int output_len;
    int capturing;
    int visible_rows;
    // Cursor-addressed "screen mode" for full-screen TUIs (edit, top). Entered when
    // a captured program emits a CSI cursor sequence (ESC[H / ESC[2J); the lines[]
    // grid is then treated as a fixed screen drawn from row 0, with a block cursor
    // at (out_row,out_col). Reset when the command finishes.
    int screen_mode;
    int out_row, out_col;
    // ANSI SGR color state (v5.8.26). cur_color is the VGA attribute byte applied
    // to newly written cells (fg = low nibble, bg = high nibble); out_color mirrors
    // output_buf so a scrollback line keeps per-char color (e.g. `ls --color`).
    uint8_t cur_color;
    uint8_t out_color[TERM_OUTPUT_MAX];
} terminal_win_t;

#define TERM_SCREEN_ROWS 45    // rows cleared/addressable in screen mode (>= any window)
#define TERM_DEFAULT_COLOR (VGA_LIGHT_GREY | (VGA_BLACK << 4))   // light-grey on black

terminal_win_t* terminal_create_ctx(void);
void terminal_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void terminal_win_key(window_t* win, int key);
int terminal_capture_putchar(int c);
void terminal_capture_reset(terminal_win_t* t);   // leave screen mode when a cmd ends

#endif
