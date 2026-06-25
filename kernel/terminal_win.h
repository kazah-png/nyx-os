#ifndef TERMINAL_WIN_H
#define TERMINAL_WIN_H

#include "kernel.h"
#include "compositor.h"

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
    char prompt[32];
    int prompt_len;
    char output_buf[TERM_OUTPUT_MAX];
    int output_len;
    int capturing;
    int visible_rows;
} terminal_win_t;

terminal_win_t* terminal_create_ctx(void);
void terminal_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void terminal_win_key(window_t* win, int key);
int terminal_capture_putchar(int c);

#endif
