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

// A VGA color index (0-15) -> RGB, for both foreground and background cells.
static uint32_t vga_to_rgb(uint8_t idx) {
    switch (idx & 0x0F) {
        case 0:  return fb_rgb(0,0,0);
        case 1:  return fb_rgb(0,0,170);
        case 2:  return fb_rgb(0,170,0);
        case 3:  return fb_rgb(0,170,170);
        case 4:  return fb_rgb(170,0,0);
        case 5:  return fb_rgb(170,0,170);
        case 6:  return fb_rgb(170,85,0);
        case 7:  return fb_rgb(170,170,170);
        case 8:  return fb_rgb(85,85,85);
        case 9:  return fb_rgb(85,85,255);
        case 10: return fb_rgb(85,255,85);
        case 11: return fb_rgb(85,255,255);
        case 12: return fb_rgb(255,85,85);
        case 13: return fb_rgb(255,85,255);
        case 14: return fb_rgb(255,255,85);
        default: return fb_rgb(255,255,255);
    }
}

// ANSI SGR color -> VGA attribute nibble. Index by (ansi_code - 30):
//   blk  red  grn  yel  blu  mag  cyn  wht
static const uint8_t ansi2vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

// Apply the collected SGR params to the terminal's current color (fg = low nibble,
// bg = high nibble). 0 resets, 1 = bold (bright fg), 7 = reverse (swap fg/bg),
// 30-37/90-97 set fg, 40-47/100-107 set bg.
static void term_apply_sgr(terminal_win_t* t, const int* p, int n) {
    if (n <= 0) { t->cur_color = TERM_DEFAULT_COLOR; return; }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0)                    t->cur_color = TERM_DEFAULT_COLOR;
        else if (v == 1)               t->cur_color |= 0x08;                                   // bold -> bright fg
        else if (v == 7)               t->cur_color = (uint8_t)(((t->cur_color & 0x0F) << 4) | ((t->cur_color >> 4) & 0x0F));
        else if (v >= 30 && v <= 37)   t->cur_color = (uint8_t)((t->cur_color & 0xF0) |  ansi2vga[v - 30]);
        else if (v >= 90 && v <= 97)   t->cur_color = (uint8_t)((t->cur_color & 0xF0) | (ansi2vga[v - 90] | 0x08));
        else if (v >= 40 && v <= 47)   t->cur_color = (uint8_t)((t->cur_color & 0x0F) | (ansi2vga[v - 40] << 4));
        else if (v >= 100 && v <= 107) t->cur_color = (uint8_t)((t->cur_color & 0x0F) | ((ansi2vga[v - 100] | 0x08) << 4));
    }
}

// Append a scrollback line carrying per-char colors (from the capture buffers),
// so a colorized command (e.g. `ls --color`) keeps its colors in history.
static void term_add_line_c(terminal_win_t* term, const char* text, const uint8_t* colorbuf, int len) {
    if (term->line_count >= TERM_LINES) {
        for (int i = 1; i < TERM_LINES; i++) {
            memcpy(term->lines[i-1], term->lines[i], TERM_COLS);
            memcpy(term->colors[i-1], term->colors[i], TERM_COLS);
        }
        term->line_count = TERM_LINES - 1;
    }
    int idx = term->line_count;
    memset_asm(term->lines[idx], ' ', TERM_COLS);
    memset_asm(term->colors[idx], TERM_DEFAULT_COLOR, TERM_COLS);
    for (int i = 0; i < TERM_COLS - 1 && i < len; i++) {
        term->lines[idx][i] = text[i];
        term->colors[idx][i] = colorbuf[i];
    }
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
    if (t) { t->cur_color = TERM_DEFAULT_COLOR; if (t->screen_mode) { t->screen_mode = 0; t->line_count = 0; } }
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
        } else if (c == 'J') {                    // erase display
            if (esc_p[0] == 3 && !t->screen_mode) {   // ESC[3J: wipe scrollback, stay normal
                t->line_count = 0;                    // (userspace `clear` — composes in the shell)
                t->scroll_offset = 0;
            } else {                                   // ESC[2J / ESC[J: full-screen clear (TUI)
                term_screen_clear(t);
            }
        } else if (c == 'K') {                    // clear to end of line
            if (t->screen_mode && t->out_row >= 0 && t->out_row < TERM_SCREEN_ROWS)
                for (int x = t->out_col; x >= 0 && x < TERM_COLS; x++) {
                    t->lines[t->out_row][x] = ' ';
                    t->colors[t->out_row][x] = t->cur_color;
                }
        } else if (c == 'm') {                    // SGR: set color (fg/bg)
            term_apply_sgr(t, esc_p, (esc_np + 1 > 2) ? 2 : esc_np + 1);
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
                t->colors[t->out_row][t->out_col] = t->cur_color;
                t->out_col++;
            }
        }
        return c;
    }

    // Normal (scrollback) mode.
    if (c == '\n') {
        t->output_buf[t->output_len] = '\0';
        if (t->output_len > 0)
            term_add_line_c(t, t->output_buf, t->out_color, t->output_len);
        t->output_len = 0;
    } else if (c == '\b' || c == 0x7F) {
        // Destructive backspace on the pending line — makes the kernel echo's
        // "\b \b" and the shell line editor's redraws render correctly.
        if (t->output_len > 0) t->output_len--;
    } else if (c == '\r') {
        t->output_len = 0;                        // carriage return: rebuild the line
    } else if (c >= 0x20 && t->output_len < TERM_OUTPUT_MAX - 1) {
        t->out_color[t->output_len] = t->cur_color;   // per-char color (ls --color)
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
            uint32_t fg = vga_to_rgb(col & 0x0F);
            uint32_t bg = vga_to_rgb((col >> 4) & 0x0F);   // background from the high nibble
            // Block cursor in screen mode: invert the cell at (out_row,out_col).
            if (term->screen_mode && li == term->out_row && c == term->out_col)
                font_draw_char(cx + c * char_w, y, ch_char, bg, fg);
            else
                font_draw_char(cx + c * char_w, y, ch_char, fg, bg);
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

    // Scrollbar on the right edge when there's more history than fits (skip in TUI
    // screen mode). Thumb height ~ visible/total; its position tracks how far up the
    // view is scrolled, and it turns purple while scrolled off the live tail.
    if (!term->screen_mode && term->line_count > rows) {
        int total = term->line_count;
        int bx = cx + cw - 3;
        fb_fill_rect(bx, cy, 3, ch, fb_rgb(35, 35, 45));            // track
        int th = (int)((uint64_t)ch * rows / total);
        if (th < 10) th = 10;
        if (th > (int)ch) th = ch;
        int max_start = total - rows;
        if (max_start < 1) max_start = 1;
        int start = start_line < 0 ? 0 : (start_line > max_start ? max_start : start_line);
        int ty = cy + (int)((uint64_t)(ch - th) * start / max_start);
        uint32_t thumb = term->scroll_offset > 0 ? fb_rgb(150, 130, 220)   // scrolled: purple
                                                 : fb_rgb(90, 90, 110);     // at the tail: dim
        fb_fill_rect(bx, ty, 3, th, thumb);
    }
}

// Scroll the scrollback VIEW by `delta` lines (positive = toward older output). No-op
// in TUI screen mode. Clamped so you can't scroll below the live tail (offset 0) or
// past the oldest kept line.
static void term_scroll(terminal_win_t* t, int delta) {
    if (t->screen_mode) return;
    int max_off = t->line_count - t->visible_rows;
    if (max_off < 0) max_off = 0;
    t->scroll_offset += delta;
    if (t->scroll_offset < 0) t->scroll_offset = 0;
    if (t->scroll_offset > max_off) t->scroll_offset = max_off;
}

void terminal_win_key(window_t* win, int key) {
    terminal_win_t* term = (terminal_win_t*)win->reserved;
    if (!term) return;

    // Scrollback navigation — moves the VIEW only (not the input line) and never snaps
    // back to the tail. A page is one screenful minus a row of overlap; a wheel notch
    // is a few lines. PgUp/PgDn come from the keyboard, WHEEL_* from the compositor.
    int page = term->visible_rows > 3 ? term->visible_rows - 2 : 1;
    switch (key) {
        case KEY_PGUP:       term_scroll(term,  page); return;
        case KEY_PGDN:       term_scroll(term, -page); return;
        case KEY_WHEEL_UP:   term_scroll(term,  3);    return;
        case KEY_WHEEL_DOWN: term_scroll(term, -3);    return;
    }

    // Any other key snaps back to the live tail — typing/Enter always jumps to bottom.
    term->scroll_offset = 0;

    char c = (char)(key < 0x80 ? key : 0);

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
            term->cur_color = TERM_DEFAULT_COLOR;   // fresh color state per command
            vfs_setcwd_node(term->cwd);       // run in THIS shell's directory
            set_putchar_hook(terminal_capture_putchar);
            execute_command(term->input);
            set_putchar_hook(NULL);
            term->capturing = 0;
            int was_screen = term->screen_mode;
            if (term->output_len > 0 && !was_screen) {   // flush a trailing (unterminated) line
                term->output_buf[term->output_len] = '\0';
                term_add_line_c(term, term->output_buf, term->out_color, term->output_len);
                term->output_len = 0;
            }
            terminal_capture_reset(term);      // leave TUI screen mode -> clean scrollback
            term->cwd = vfs_getcwd_node();     // a `cd` may have moved us
            term_set_prompt(term);             // reflect the new path in the prompt
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
