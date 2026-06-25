#include "kernel.h"
#include "compositor.h"
#include "terminal_win.h"
#include "font.h"

static void term_add_line(terminal_win_t* term, const char* text, uint8_t color) {
    if (term->line_count >= TERM_LINES) {
        for (int i = 1; i < TERM_LINES; i++) {
            memcpy(term->lines[i-1], term->lines[i], TERM_COLS);
            memcpy(term->colors[i-1], term->colors[i], TERM_COLS);
        }
        term->line_count = TERM_LINES - 1;
    }
    int idx = term->line_count;
memset_asm(term->lines[idx], ' ', TERM_COLS);
    for (int i = 0; i < TERM_COLS-1 && text[i]; i++)
        term->lines[idx][i] = text[i];
    memset_asm(term->colors[idx], color, TERM_COLS);
    term->line_count++;
    if (term->scroll_offset > 0) term->scroll_offset++;
}

terminal_win_t* terminal_create_ctx(void) {
    terminal_win_t* term = (terminal_win_t*)kmalloc(sizeof(terminal_win_t));
    if (!term) return NULL;
    memset_asm(term, 0, sizeof(terminal_win_t));
    snprintf(term->prompt, sizeof(term->prompt), "nyx:~$ ");
    term->prompt_len = strlen(term->prompt);
    term->visible_rows = 20;
    term_add_line(term, "NyxOS Terminal v0.2.0", VGA_LIGHT_GREEN | (VGA_BLACK << 4));
    term_add_line(term, "Type 'help' for available commands.", VGA_LIGHT_CYAN | (VGA_BLACK << 4));
    term_add_line(term, "", VGA_LIGHT_GREY | (VGA_BLACK << 4));
    return term;
}

static terminal_win_t* capture_term = NULL;

int terminal_capture_putchar(int c) {
    if (!capture_term || !capture_term->capturing) return c;
    if (c == '\n') {
        capture_term->output_buf[capture_term->output_len] = '\0';
        if (capture_term->output_len > 0)
            term_add_line(capture_term, capture_term->output_buf, VGA_LIGHT_GREY | (VGA_BLACK << 4));
        capture_term->output_len = 0;
    } else if (c >= 0x20 && capture_term->output_len < TERM_OUTPUT_MAX - 1) {
        capture_term->output_buf[capture_term->output_len++] = c;
    }
    return c;
}
void terminal_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    terminal_win_t* term = (terminal_win_t*)win->reserved;
    if (!term) return;

    fb_fill_rect(cx, cy, cw, ch, fb_rgb(0,0,0));

    uint32_t char_w = FONT_WIDTH;
    uint32_t char_h = FONT_HEIGHT;
    int cols = cw / char_w;
    if (cols > TERM_COLS) cols = TERM_COLS;
    int rows = ch / char_h;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    term->visible_rows = rows;

    int start_line = term->line_count - rows - 1 - term->scroll_offset;
    if (start_line < 0) start_line = 0;

    for (int r = 0; r < rows && start_line + r < term->line_count; r++) {
        int li = start_line + r;
        int y = cy + r * char_h;
        for (int c = 0; c < cols; c++) {
            char ch_char = term->lines[li][c];
            if (!ch_char) ch_char = ' ';
            uint8_t col = term->colors[li][c];
            uint8_t fg_col = col & 0x0F;
            uint32_t fg;
            switch (fg_col) {
                case 0: fg = fb_rgb(0,0,0); break;
                case 1: fg = fb_rgb(0,0,170); break;
                case 2: fg = fb_rgb(0,170,0); break;
                case 3: fg = fb_rgb(0,170,170); break;
                case 4: fg = fb_rgb(170,0,0); break;
                case 5: fg = fb_rgb(170,0,170); break;
                case 6: fg = fb_rgb(170,85,0); break;
                case 7: fg = fb_rgb(170,170,170); break;
                case 8: fg = fb_rgb(85,85,85); break;
                case 9: fg = fb_rgb(85,85,255); break;
                case 10: fg = fb_rgb(85,255,85); break;
                case 11: fg = fb_rgb(85,255,255); break;
                case 12: fg = fb_rgb(255,85,85); break;
                case 13: fg = fb_rgb(255,85,255); break;
                case 14: fg = fb_rgb(255,255,85); break;
                case 15: fg = fb_rgb(255,255,255); break;
                default: fg = fb_rgb(200,200,200); break;
            }
            font_draw_char(cx + c * char_w, y, ch_char, fg, fb_rgb(0,0,0));
        }
    }

    int input_row = rows - 1;
    if (input_row >= 0) {
        int y = cy + input_row * char_h;
        char full_line[TERM_COLS + TERM_INPUT_MAX];
        int fl = 0;
        for (int i = 0; i < term->prompt_len && fl < TERM_COLS + TERM_INPUT_MAX - 1; i++)
            full_line[fl++] = term->prompt[i];
        for (int i = 0; i < term->input_len && fl < TERM_COLS + TERM_INPUT_MAX - 1; i++)
            full_line[fl++] = term->input[i];
        if (term->input_len < term->cursor_pos) term->cursor_pos = term->input_len;
        int cursor_draw = term->cursor_pos + term->prompt_len;
        full_line[fl] = '\0';

        for (int c = 0; c < cols && full_line[c]; c++) {
            uint32_t fg = (c == cursor_draw) ? fb_rgb(0,0,0) : fb_rgb(0,255,0);
            uint32_t bg = (c == cursor_draw) ? fb_rgb(0,255,0) : fb_rgb(0,0,0);
            font_draw_char(cx + c * char_w, y, full_line[c], fg, bg);
        }
    }
}

void terminal_win_key(window_t* win, int key) {
    char c = (char)(key < 0x80 ? key : 0);
    terminal_win_t* term = (terminal_win_t*)win->reserved;
    if (!term) return;

    // Extended keycodes (arrows, etc.)
    if (key >= 0x80) {
        switch (key) {
            case KEY_LEFT:
                if (term->cursor_pos > 0) term->cursor_pos--;
                break;
            case KEY_RIGHT:
                if (term->cursor_pos < term->input_len) term->cursor_pos++;
                break;
            case KEY_HOME:
                term->cursor_pos = 0;
                break;
            case KEY_END:
                term->cursor_pos = term->input_len;
                break;
            case KEY_DEL:
                if (term->cursor_pos < term->input_len) {
                    for (int i = term->cursor_pos; i < term->input_len - 1; i++)
                        term->input[i] = term->input[i + 1];
                    term->input_len--;
                }
                break;
        }
        return;
    }

    if (c == '\b' || c == 0x7F) {
        if (term->cursor_pos > 0) {
            for (int i = term->cursor_pos - 1; i < term->input_len - 1; i++)
                term->input[i] = term->input[i + 1];
            term->input_len--;
            term->cursor_pos--;
        }
        return;
    }

    if (c == '\t') {
        if (term->input_len > 0) {
            int space_pos = -1;
            for (int i = 0; i < term->input_len; i++) {
                if (term->input[i] == ' ') { space_pos = i; break; }
            }
            if (space_pos < 0) {
                char completed[64];
                int match_count = 0;
                command_complete(term->input, completed, sizeof(completed), &match_count);
                if (match_count == 1) {
                    int clen = strlen(completed);
                    term->input_len = clen;
                    memcpy(term->input, completed, clen);
                    term->cursor_pos = clen;
                } else if (match_count > 1) {
                    char matches[256];
                    command_list_matches(term->input, matches, sizeof(matches));
                    term_add_line(term, matches, VGA_LIGHT_CYAN | (VGA_BLACK << 4));
                    if (strlen(completed) > (size_t)term->input_len) {
                        int clen = strlen(completed);
                        term->input_len = clen;
                        memcpy(term->input, completed, clen);
                        term->cursor_pos = clen;
                    }
                }
            }
        }
        return;
    }

    if (c == '\n' || c == '\r') {
        term->input[term->input_len] = '\0';
        char full_line[TERM_COLS + TERM_INPUT_MAX];
        int fl = 0;
        for (int i = 0; i < term->prompt_len; i++)
            full_line[fl++] = term->prompt[i];
        for (int i = 0; i < term->input_len; i++)
            full_line[fl++] = term->input[i];
        full_line[fl] = '\0';
        term_add_line(term, full_line, VGA_LIGHT_GREEN | (VGA_BLACK << 4));

        if (term->input_len > 0) {
            capture_term = term;
            term->capturing = 1;
            term->output_len = 0;
            set_putchar_hook(terminal_capture_putchar);
            execute_command(term->input);
            set_putchar_hook(NULL);
            term->capturing = 0;
            if (term->output_len > 0) {
                term->output_buf[term->output_len] = '\0';
                term_add_line(term, term->output_buf, VGA_LIGHT_GREY | (VGA_BLACK << 4));
                term->output_len = 0;
            }
            capture_term = NULL;
        }

        term->input_len = 0;
        term->cursor_pos = 0;
        return;
    }

    if (c >= 0x20 && c < 0x7F) {
        if (term->input_len < TERM_INPUT_MAX - 1) {
            for (int i = term->input_len; i > term->cursor_pos; i--)
                term->input[i] = term->input[i - 1];
            term->input[term->cursor_pos] = c;
            term->input_len++;
            term->cursor_pos++;
        }
    }
}
