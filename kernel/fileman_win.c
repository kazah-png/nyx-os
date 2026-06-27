#include "kernel.h"
#include "compositor.h"
#include "fileman_win.h"
#include "font.h"

#define TOOLBAR_H 28
#define BTN_SPACE 6
#define HEADER_H  18
#define SCROLL_W  12
#define CTX_W     160
#define CTX_ITEM_H 20

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

void fileman_new_file(fileman_win_t* fm); // forward

static void ctx_rename(fileman_win_t* fm) {
    if (fm->sel_index < 0 || fm->sel_index >= fm->entry_count) return;
    strncpy(fm->input_buf, fm->entries[fm->sel_index], sizeof(fm->input_buf) - 1);
    fm->input_pos = strlen(fm->input_buf);
    fm->input_mode = 3;
    fm->input_cursor_tick = get_ticks();
    snprintf(fm->status, sizeof(fm->status), "Rename:");
}

static void ctx_copy(fileman_win_t* fm) {
    if (fm->sel_index < 0 || fm->sel_index >= fm->entry_count) return;
    fileman_get_path(fm, fm->entries[fm->sel_index], fm->clipboard_path, sizeof(fm->clipboard_path));
    fm->clipboard_mode = 1;
    snprintf(fm->status, sizeof(fm->status), "Copied: %s", fm->entries[fm->sel_index]);
}

static void ctx_cut(fileman_win_t* fm) {
    if (fm->sel_index < 0 || fm->sel_index >= fm->entry_count) return;
    fileman_get_path(fm, fm->entries[fm->sel_index], fm->clipboard_path, sizeof(fm->clipboard_path));
    fm->clipboard_mode = 2;
    snprintf(fm->status, sizeof(fm->status), "Cut: %s", fm->entries[fm->sel_index]);
}

static void ctx_paste(fileman_win_t* fm) {
    if (fm->clipboard_mode == 0 || !fm->clipboard_path[0]) {
        snprintf(fm->status, sizeof(fm->status), "Clipboard is empty");
        return;
    }
    const char* basename = fm->clipboard_path;
    for (int i = 0; fm->clipboard_path[i]; i++)
        if (fm->clipboard_path[i] == '/') basename = &fm->clipboard_path[i] + 1;
    char dst[256];
    if (strcmp(fm->cwd, "/") == 0)
        snprintf(dst, sizeof(dst), "/%s", basename);
    else
        snprintf(dst, sizeof(dst), "%s/%s", fm->cwd, basename);

    if (fm->clipboard_mode == 1) {
        if (vfs_cp(fm->clipboard_path, dst) == 0)
            snprintf(fm->status, sizeof(fm->status), "Pasted: %s", basename);
        else
            snprintf(fm->status, sizeof(fm->status), "Paste failed");
    } else if (fm->clipboard_mode == 2) {
        vfs_rename(fm->clipboard_path, dst);
        snprintf(fm->status, sizeof(fm->status), "Moved: %s", basename);
        fm->clipboard_mode = 0;
        fm->clipboard_path[0] = '\0';
    }
    fileman_refresh(fm);
}

static const char* ctx_items[] = {"Rename", "Copy", "Cut", "Paste", "---", "Delete", "New File", "New Folder"};
#define CTX_ITEMS_COUNT (sizeof(ctx_items) / sizeof(ctx_items[0]))

static void ctx_do_action(fileman_win_t* fm, int idx) {
    fm->ctx_open = 0;
    switch (idx) {
        case 0: ctx_rename(fm); break;
        case 1: ctx_copy(fm); break;
        case 2: ctx_cut(fm); break;
        case 3: ctx_paste(fm); break;
        case 5: fileman_delete(fm); break;
        case 6: fileman_new_file(fm); break;
        case 7: fileman_new_folder(fm); break;
    }
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
    int list_w = (int)cw - SCROLL_W;

    for (int i = fm->scroll_offset; i < fm->entry_count && (i - fm->scroll_offset) < max_rows; i++) {
        int row = i - fm->scroll_offset;
        int ey = list_y + row * char_h;
        uint32_t bg = (i == fm->sel_index) ? fb_rgb(60,80,120) : fb_rgb(30,30,35);
        fb_fill_rect(cx + 2, ey, (uint32_t)(list_w - 4), char_h, bg);

        char prefix = fm->entry_types[i] ? '/' : ' ';
        char display[68];
        snprintf(display, sizeof(display), "%c %s", prefix, fm->entries[i]);
        uint32_t fg = fm->entry_types[i] ? fb_rgb(100,200,255) : fb_rgb(220,220,220);
        font_draw_string(cx + 4, ey, display, fg, bg);
    }

    // Scrollbar
    int sb_x = cx + list_w;
    int sb_y = list_y;
    int sb_h = avail_h;
    fb_fill_rect(sb_x, sb_y, SCROLL_W, (uint32_t)sb_h, fb_rgb(40,40,45));

    if (fm->entry_count > max_rows) {
        int thumb_top = (fm->scroll_offset * sb_h) / fm->entry_count;
        int thumb_h = (max_rows * sb_h) / fm->entry_count;
        if (thumb_h < 8) thumb_h = 8;
        if (thumb_top + thumb_h > sb_h) thumb_top = sb_h - thumb_h;
        fb_fill_rect(sb_x, sb_y + thumb_top, SCROLL_W, (uint32_t)thumb_h, fb_rgb(100,100,110));
        fb_fill_rect(sb_x, sb_y + thumb_top, SCROLL_W, 1, fb_rgb(140,140,150));
        fb_fill_rect(sb_x, sb_y + thumb_top + thumb_h - 1, SCROLL_W, 1, fb_rgb(70,70,80));
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

    // Context menu
    if (fm->ctx_open) {
        int cmx = fm->ctx_x, cmy = fm->ctx_y;
        int cmw = CTX_W, cmh = CTX_ITEM_H * CTX_ITEMS_COUNT + 4;
        fb_fill_rect(cmx, cmy, cmw, cmh, fb_rgb(50,50,55));
        fb_fill_rect(cmx, cmy, cmw, 1, fb_rgb(100,100,110));
        fb_fill_rect(cmx, cmy + cmh - 1, cmw, 1, fb_rgb(100,100,110));
        fb_fill_rect(cmx, cmy, 1, cmh, fb_rgb(100,100,110));
        fb_fill_rect(cmx + cmw - 1, cmy, 1, cmh, fb_rgb(100,100,110));
        for (uint32_t i = 0; i < CTX_ITEMS_COUNT; i++) {
            int iy = cmy + 2 + i * CTX_ITEM_H;
            uint32_t bg = ((int)i == fm->ctx_hover) ? fb_rgb(70,90,130) : fb_rgb(50,50,55);
            if (ctx_items[i][0] == '-' && ctx_items[i][1] == '-') {
                bg = fb_rgb(50,50,55);
                fb_fill_rect(cmx + 4, iy + CTX_ITEM_H / 2, cmw - 8, 1, fb_rgb(80,80,90));
                continue;
            }
            fb_fill_rect(cmx + 2, iy, cmw - 4, CTX_ITEM_H, bg);
            // Disable paste if clipboard empty
            if (i == 3 && (fm->clipboard_mode == 0 || !fm->clipboard_path[0]))
                font_draw_string(cmx + 10, iy + 2, ctx_items[i], fb_rgb(100,100,100), bg);
            else
                font_draw_string(cmx + 10, iy + 2, ctx_items[i], fb_rgb(220,220,220), bg);
        }
    }
}

void fileman_win_click(window_t* win, int mx, int my, int btn) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;
    int cx = win->x, cy = win->y + TITLE_H;
    uint32_t cw = win->w, ch = win->h;
    uint32_t char_h = FONT_HEIGHT;

    if (fm->entry_count == 0) fileman_refresh(fm);

    // Context menu handling (left-click closes menu or executes action)
    if (btn == 1 && fm->ctx_open) {
        int cmx = fm->ctx_x, cmy = fm->ctx_y;
        int cmw = CTX_W, cmh = CTX_ITEM_H * CTX_ITEMS_COUNT + 4;
        if (mx >= cmx && mx < cmx + cmw && my >= cmy && my < cmy + cmh) {
            int idx = (my - cmy - 2) / CTX_ITEM_H;
            if (idx >= 0 && idx < (int)CTX_ITEMS_COUNT && ctx_items[idx][0] != '-') {
                ctx_do_action(fm, idx);
            }
        }
        fm->ctx_open = 0;
        return;
    }

    // Right-click: show context menu
    if (btn == 2) {
        fm->ctx_hover = -1;

        // Check if click is in file list area
        int list_header_y = cy + TOOLBAR_H + HEADER_H;
        int list_y = list_header_y + char_h + 4;
        int avail_h = (int)(ch - TOOLBAR_H - HEADER_H - char_h - 4 - HEADER_H - 4);
        int max_rows = avail_h / (int)char_h;
        if (max_rows < 1) max_rows = 1;

        if (my >= list_y && my < list_y + max_rows * (int)char_h) {
            int idx = (my - list_y) / (int)char_h + fm->scroll_offset;
            if (idx >= 0 && idx < fm->entry_count) {
                fm->sel_index = idx;
            }
        }

        fm->ctx_open = 1;
        fm->ctx_x = mx;
        fm->ctx_y = my;
        // Keep menu within window bounds
        int cmw = CTX_W, cmh = CTX_ITEM_H * CTX_ITEMS_COUNT + 4;
        if (fm->ctx_x + cmw > cx + (int)cw) fm->ctx_x = cx + (int)cw - cmw;
        if (fm->ctx_y + cmh > cy + (int)ch) fm->ctx_y = cy + (int)ch - cmh;
        return;
    }

    // Left-click: normal handling
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
    int list_w = (int)cw - SCROLL_W;

    // Scrollbar click
    int sb_x = cx + list_w;
    if (mx >= sb_x && mx < sb_x + SCROLL_W && my >= list_y && my < list_y + avail_h) {
        if (fm->entry_count > max_rows) {
            int rel_y = my - list_y;
            fm->scroll_offset = (rel_y * fm->entry_count) / avail_h;
            if (fm->scroll_offset > fm->entry_count - max_rows)
                fm->scroll_offset = fm->entry_count - max_rows;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
        }
        return;
    }

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

void fileman_win_mousemove(window_t* win, int mx, int my) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm || !fm->ctx_open) return;
    int cmx = fm->ctx_x, cmy = fm->ctx_y;
    int cmw = CTX_W, cmh = CTX_ITEM_H * CTX_ITEMS_COUNT + 4;
    if (mx >= cmx && mx < cmx + cmw && my >= cmy && my < cmy + cmh) {
        int idx = (my - cmy - 2) / CTX_ITEM_H;
        if (idx >= 0 && idx < (int)CTX_ITEMS_COUNT)
            fm->ctx_hover = idx;
        else
            fm->ctx_hover = -1;
    } else {
        fm->ctx_hover = -1;
    }
}

void fileman_win_key(window_t* win, int key) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;

    // Navigation keys when not in input mode
    if (!fm->input_mode) {
        if (fm->entry_count == 0) return;

        // Calculate visible rows (same as in draw)
        int char_h = FONT_HEIGHT;
        uint32_t ch = win->h;
        int avail_h = (int)(ch - TOOLBAR_H - HEADER_H - char_h - 4 - HEADER_H - 4);
        int max_rows = avail_h / char_h;
        if (max_rows < 1) max_rows = 1;

        if (key == KEY_DOWN) {
            if (fm->sel_index < 0) fm->sel_index = 0;
            else if (fm->sel_index < fm->entry_count - 1) fm->sel_index++;
            if (fm->sel_index >= fm->scroll_offset + max_rows)
                fm->scroll_offset = fm->sel_index - max_rows + 1;
            return;
        }
        if (key == KEY_UP) {
            if (fm->sel_index < 0) fm->sel_index = fm->entry_count - 1;
            else if (fm->sel_index > 0) fm->sel_index--;
            if (fm->sel_index < fm->scroll_offset)
                fm->scroll_offset = fm->sel_index;
            return;
        }
        if (key == KEY_PGDN) {
            fm->scroll_offset += max_rows;
            if (fm->scroll_offset >= fm->entry_count)
                fm->scroll_offset = fm->entry_count - 1;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
            fm->sel_index = fm->scroll_offset;
            return;
        }
        if (key == KEY_PGUP) {
            fm->scroll_offset -= max_rows;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
            fm->sel_index = fm->scroll_offset;
            return;
        }
        if (key == KEY_HOME) {
            fm->scroll_offset = 0;
            fm->sel_index = 0;
            return;
        }
        if (key == KEY_END) {
            fm->sel_index = fm->entry_count - 1;
            fm->scroll_offset = fm->sel_index - max_rows + 1;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
            return;
        }
        if (key == '\n' && fm->sel_index >= 0) {
            if (fm->entry_types[fm->sel_index]) {
                fileman_cd(fm, fm->entries[fm->sel_index], win);
            } else {
                char path[256];
                fileman_get_path(fm, fm->entries[fm->sel_index], path, sizeof(path));
                int fd = vfs_open(path, 0, 0);
                if (fd >= 0) {
                    char buf[512];
                    int n = vfs_read(fd, buf, sizeof(buf)-1);
                    vfs_close(fd);
                    if (n > 0) {
                        buf[n] = '\0';
                        snprintf(fm->status, sizeof(fm->status), "%s (%d bytes): %.200s", fm->entries[fm->sel_index], n, buf);
                    } else {
                        snprintf(fm->status, sizeof(fm->status), "%s (empty)", fm->entries[fm->sel_index]);
                    }
                }
            }
            return;
        }
        return;
    }

    if (key == '\n') {
        // Commit input
        if (fm->input_pos > 0) {
            char path[256];
            fileman_get_path(fm, fm->input_buf, path, sizeof(path));
            if (fm->input_mode == 3) {
                // Rename
                char old_path[256];
                fileman_get_path(fm, fm->entries[fm->sel_index], old_path, sizeof(old_path));
                vfs_rename(old_path, path);
                snprintf(fm->status, sizeof(fm->status), "Renamed to: %s", fm->input_buf);
                fileman_refresh(fm);
            } else if (fm->input_mode == 2) {
                // Create directory
                if (vfs_mkdir(path, 0) == 0) {
                    snprintf(fm->status, sizeof(fm->status), "Created folder: %s", fm->input_buf);
                } else {
                    snprintf(fm->status, sizeof(fm->status), "Cannot create folder: %s", fm->input_buf);
                }
                fileman_refresh(fm);
            } else {
                // Create file (empty)
                int fd = vfs_open(path, 1, 0);
                if (fd >= 0) {
                    vfs_close(fd);
                    snprintf(fm->status, sizeof(fm->status), "Created file: %s", fm->input_buf);
                } else {
                    snprintf(fm->status, sizeof(fm->status), "Cannot create file: %s", fm->input_buf);
                }
                fileman_refresh(fm);
            }
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
