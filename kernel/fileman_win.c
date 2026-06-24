#include "kernel.h"
#include "compositor.h"
#include "fileman_win.h"
#include "font.h"

#define TOOLBAR_H 28
#define BTN_SPACE 6
#define HEADER_H  18

fileman_win_t* fileman_create_ctx(void) {
    fileman_win_t* fm = (fileman_win_t*)kmalloc(sizeof(fileman_win_t));
    if (!fm) return NULL;
    memset_asm(fm, 0, sizeof(fileman_win_t));
    strncpy(fm->cwd, "/", sizeof(fm->cwd));
    fm->sel_index = -1;
    fm->input_mode = 0;
    fm->input_cursor_tick = get_ticks();
    snprintf(fm->status, sizeof(fm->status), "Ready");
    return fm;
}

void fileman_refresh(fileman_win_t* fm) {
    fm->entry_count = 0;
    fm->scroll_offset = 0;
    int fd = vfs_open(fm->cwd, 0, 0);
    if (fd < 0) {
        snprintf(fm->status, sizeof(fm->status), "Cannot open: %s", fm->cwd);
        return;
    }
    dirent_t* de = vfs_readdir(fd);
    while (de && fm->entry_count < FILEMAN_MAX_ENTRIES) {
        strncpy(fm->entries[fm->entry_count], de->name, 63);
        fm->entries[fm->entry_count][63] = '\0';
        fm->entry_types[fm->entry_count] = de->type;
        fm->entry_count++;
        de = vfs_readdir(fd);
    }
    vfs_close(fd);
    snprintf(fm->status, sizeof(fm->status), "%d entries", fm->entry_count);
}

static void fileman_cd(fileman_win_t* fm, const char* dir, window_t* win) {
    (void)win;
    char newpath[256];
    if (strcmp(dir, "..") == 0) {
        if (strcmp(fm->cwd, "/") == 0) return;
        char* last_slash = NULL;
        for (int i = 0; fm->cwd[i]; i++)
            if (fm->cwd[i] == '/') last_slash = &fm->cwd[i];
        if (last_slash && last_slash > fm->cwd) {
            *last_slash = '\0';
            strncpy(newpath, fm->cwd, sizeof(newpath));
            *last_slash = '/';
        } else {
            strncpy(newpath, "/", sizeof(newpath));
        }
    } else if (dir[0] == '/') {
        strncpy(newpath, dir, sizeof(newpath));
    } else {
        if (strcmp(fm->cwd, "/") == 0)
            snprintf(newpath, sizeof(newpath), "/%s", dir);
        else
            snprintf(newpath, sizeof(newpath), "%s/%s", fm->cwd, dir);
    }
    newpath[255] = '\0';
    int fd = vfs_open(newpath, 0, 0);
    if (fd >= 0) {
        vfs_close(fd);
        strncpy(fm->cwd, newpath, sizeof(fm->cwd));
        fm->cwd[255] = '\0';
        fileman_refresh(fm);
    } else {
        snprintf(fm->status, sizeof(fm->status), "Cannot enter: %s", newpath);
    }
}

static int fileman_get_path(fileman_win_t* fm, const char* name, char* path, int maxlen) {
    if (strcmp(fm->cwd, "/") == 0)
        snprintf(path, maxlen, "/%s", name);
    else
        snprintf(path, maxlen, "%s/%s", fm->cwd, name);
    return 0;
}

static void fileman_delete(fileman_win_t* fm) {
    if (fm->sel_index < 0 || fm->sel_index >= fm->entry_count) {
        snprintf(fm->status, sizeof(fm->status), "No file selected");
        return;
    }
    char path[256];
    fileman_get_path(fm, fm->entries[fm->sel_index], path, sizeof(path));
    if (vfs_unlink(path) == 0) {
        snprintf(fm->status, sizeof(fm->status), "Deleted: %s", fm->entries[fm->sel_index]);
        fm->sel_index = -1;
        fileman_refresh(fm);
    } else {
        snprintf(fm->status, sizeof(fm->status), "Cannot delete: %s", fm->entries[fm->sel_index]);
    }
}

void fileman_new_folder(fileman_win_t* fm) {
    fm->input_mode = 2;
    memset_asm(fm->input_buf, 0, sizeof(fm->input_buf));
    fm->input_pos = 0;
    fm->input_cursor_tick = get_ticks();
    snprintf(fm->status, sizeof(fm->status), "Enter folder name:");
}

void fileman_new_file(fileman_win_t* fm) {
    fm->input_mode = 1;
    memset_asm(fm->input_buf, 0, sizeof(fm->input_buf));
    fm->input_pos = 0;
    fm->input_cursor_tick = get_ticks();
    snprintf(fm->status, sizeof(fm->status), "Enter file name:");
}



void fileman_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;
    uint32_t char_h = FONT_HEIGHT;

    fb_fill_rect(cx, cy, cw, ch, fb_rgb(30,30,35));

    // Toolbar
    int tb_y = cy;
    fb_fill_rect(cx, tb_y, cw, TOOLBAR_H, fb_rgb(50,55,65));
    fb_fill_rect(cx, tb_y + TOOLBAR_H - 1, cw, 1, fb_rgb(70,70,80));

    int bx = cx + 4;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, fb_rgb(60,70,80));
    font_draw_string(bx + (80 - strlen("New File") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "New File", fb_rgb(220,220,220), fb_rgb(60,70,80));
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, fb_rgb(60,70,80));
    font_draw_string(bx + (80 - strlen("New Dir") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "New Dir", fb_rgb(220,220,220), fb_rgb(60,70,80));
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, fb_rgb(80,50,50));
    font_draw_string(bx + (80 - strlen("Delete") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Delete", fb_rgb(220,220,220), fb_rgb(80,50,50));
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, fb_rgb(60,70,80));
    font_draw_string(bx + (80 - strlen("Go Up") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Go Up", fb_rgb(220,220,220), fb_rgb(60,70,80));
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, fb_rgb(60,70,80));
    font_draw_string(bx + (80 - strlen("Refresh") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Refresh", fb_rgb(220,220,220), fb_rgb(60,70,80));

    // Directory path bar
    int path_y = tb_y + TOOLBAR_H;
    fb_fill_rect(cx, path_y, cw, HEADER_H, fb_rgb(40,45,55));
    font_draw_string(cx + 4, path_y + (HEADER_H - char_h) / 2, fm->cwd, fb_rgb(200,200,255), fb_rgb(40,45,55));

    // Header row
    int list_header_y = path_y + HEADER_H;
    fb_fill_rect(cx, list_header_y, cw, char_h + 2, fb_rgb(50,55,65));
    font_draw_string(cx + 4, list_header_y + 1, "Name", fb_rgb(255,255,255), fb_rgb(50,55,65));

    // File listing
    int list_y = list_header_y + char_h + 4;
    int avail_h = (int)(cy + ch - list_y - HEADER_H - 4);
    int max_rows = avail_h / (int)char_h;
    if (max_rows < 1) max_rows = 1;

    for (int i = fm->scroll_offset; i < fm->entry_count && (i - fm->scroll_offset) < max_rows; i++) {
        int row = i - fm->scroll_offset;
        int ey = list_y + row * char_h;
        uint32_t bg = (i == fm->sel_index) ? fb_rgb(60,80,120) : fb_rgb(30,30,35);
        fb_fill_rect(cx + 2, ey, cw - 4, char_h, bg);

        char prefix = fm->entry_types[i] ? '/' : ' ';
        char display[68];
        snprintf(display, sizeof(display), "%c %s", prefix, fm->entries[i]);
        uint32_t fg = fm->entry_types[i] ? fb_rgb(100,200,255) : fb_rgb(220,220,220);
        font_draw_string(cx + 4, ey, display, fg, bg);
    }

    // Status bar
    int status_y = cy + ch - HEADER_H;
    fb_fill_rect(cx, status_y, cw, HEADER_H, fb_rgb(40,45,55));

    if (fm->input_mode) {
        // Show input prompt
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "%s %s_", fm->status, fm->input_buf);
        font_draw_string(cx + 4, status_y + (HEADER_H - char_h) / 2, prompt, fb_rgb(255,255,100), fb_rgb(40,45,55));
    } else {
        font_draw_string(cx + 4, status_y + (HEADER_H - char_h) / 2, fm->status, fb_rgb(180,180,180), fb_rgb(40,45,55));
    }
}

void fileman_win_click(window_t* win, int mx, int my) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;
    int cx = win->x, cy = win->y + TITLE_H;
    uint32_t cw = win->w, ch = win->h;
    uint32_t char_h = FONT_HEIGHT;

    if (fm->entry_count == 0) fileman_refresh(fm);

    // Toolbar clicks
    int tb_y = cy;
    if (my >= tb_y && my < tb_y + TOOLBAR_H) {
        int rel_x = mx - cx;
        if (rel_x >= 4 && rel_x < 84) { fileman_new_file(fm); return; }
        if (rel_x >= 88 && rel_x < 168) { fileman_new_folder(fm); return; }
        if (rel_x >= 172 && rel_x < 252) { fileman_delete(fm); return; }
        if (rel_x >= 256 && rel_x < 336) { fileman_cd(fm, "..", win); return; }
        if (rel_x >= 340 && rel_x < 420) { fileman_refresh(fm); return; }
    }

    // File list area
    int list_header_y = cy + TOOLBAR_H + HEADER_H;
    int list_y = list_header_y + char_h + 4;
    int avail_h = (int)(ch - TOOLBAR_H - HEADER_H - char_h - 4 - HEADER_H - 4);
    int max_rows = avail_h / (int)char_h;
    if (max_rows < 1) max_rows = 1;

    if (my >= list_y && my < list_y + max_rows * (int)char_h) {
        int idx = (my - list_y) / (int)char_h + fm->scroll_offset;
        if (idx >= 0 && idx < fm->entry_count) {
            fm->sel_index = idx;
            if (fm->entry_types[idx]) {
                fileman_cd(fm, fm->entries[idx], win);
            } else {
                char path[256];
                fileman_get_path(fm, fm->entries[idx], path, sizeof(path));
                int fd = vfs_open(path, 0, 0);
                if (fd >= 0) {
                    char buf[512];
                    int n = vfs_read(fd, buf, sizeof(buf)-1);
                    vfs_close(fd);
                    if (n > 0) {
                        buf[n] = '\0';
                        snprintf(fm->status, sizeof(fm->status), "%s (%d bytes): %.200s", fm->entries[idx], n, buf);
                    } else {
                        snprintf(fm->status, sizeof(fm->status), "%s (empty)", fm->entries[idx]);
                    }
                }
            }
        }
    }
}

void fileman_win_key(window_t* win, int key) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm || !fm->input_mode) return;

    if (key == '\n') {
        // Commit input
        if (fm->input_pos > 0) {
            char path[256];
            fileman_get_path(fm, fm->input_buf, path, sizeof(path));
            if (fm->input_mode == 2) {
                // Create directory
                if (vfs_mkdir(path, 0) == 0) {
                    snprintf(fm->status, sizeof(fm->status), "Created folder: %s", fm->input_buf);
                } else {
                    snprintf(fm->status, sizeof(fm->status), "Cannot create folder: %s", fm->input_buf);
                }
            } else {
                // Create file (empty)
                int fd = vfs_open(path, 1, 0);
                if (fd >= 0) {
                    vfs_close(fd);
                    snprintf(fm->status, sizeof(fm->status), "Created file: %s", fm->input_buf);
                } else {
                    snprintf(fm->status, sizeof(fm->status), "Cannot create file: %s", fm->input_buf);
                }
            }
            fileman_refresh(fm);
        }
        fm->input_mode = 0;
        fm->input_buf[0] = '\0';
        return;
    }

    if (key == 0x1B) {
        // Cancel
        fm->input_mode = 0;
        fm->input_buf[0] = '\0';
        snprintf(fm->status, sizeof(fm->status), "Cancelled");
        return;
    }

    if (key == '\b') {
        if (fm->input_pos > 0) {
            fm->input_pos--;
            fm->input_buf[fm->input_pos] = '\0';
        }
        return;
    }

    // Only printable characters
    if (key >= 0x20 && key <= 0x7E && fm->input_pos < 63) {
        fm->input_buf[fm->input_pos++] = (char)key;
        fm->input_buf[fm->input_pos] = '\0';
    }
}
