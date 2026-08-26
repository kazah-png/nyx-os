#include "../core/theme.h"
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "editor_win.h"
#include "../../drivers/video/font.h"
#include "../../auth/login.h"   // g_login_home: default new saves into the persistent home

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
    // A never-named buffer saves into the logged-in user's persistent home
    // (/mnt/home/<user> on the ext2 disk when a disk is mounted) so a new note
    // survives a reboot -- the old default was the ephemeral RAM path /home/user/.
    char defbuf[128];
    snprintf(defbuf, sizeof(defbuf), "%s/untitled.txt", g_login_home[0] ? g_login_home : "/home");
    char* filename = ed->filename[0] ? ed->filename : defbuf;
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
    // vfs_write_file returns the number of bytes written on success (== pos here) and
    // a negative value on failure; it does NOT return 0 on success, so the old `== 0`
    // check reported every non-empty save as "Save failed" even though it was written.
    if (vfs_write_file(filename, buf, pos) == pos) {
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
    // A file with >= EDITOR_MAX_LINES newlines makes the load loop stop with line ==
    // EDITOR_MAX_LINES, so `line + 1` would set line_count one past the lines[] array.
    // line_count bounds cursor movement and gates editor_insert_char, so that lets the
    // cursor reach row EDITOR_MAX_LINES and read/write ed->lines[EDITOR_MAX_LINES] — which
    // aliases line_count and overruns the struct (a heap OOB). Clamp, like editor_newline.
    ed->line_count = (line < EDITOR_MAX_LINES) ? line + 1 : EDITOR_MAX_LINES;
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

    // Status bar: the message on the left, and the cursor position (with a `*` when the buffer
    // has unsaved edits) right-aligned — the Ln/Col readout every real editor shows.
    int status_y = cy + (int)ch - STATUS_H;
    fb_fill_rect(cx, status_y, cw, STATUS_H, THEME_WINDOW_BG);
    font_draw_string(cx + 4, (uint32_t)status_y + 2, ed->status, fb_rgb(180,200,220), THEME_WINDOW_BG);

    char pos[40];
    snprintf(pos, sizeof(pos), "%s Ln %d, Col %d", ed->modified ? "*" : " ",
             ed->cursor_y + 1, ed->cursor_x + 1);
    int pw = (int)strlen(pos) * FONT_WIDTH;
    int px = cx + (int)cw - 6 - pw;
    if (px < cx + 4) px = cx + 4;
    font_draw_string((uint32_t)px, (uint32_t)status_y + 2, pos, fb_rgb(150,185,215), THEME_WINDOW_BG);
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

// First index >= `start` where `pat` occurs in NUL-terminated `s`, or -1.
static int editor_line_find(const char* s, int start, const char* pat) {
    int slen = (int)strlen(s), plen = (int)strlen(pat);
    if (start < 0) start = 0;
    for (int i = start; i + plen <= slen; i++) {
        int k = 0;
        while (k < plen && s[i + k] == pat[k]) k++;
        if (k == plen) return i;
    }
    return -1;
}

// Search `lines[0..count)` for `pat`, starting at (from_y, from_x) and wrapping once to
// the top so every position is visited exactly once. On a match sets *my/*mx and returns
// 1; returns 0 if `pat` is empty or not found anywhere. Pure — pinned by editor_find_selftest.
int editor_find_in(char lines[][EDITOR_LINE_LEN], int count, int from_y, int from_x,
                   const char* pat, int* my, int* mx) {
    if (!pat || !pat[0] || count <= 0) return 0;
    if (from_y < 0 || from_y >= count) from_y = 0;
    for (int i = 0; i <= count; i++) {
        int y = (from_y + i) % count;
        int start = (i == 0) ? from_x : 0;
        int hit = editor_line_find(lines[y], start, pat);
        if (hit >= 0) { if (my) *my = y; if (mx) *mx = hit; return 1; }
        if (i == count) break;                     // completed the wrap back onto from_y
    }
    return 0;
}

// KAT: forward search, next-after-cursor, a later line, not-found, and the wrap-around.
int editor_find_selftest(void) {
    static char L[4][EDITOR_LINE_LEN];
    strcpy(L[0], "the quick brown fox");
    strcpy(L[1], "jumps over the lazy");
    strcpy(L[2], "dog and the cat");
    L[3][0] = '\0';
    int my = 0, mx = 0;
    if (!editor_find_in(L, 4, 0, 0, "the",   &my, &mx) || my != 0 || mx != 0)  return 1;
    if (!editor_find_in(L, 4, 0, 1, "the",   &my, &mx) || my != 1 || mx != 11) return 2;
    if (!editor_find_in(L, 4, 0, 0, "cat",   &my, &mx) || my != 2 || mx != 12) return 3;
    if ( editor_find_in(L, 4, 0, 0, "zebra", &my, &mx))                         return 4;
    if (!editor_find_in(L, 4, 2, 0, "quick", &my, &mx) || my != 0 || mx != 4)  return 5;
    return 0;
}

// Replace every non-overlapping occurrence of `pat` with `with` in NUL-terminated `line`
// (capacity EDITOR_LINE_LEN). Returns the number of replacements made. Builds the result in
// a scratch buffer and never overflows: if the growing result would exceed the buffer it
// stops replacing and copies the remaining input raw. The replacement is not re-scanned, so
// a `with` that contains `pat` can't loop. Pure — pinned by editor_replace_selftest.
static int editor_line_replace(char* line, const char* pat, const char* with) {
    int plen = (int)strlen(pat);
    if (plen == 0) return 0;
    int wlen = (int)strlen(with), llen = (int)strlen(line);
    char out[EDITOR_LINE_LEN];
    int oi = 0, i = 0, n = 0;
    while (i < llen) {
        int k = 0;
        while (k < plen && line[i + k] == pat[k]) k++;
        if (k == plen) {
            if (oi + wlen >= EDITOR_LINE_LEN) break;   // would overflow — stop replacing
            memcpy_asm(out + oi, with, wlen);
            oi += wlen; i += plen; n++;
        } else {
            if (oi + 1 >= EDITOR_LINE_LEN) break;
            out[oi++] = line[i++];
        }
    }
    while (i < llen && oi + 1 < EDITOR_LINE_LEN) out[oi++] = line[i++];  // raw tail if we broke early
    out[oi] = '\0';
    if (n > 0) memcpy_asm(line, out, oi + 1);
    return n;
}

// Replace `pat` with `with` across every line of `lines[0..count)`. Returns the total number
// of replacements. Empty pattern or empty file → 0, no change. Pure — KAT-able off-target.
static int editor_replace_all(char lines[][EDITOR_LINE_LEN], int count,
                              const char* pat, const char* with) {
    if (!pat || !pat[0] || count <= 0) return 0;
    int total = 0;
    for (int y = 0; y < count; y++) total += editor_line_replace(lines[y], pat, with);
    return total;
}

// KAT: multi-line replace, same-length / growing / shrinking replacements, empty pattern,
// no-match, and a replacement that contains the pattern (must not re-scan or loop).
int editor_replace_selftest(void) {
    static char L[4][EDITOR_LINE_LEN];
    strcpy(L[0], "the cat sat on the mat");
    strcpy(L[1], "cat cat cat");
    strcpy(L[2], "no match here");
    L[3][0] = '\0';
    if (editor_replace_all(L, 4, "cat", "dog") != 4)         return 1;   // 1 + 3 across two lines
    if (strcmp(L[0], "the dog sat on the mat"))              return 2;
    if (strcmp(L[1], "dog dog dog"))                         return 3;
    if (strcmp(L[2], "no match here"))                       return 4;   // untouched
    if (editor_replace_all(L, 4, "dog", "puppy") != 4)       return 5;   // grow 3->5
    if (strcmp(L[1], "puppy puppy puppy"))                   return 6;
    if (editor_replace_all(L, 4, "puppy", "x") != 4)         return 7;   // shrink 5->1
    if (strcmp(L[1], "x x x"))                               return 8;
    if (editor_replace_all(L, 4, "", "z") != 0)              return 9;   // empty pattern: no-op
    if (editor_replace_all(L, 4, "zzz", "q") != 0)           return 10;  // not present: no-op
    if (editor_replace_all(L, 4, "x", "xx") != 4)            return 11;  // 1 in L0 + 3 in L1
    if (strcmp(L[1], "xx xx xx"))                            return 12;  // replacement holds pattern, no loop
    return 0;
}

void editor_win_key(window_t* win, int key) {
    editor_win_t* ed = (editor_win_t*)win->reserved;
    if (!ed) return;

    ed->cursor_tick = get_ticks();

    // Ctrl+F — enter incremental find mode (type a pattern, Enter = jump to next match,
    // Enter again = keep advancing, Esc = back to editing).
    if (key == 0x06 && !ed->repl_active) {
        ed->find_active = 1; ed->find_len = 0; ed->find_pat[0] = '\0';
        snprintf(ed->status, sizeof(ed->status), "Find: ");
        return;
    }
    if (ed->find_active) {
        if (key == 0x1B) {                          // Esc — leave find mode
            ed->find_active = 0;
            ed->status[0] = 0;                       // clear the "Find:" prompt (Ln/Col shows in the bar)
            return;
        }
        if (key == '\r' || key == '\n') {           // Enter — jump to the next match
            if (ed->find_len == 0) return;
            int my, mx;
            if (editor_find_in(ed->lines, ed->line_count, ed->cursor_y, ed->cursor_x + 1,
                               ed->find_pat, &my, &mx)) {
                ed->cursor_y = my; ed->cursor_x = mx;
                editor_adjust_scroll(ed, win);
                snprintf(ed->status, sizeof(ed->status), "Found '%s' (Ln %d)", ed->find_pat, my + 1);
            } else {
                snprintf(ed->status, sizeof(ed->status), "Not found: %s", ed->find_pat);
            }
            return;
        }
        if (key == '\b') {                          // edit the pattern
            if (ed->find_len > 0) ed->find_pat[--ed->find_len] = '\0';
            snprintf(ed->status, sizeof(ed->status), "Find: %s", ed->find_pat);
            return;
        }
        if (key >= 0x20 && key <= 0x7E && ed->find_len < (int)sizeof(ed->find_pat) - 1) {
            ed->find_pat[ed->find_len++] = (char)key;
            ed->find_pat[ed->find_len] = '\0';
            snprintf(ed->status, sizeof(ed->status), "Find: %s", ed->find_pat);
        }
        return;                                     // swallow every other key while finding
    }

    // Ctrl+R — find-and-replace. Phase 1: type the text to find, Enter. Phase 2: type the
    // replacement, Enter = replace every occurrence across the file. Esc cancels either phase.
    if (key == 0x12) {
        ed->repl_active = 1;
        ed->repl_find_len = 0; ed->repl_find[0] = '\0';
        ed->repl_with_len = 0; ed->repl_with[0] = '\0';
        snprintf(ed->status, sizeof(ed->status), "Replace - find: ");
        return;
    }
    if (ed->repl_active) {
        if (key == 0x1B) {                          // Esc — cancel
            ed->repl_active = 0;
            ed->status[0] = 0;                       // clear the "Replace:" prompt
            return;
        }
        if (key == '\r' || key == '\n') {
            if (ed->repl_active == 1) {             // finished the find text → ask for replacement
                if (ed->repl_find_len == 0) return; // need something to find
                ed->repl_active = 2;
                snprintf(ed->status, sizeof(ed->status), "Replace '%s' with: ", ed->repl_find);
                return;
            }
            int n = editor_replace_all(ed->lines, ed->line_count, ed->repl_find, ed->repl_with);
            if (n > 0) ed->modified = 1;
            if (ed->cursor_y >= ed->line_count) ed->cursor_y = ed->line_count - 1;   // clamp after edits
            int llen = strlen(ed->lines[ed->cursor_y]);
            if (ed->cursor_x > llen) ed->cursor_x = llen;
            ed->repl_active = 0;
            snprintf(ed->status, sizeof(ed->status), "Replaced %d occurrence%s of '%s'",
                     n, n == 1 ? "" : "s", ed->repl_find);
            return;
        }
        if (key == '\b') {                          // edit whichever field is active
            if (ed->repl_active == 1) {
                if (ed->repl_find_len > 0) ed->repl_find[--ed->repl_find_len] = '\0';
                snprintf(ed->status, sizeof(ed->status), "Replace - find: %s", ed->repl_find);
            } else {
                if (ed->repl_with_len > 0) ed->repl_with[--ed->repl_with_len] = '\0';
                snprintf(ed->status, sizeof(ed->status), "Replace '%s' with: %s", ed->repl_find, ed->repl_with);
            }
            return;
        }
        if (key >= 0x20 && key <= 0x7E) {
            if (ed->repl_active == 1 && ed->repl_find_len < (int)sizeof(ed->repl_find) - 1) {
                ed->repl_find[ed->repl_find_len++] = (char)key; ed->repl_find[ed->repl_find_len] = '\0';
                snprintf(ed->status, sizeof(ed->status), "Replace - find: %s", ed->repl_find);
            } else if (ed->repl_active == 2 && ed->repl_with_len < (int)sizeof(ed->repl_with) - 1) {
                ed->repl_with[ed->repl_with_len++] = (char)key; ed->repl_with[ed->repl_with_len] = '\0';
                snprintf(ed->status, sizeof(ed->status), "Replace '%s' with: %s", ed->repl_find, ed->repl_with);
            }
        }
        return;                                     // swallow every other key while replacing
    }

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
    // (The live Ln/Col is drawn in the status bar every frame, so we no longer overwrite the
    // status MESSAGE on each keystroke — "Saved: ..." / "Opened: ..." now persists.)
}
