#include "theme.h"
#include "kernel.h"
#include "compositor.h"
#include "editor_win.h"
#include "font.h"

#define TOOLBAR_H 26
#define STATUS_H 18
#define BTN_W 56
#define BTN_H 20

editor_win_t* editor_create_ctx(void) {
    editor_win_t* ed = (editor_win_t*)kmalloc(sizeof(editor_win_t));
    if (!ed) return NULL;
    memset_asm(ed, 0, sizeof(editor_win_t));
    ed->line_count = 1;
    ed->cursor_tick = get_ticks();
    snprintf(ed->status, sizeof(ed->status), "New file");
    return ed;
}

static void editor_save(editor_win_t* ed) {
    char* filename = ed->filename[0] ? ed->filename : "/home/user/untitled.txt";
    // 512 * 256 = 128 KB. This used to be a STACK array, and kernel task stacks
    // are kmalloc(4096) — 4 KB. Every single Save overflowed the kernel stack by
    // a factor of thirty-two, straight through whatever happened to live below
    // it. On the heap it is just a large allocation.
    char* buf = (char*)kmalloc(EDITOR_MAX_LINES * EDITOR_LINE_LEN);
    if (!buf) return;
    const int bufsz = EDITOR_MAX_LINES * EDITOR_LINE_LEN;
    int pos = 0;
    for (int i = 0; i < ed->line_count && pos < bufsz - 2; i++) {
        int len = strlen(ed->lines[i]);
        if (pos + len + 1 >= bufsz) break;
        memcpy_asm(buf + pos, ed->lines[i], len);
        pos += len;
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    if (vfs_write_file(filename, buf, pos) == 0) {
        ed->modified = 0;
        if (!ed->filename[0]) strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
        snprintf(ed->status, sizeof(ed->status), "Saved: %s (%d bytes)", filename, pos);
    } else {
        snprintf(ed->status, sizeof(ed->status), "Save failed: %s", filename);
    }
    kfree(buf);
}

static void editor_open(editor_win_t* ed, const char* path) {
    strncpy(ed->filename, path, sizeof(ed->filename) - 1);
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) {
        ed->line_count = 1;
        ed->lines[0][0] = '\0';
        ed->cursor_x = 0; ed->cursor_y = 0;
        ed->scroll_x = 0; ed->scroll_y = 0;
        ed->modified = 0;
        snprintf(ed->status, sizeof(ed->status), "New: %s", path);
        return;
    }
    uint32_t size = vfs_fsize(fd);
    uint8_t* data = vfs_fdata(fd);
    if (!data || size == 0) {
        vfs_close(fd);
        ed->line_count = 1;
        ed->lines[0][0] = '\0';
        snprintf(ed->status, sizeof(ed->status), "Empty: %s", path);
        return;
    }
    int line = 0, col = 0;
    for (uint32_t i = 0; i < size && line < EDITOR_MAX_LINES; i++) {
        if (data[i] == '\n') {
            ed->lines[line][col] = '\0';
            line++; col = 0;
            if (line < EDITOR_MAX_LINES) ed->lines[line][0] = '\0';
        } else if (data[i] != '\r') {
            if (col < EDITOR_LINE_LEN - 1) {
                ed->lines[line][col++] = data[i];
                ed->lines[line][col] = '\0';
            }
        }
    }
    if (line < EDITOR_MAX_LINES && (col > 0 || line == 0))
        ed->lines[line][col] = '\0';
    ed->line_count = line + 1;
    vfs_close(fd);
    ed->cursor_x = 0; ed->cursor_y = 0;
    ed->scroll_x = 0; ed->scroll_y = 0;
    ed->modified = 0;
    snprintf(ed->status, sizeof(ed->status), "Opened: %s (%u bytes)", path, size);
}

// Public entry point: load `path` into this editor context. Used by the file
// manager to open a file the user picked; wraps the internal loader.
void editor_load_file(editor_win_t* ed, const char* path) {
    if (ed && path && path[0]) editor_open(ed, path);
}

static void editor_insert_char(editor_win_t* ed, char c) {
    int y = ed->cursor_y;
    if (y >= ed->line_count) return;
    int len = strlen(ed->lines[y]);
    if (len < EDITOR_LINE_LEN - 1) {
        for (int i = len; i >= ed->cursor_x; i--)
            ed->lines[y][i + 1] = ed->lines[y][i];
        ed->lines[y][ed->cursor_x] = c;
        if (ed->cursor_x < EDITOR_LINE_LEN - 1) ed->cursor_x++;
        ed->modified = 1;
    }
}

static void editor_newline(editor_win_t* ed) {
    if (ed->line_count >= EDITOR_MAX_LINES) return;
    int y = ed->cursor_y;
    for (int i = ed->line_count; i > y + 1; i--)
        strncpy(ed->lines[i], ed->lines[i - 1], EDITOR_LINE_LEN - 1);
    int rest = strlen(ed->lines[y] + ed->cursor_x);
    memcpy_asm(ed->lines[y + 1], ed->lines[y] + ed->cursor_x, rest + 1);
    ed->lines[y][ed->cursor_x] = '\0';
    ed->line_count++;
    ed->cursor_y++;
    ed->cursor_x = 0;
    ed->modified = 1;
}

static void editor_backspace(editor_win_t* ed) {
    if (ed->cursor_x > 0) {
        int len = strlen(ed->lines[ed->cursor_y]);
        for (int i = ed->cursor_x - 1; i < len; i++)
            ed->lines[ed->cursor_y][i] = ed->lines[ed->cursor_y][i + 1];
        ed->cursor_x--;
        ed->modified = 1;
    } else if (ed->cursor_y > 0) {
        int prev_len = strlen(ed->lines[ed->cursor_y - 1]);
        int cur_len = strlen(ed->lines[ed->cursor_y]);
        if (prev_len + cur_len < EDITOR_LINE_LEN) {
            memcpy_asm(ed->lines[ed->cursor_y - 1] + prev_len, ed->lines[ed->cursor_y], cur_len + 1);
            for (int i = ed->cursor_y; i < ed->line_count - 1; i++)
                strncpy(ed->lines[i], ed->lines[i + 1], EDITOR_LINE_LEN - 1);
            ed->line_count--;
            ed->cursor_y--;
            ed->cursor_x = prev_len;
            ed->modified = 1;
        }
    }
}

static void editor_delete(editor_win_t* ed) {
    int len = strlen(ed->lines[ed->cursor_y]);
    if (ed->cursor_x < len) {
        for (int i = ed->cursor_x; i < len; i++)
            ed->lines[ed->cursor_y][i] = ed->lines[ed->cursor_y][i + 1];
        ed->modified = 1;
    } else if (ed->cursor_y < ed->line_count - 1) {
        int next_len = strlen(ed->lines[ed->cursor_y + 1]);
        if (len + next_len < EDITOR_LINE_LEN) {
            memcpy_asm(ed->lines[ed->cursor_y] + len, ed->lines[ed->cursor_y + 1], next_len + 1);
            for (int i = ed->cursor_y + 1; i < ed->line_count - 1; i++)
                strncpy(ed->lines[i], ed->lines[i + 1], EDITOR_LINE_LEN - 1);
            ed->line_count--;
            ed->modified = 1;
        }
    }
}

static void editor_adjust_scroll(editor_win_t* ed, window_t* win) {
    int max_rows = ((int)win->h - TOOLBAR_H - STATUS_H) / FONT_HEIGHT;
    int max_cols = (((int)win->w - 4) / FONT_WIDTH) - 4;
    if (max_rows < 1) max_rows = 1;
    if (max_cols < 1) max_cols = 1;
    if (ed->cursor_y < ed->scroll_y) ed->scroll_y = ed->cursor_y;
    if (ed->cursor_y >= ed->scroll_y + max_rows) ed->scroll_y = ed->cursor_y - max_rows + 1;
    if (ed->cursor_x < ed->scroll_x) ed->scroll_x = ed->cursor_x;
    if (ed->cursor_x >= ed->scroll_x + max_cols) ed->scroll_x = ed->cursor_x - max_cols + 1;
    if (ed->scroll_y < 0) ed->scroll_y = 0;
    if (ed->scroll_x < 0) ed->scroll_x = 0;
}

void editor_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    editor_win_t* ed = (editor_win_t*)win->reserved;
    if (!ed) return;

    // Toolbar
    fb_fill_rect(cx, cy, cw, TOOLBAR_H, THEME_ROW_DIV);
    // Open button
    fb_fill_rect(cx + 4, cy + 3, BTN_W, BTN_H, THEME_SELECTION);
    font_draw_string(cx + 4 + (BTN_W - 3*FONT_WIDTH)/2, cy + 3 + (BTN_H - FONT_HEIGHT)/2, "Open", fb_rgb(255,255,255), THEME_SELECTION);
    // Save button
    fb_fill_rect(cx + 64, cy + 3, BTN_W, BTN_H, THEME_SELECTION);
    font_draw_string(cx + 64 + (BTN_W - 4*FONT_WIDTH)/2, cy + 3 + (BTN_H - FONT_HEIGHT)/2, "Save", fb_rgb(255,255,255), THEME_SELECTION);
    // Filename
    char name_buf[56];
    snprintf(name_buf, sizeof(name_buf), "%s%s",
             ed->filename[0] ? ed->filename : "untitled",
             ed->modified ? " *" : "");
    uint32_t name_x = cx + 128;
    uint32_t name_y = cy + (TOOLBAR_H - FONT_HEIGHT) / 2;
    font_draw_string(name_x, name_y, name_buf, fb_rgb(200,200,220), THEME_ROW_DIV);

    // Text area
    int text_area_y = cy + TOOLBAR_H;
    int avail_w = (int)cw - 4;
    int avail_h = (int)ch - TOOLBAR_H - STATUS_H;
    int max_cols = avail_w / (int)FONT_WIDTH;
    int max_rows = avail_h / (int)FONT_HEIGHT;
    if (max_cols < 1) max_cols = 1;
    if (max_rows < 1) max_rows = 1;

    fb_fill_rect(cx, text_area_y, cw, avail_h, fb_rgb(40,42,48));

    for (int r = 0; r < max_rows; r++) {
        int idx = r + ed->scroll_y;
        if (idx >= ed->line_count) break;
        int draw_x = cx + 36 - ed->scroll_x * (int)FONT_WIDTH;
        int draw_y = text_area_y + r * (int)FONT_HEIGHT;
        uint32_t line_bg = (idx == ed->cursor_y) ? fb_rgb(55,60,70) : fb_rgb(40,42,48);
        fb_fill_rect(cx + 2, draw_y, (uint32_t)avail_w, FONT_HEIGHT, line_bg);
        // Line number
        char ln[12];
        snprintf(ln, sizeof(ln), "%d", idx + 1);
        font_draw_string(cx + 2, draw_y, ln, fb_rgb(100,120,140), line_bg);
        // Text
        if (draw_x < (int)(cx + cw))
            font_draw_string((uint32_t)draw_x, (uint32_t)draw_y, ed->lines[idx], THEME_TEXT, line_bg);
    }

    // Cursor (blink)
    if ((get_ticks() - ed->cursor_tick) < 500) {
        int cur_x = cx + 36 + ed->cursor_x * (int)FONT_WIDTH - ed->scroll_x * (int)FONT_WIDTH;
        int cur_y = text_area_y + (ed->cursor_y - ed->scroll_y) * (int)FONT_HEIGHT;
        if (cur_y >= text_area_y && cur_y < text_area_y + avail_h &&
            cur_x >= (int)cx + 36 && cur_x < (int)(cx + cw - 2))
            fb_fill_rect((uint32_t)cur_x, (uint32_t)cur_y, 2, FONT_HEIGHT, fb_rgb(255,255,255));
    }

    // Status bar
    int status_y = cy + (int)ch - STATUS_H;
    fb_fill_rect(cx, status_y, cw, STATUS_H, THEME_WINDOW_BG);
    font_draw_string(cx + 4, (uint32_t)status_y + 2, ed->status, fb_rgb(180,200,220), THEME_WINDOW_BG);
}

void editor_win_click(window_t* win, int mx, int my, int btn) {
    editor_win_t* ed = (editor_win_t*)win->reserved;
    if (!ed || btn != 1) return;
    // Toolbar click
    if (my >= WIN_CLIENT_Y(win) && my < WIN_CLIENT_Y(win) + TOOLBAR_H) {
        int rx = mx - WIN_CLIENT_X(win);
        if (rx >= 4 && rx < 4 + BTN_W) {
            // Open
            if (!ed->filename[0]) {
                // Try common paths
                const char* paths[] = {"/home/test.txt", "/test.txt", "/shell.txt", "/README.txt"};
                int found = 0;
                for (int i = 0; i < 4; i++) {
                    int fd = vfs_open(paths[i], 0, 0);
                    if (fd >= 0) {
                        vfs_close(fd);
                        editor_open(ed, paths[i]);
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    snprintf(ed->status, sizeof(ed->status), "No file found. Type path and press Ctrl+O");
            } else {
                editor_open(ed, ed->filename);
            }
            return;
        }
        if (rx >= 64 && rx < 64 + BTN_W) {
            editor_save(ed);
            return;
        }
    }
    // Click in text area → move cursor
    int text_area_y = WIN_CLIENT_Y(win) + TOOLBAR_H;   /* was missing TITLE_H too */
    int avail_h = (int)win->h - TOOLBAR_H - STATUS_H;
    if (my >= text_area_y && my < text_area_y + avail_h && mx >= win->x + 36) {
        int max_rows = avail_h / FONT_HEIGHT;
        if (max_rows < 1) max_rows = 1;
        int row = (my - text_area_y) / FONT_HEIGHT;
        int col = (mx - (win->x + 36)) / FONT_WIDTH;
        ed->cursor_y = ed->scroll_y + row;
        if (ed->cursor_y >= ed->line_count) ed->cursor_y = ed->line_count - 1;
        int len = strlen(ed->lines[ed->cursor_y]);
        ed->cursor_x = ed->scroll_x + col;
        if (ed->cursor_x > len) ed->cursor_x = len;
        ed->cursor_tick = get_ticks();
    }
}

void editor_win_key(window_t* win, int key) {
    editor_win_t* ed = (editor_win_t*)win->reserved;
    if (!ed) return;

    ed->cursor_tick = get_ticks();

    if (key == 0x13) { // Ctrl+S
        editor_save(ed);
        return;
    }
    if (key == 0x0F) { // Ctrl+O
        // Open: if filename ends in .txt or similar, open it
        if (ed->filename[0]) {
            editor_open(ed, ed->filename);
        } else {
            snprintf(ed->status, sizeof(ed->status), "Save file first to set filename, then Ctrl+O to reopen");
        }
        return;
    }

    if (key >= 0x20 && key <= 0x7E) {
        editor_insert_char(ed, (char)key);
    } else if (key == '\r' || key == '\n') {
        editor_newline(ed);
    } else if (key == '\b') {
        editor_backspace(ed);
    } else if (key == KEY_DEL) {
        editor_delete(ed);
    } else if (key == KEY_LEFT) {
        if (ed->cursor_x > 0) ed->cursor_x--;
        else if (ed->cursor_y > 0) { ed->cursor_y--; ed->cursor_x = strlen(ed->lines[ed->cursor_y]); }
    } else if (key == KEY_RIGHT) {
        int len = strlen(ed->lines[ed->cursor_y]);
        if (ed->cursor_x < len) ed->cursor_x++;
        else if (ed->cursor_y < ed->line_count - 1) { ed->cursor_y++; ed->cursor_x = 0; }
    } else if (key == KEY_UP) {
        if (ed->cursor_y > 0) ed->cursor_y--;
    } else if (key == KEY_DOWN) {
        if (ed->cursor_y < ed->line_count - 1) ed->cursor_y++;
    } else if (key == KEY_HOME) {
        ed->cursor_x = 0;
    } else if (key == KEY_END) {
        ed->cursor_x = strlen(ed->lines[ed->cursor_y]);
    } else if (key == KEY_PGUP) {
        int rows = ((int)win->h - TOOLBAR_H - STATUS_H) / FONT_HEIGHT;
        if (rows < 1) rows = 1;
        ed->cursor_y -= rows;
        if (ed->cursor_y < 0) ed->cursor_y = 0;
    } else if (key == KEY_PGDN) {
        int rows = ((int)win->h - TOOLBAR_H - STATUS_H) / FONT_HEIGHT;
        if (rows < 1) rows = 1;
        ed->cursor_y += rows;
        if (ed->cursor_y >= ed->line_count) ed->cursor_y = ed->line_count - 1;
    }

    editor_adjust_scroll(ed, win);
    snprintf(ed->status, sizeof(ed->status), "Ln %d, Col %d",
             ed->cursor_y + 1, ed->cursor_x + 1);
}
