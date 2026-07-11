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

// Rebuild this terminal's prompt from its own current directory. Activating the
// shell's CWD first means the prompt shows the real absolute path (e.g. after a
// `cd`) and each Terminal window tracks its directory independently.
static void term_set_prompt(terminal_win_t* term) {
    vfs_setcwd_node(term->cwd);
    snprintf(term->prompt, sizeof(term->prompt), "nyx:%s$ ", vfs_getcwd());
    term->prompt_len = strlen(term->prompt);
}

terminal_win_t* terminal_create_ctx(void) {
    terminal_win_t* term = (terminal_win_t*)kmalloc(sizeof(terminal_win_t));
    if (!term) return NULL;
    memset_asm(term, 0, sizeof(terminal_win_t));
    term->cwd = vfs_root_node();          // each terminal starts at /
    term_set_prompt(term);
    term->visible_rows = 20;
    term_add_line(term, "NyxOS Terminal v0.2.0", VGA_LIGHT_GREEN | (VGA_BLACK << 4));
    term_add_line(term, "Type 'help' for available commands.", VGA_LIGHT_CYAN | (VGA_BLACK << 4));
    term_add_line(term, "", VGA_LIGHT_GREY | (VGA_BLACK << 4));
    return term;
}

static terminal_win_t* capture_term = NULL;

// ANSI/CSI escape parser for captured TUI output. States: 0 normal, 1 saw ESC,
// 2 inside a CSI collecting numeric params. Recognized finals:
//   H / f  cursor position ESC[row;colH (1-based; default 1,1)  -> screen mode
//   J      ESC[2J clear the screen                              -> screen mode
//   K      ESC[K clear from the cursor to end of line
// The first cursor/clear sequence flips the terminal into screen_mode, where the
// lines[] grid becomes a fixed cell screen addressed by (out_row,out_col) with a
// drawn block cursor — this is what a full-screen editor draws onto.
static int esc_state = 0;
static int esc_p[2];       // collected CSI params
static int esc_np;         // current param index (0 or 1)

// Enter/refresh screen mode: blank the addressable grid, home the output cursor.
static void term_screen_clear(terminal_win_t* t) {
    for (int r = 0; r < TERM_SCREEN_ROWS; r++) {
        memset_asm(t->lines[r], ' ', TERM_COLS);
        memset_asm(t->colors[r], VGA_LIGHT_GREY | (VGA_BLACK << 4), TERM_COLS);
    }
    t->line_count = TERM_SCREEN_ROWS;
    t->scroll_offset = 0;
    t->out_row = t->out_col = 0;
    t->screen_mode = 1;
}

// Reset back to scrollback mode (called when a command finishes).
void terminal_capture_reset(terminal_win_t* t) {
    esc_state = 0;
    if (t && t->screen_mode) { t->screen_mode = 0; t->line_count = 0; }
}

int terminal_capture_putchar(int c) {
    if (!capture_term || !capture_term->capturing) return c;
    terminal_win_t* t = capture_term;

    if (esc_state == 1) {                        // saw ESC: a CSI opens with '['
        if (c == '[') { esc_state = 2; esc_p[0] = esc_p[1] = 0; esc_np = 0; }
        else esc_state = 0;
        return c;
    }
    if (esc_state == 2) {                         // inside CSI
        if (c >= '0' && c <= '9') {               // accumulate a numeric param
            int i = esc_np < 2 ? esc_np : 1;
            esc_p[i] = esc_p[i] * 10 + (c - '0');
            return c;
        }
        if (c == ';') { esc_np++; return c; }
        if (c == 'H' || c == 'f') {               // cursor position (1-based -> 0-based)
            if (!t->screen_mode) term_screen_clear(t);
            t->out_row = esc_p[0] > 0 ? esc_p[0] - 1 : 0;
            t->out_col = esc_p[1] > 0 ? esc_p[1] - 1 : 0;
        } else if (c == 'J') {                    // clear screen
            term_screen_clear(t);
        } else if (c == 'K') {                    // clear to end of line
            if (t->screen_mode && t->out_row >= 0 && t->out_row < TERM_SCREEN_ROWS)
                for (int x = t->out_col; x >= 0 && x < TERM_COLS; x++) t->lines[t->out_row][x] = ' ';
        }
        esc_state = 0;                            // any other final is swallowed
        return c;
    }
    if (c == 0x1B) { esc_state = 1; return c; }   // ESC begins a sequence

    if (t->screen_mode) {                         // cursor-addressed cell writes
        if (c == '\n') { t->out_row++; t->out_col = 0; }
        else if (c == '\r') { t->out_col = 0; }
        else if (c == '\b' || c == 0x7F) { if (t->out_col > 0) t->out_col--; }
        else if (c >= 0x20) {
            if (t->out_row < 0) t->out_row = 0;
            if (t->out_row >= TERM_SCREEN_ROWS) t->out_row = TERM_SCREEN_ROWS - 1;
            if (t->out_col >= 0 && t->out_col < TERM_COLS) {
                t->lines[t->out_row][t->out_col] = (char)c;
                t->colors[t->out_row][t->out_col] = VGA_LIGHT_GREY | (VGA_BLACK << 4);
                t->out_col++;
            }
        }
        return c;
    }

    // Normal (scrollback) mode.
    if (c == '\n') {
        t->output_buf[t->output_len] = '\0';
        if (t->output_len > 0)
            term_add_line(t, t->output_buf, VGA_LIGHT_GREY | (VGA_BLACK << 4));
        t->output_len = 0;
    } else if (c == '\b' || c == 0x7F) {
        // Destructive backspace on the pending line — makes the kernel echo's
        // "\b \b" and the shell line editor's redraws render correctly.
        if (t->output_len > 0) t->output_len--;
    } else if (c == '\r') {
        t->output_len = 0;                        // carriage return: rebuild the line
    } else if (c >= 0x20 && t->output_len < TERM_OUTPUT_MAX - 1) {
        t->output_buf[t->output_len++] = c;
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

    // Screen mode (full-screen TUI): the lines[] grid IS the screen, drawn from
    // row 0. Normal mode: show the tail of the scrollback with the input line last.
    int start_line = term->screen_mode ? 0
                     : term->line_count - rows - 1 - term->scroll_offset;
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
            // Block cursor in screen mode: invert the cell at (out_row,out_col).
            if (term->screen_mode && li == term->out_row && c == term->out_col)
                font_draw_char(cx + c * char_w, y, ch_char, fb_rgb(0,0,0), fb_rgb(200,200,200));
            else
                font_draw_char(cx + c * char_w, y, ch_char, fg, fb_rgb(0,0,0));
        }
    }

    int input_row = rows - 1;
    if (!term->screen_mode && input_row >= 0) {
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
            vfs_setcwd_node(term->cwd);       // run in THIS shell's directory
            set_putchar_hook(terminal_capture_putchar);
            execute_command(term->input);
            set_putchar_hook(NULL);
            term->capturing = 0;
            terminal_capture_reset(term);      // leave TUI screen mode -> clean scrollback
            term->cwd = vfs_getcwd_node();     // a `cd` may have moved us
            term_set_prompt(term);             // reflect the new path in the prompt
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
