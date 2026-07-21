#include "theme.h"
#include "kernel.h"
#include "compositor.h"
#include "fileman_win.h"
#include "font.h"
#include "login.h"

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
    strncpy(fm->cwd, g_login_home[0] ? g_login_home : "/", sizeof(fm->cwd));
    fm->cwd[sizeof(fm->cwd) - 1] = '\0';
    fm->sel_index = -1;
    fm->input_mode = 0;
    fm->input_cursor_tick = get_ticks();
    fm->mouse_down = 0;
    fm->drag_active = 0;
    fm->drag_file_idx = -1;
    fm->drag_mode = 0;
    fm->last_click_idx = -1;
    fm->search_active = 0;
    fm->search_pattern[0] = '\0';
    fm->search_count = 0;
    snprintf(fm->status, sizeof(fm->status), "Ready");
    return fm;
}

static int str_contains(const char* s, const char* sub) {
    if (!sub || !*sub) return 1;
    while (*s) {
        const char* a = s;
        const char* b = sub;
        while (*a && *b && (*a == *b || (*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) == (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b))) {
            a++; b++;
        }
        if (!*b) return 1;
        s++;
    }
    return 0;
}

static void fileman_apply_search(fileman_win_t* fm) {
    fm->search_count = 0;
    if (!fm->search_active || !fm->search_pattern[0]) {
        for (int i = 0; i < fm->entry_count; i++)
            fm->search_indices[fm->search_count++] = i;
        return;
    }
    for (int i = 0; i < fm->entry_count; i++) {
        if (str_contains(fm->entries[i], fm->search_pattern))
            fm->search_indices[fm->search_count++] = i;
    }
    if (fm->sel_index >= 0) {
        // Keep selection if still visible
        int found = 0;
        for (int i = 0; i < fm->search_count; i++)
            if (fm->search_indices[i] == fm->sel_index) { found = 1; break; }
        if (!found) fm->sel_index = -1;
    }
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
    fileman_apply_search(fm);
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
        // Copy — if the name is already taken (e.g. pasting into the same directory)
        // make a unique "<name>_copy" so paste always produces a new file.
        int exists = vfs_open(dst, 0, 0);
        if (exists >= 0) {
            vfs_close(exists);
            if (strcmp(fm->cwd, "/") == 0)
                snprintf(dst, sizeof(dst), "/%s_copy", basename);
            else
                snprintf(dst, sizeof(dst), "%s/%s_copy", fm->cwd, basename);
        }
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
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, THEME_BUTTON);
    font_draw_string(bx + (80 - strlen("New File") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "New File", THEME_TEXT, THEME_BUTTON);
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, THEME_BUTTON);
    font_draw_string(bx + (80 - strlen("New Dir") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "New Dir", THEME_TEXT, THEME_BUTTON);
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, fb_rgb(80,50,50));
    font_draw_string(bx + (80 - strlen("Delete") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Delete", THEME_TEXT, fb_rgb(80,50,50));
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, THEME_BUTTON);
    font_draw_string(bx + (80 - strlen("Go Up") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Go Up", THEME_TEXT, THEME_BUTTON);
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 80, TOOLBAR_H - 4, THEME_BUTTON);
    font_draw_string(bx + (80 - strlen("Refresh") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Refresh", THEME_TEXT, THEME_BUTTON);
    bx += 84;
    fb_fill_rect(bx, tb_y + 2, 60, TOOLBAR_H - 4, fm->search_active ? fb_rgb(80,70,40) : THEME_BUTTON);
    font_draw_string(bx + (60 - strlen("Find") * FONT_WIDTH) / 2, tb_y + (TOOLBAR_H - FONT_HEIGHT) / 2, "Find", THEME_TEXT, fm->search_active ? fb_rgb(80,70,40) : THEME_BUTTON);

    // Directory path bar
    int path_y = tb_y + TOOLBAR_H;
    fb_fill_rect(cx, path_y, cw, HEADER_H, THEME_PANEL_HEADER);
    font_draw_string(cx + 4, path_y + (HEADER_H - char_h) / 2, fm->cwd, fb_rgb(200,200,255), THEME_PANEL_HEADER);

    // Search bar (visible when search_active)
    int search_bar_h = fm->search_active ? HEADER_H : 0;
    if (fm->search_active) {
        int sby = path_y + HEADER_H;
        fb_fill_rect(cx, sby, cw, (uint32_t)search_bar_h, fb_rgb(60,50,30));
        char search_disp[128];
        snprintf(search_disp, sizeof(search_disp), "Search: %s_ (%d/%d)", fm->search_pattern,
            fm->search_count, fm->entry_count);
        font_draw_string(cx + 4, sby + (HEADER_H - char_h) / 2, search_disp,
            fb_rgb(255,255,150), fb_rgb(60,50,30));
    }

    // Header row
    int list_header_y = path_y + HEADER_H + search_bar_h;
    fb_fill_rect(cx, list_header_y, cw, char_h + 2, fb_rgb(50,55,65));
    font_draw_string(cx + 4, list_header_y + 1, "Name", fb_rgb(255,255,255), fb_rgb(50,55,65));

    // File listing
    int list_y = list_header_y + char_h + 4;
    int avail_h = (int)(cy + ch - list_y - HEADER_H - 4);
    int max_rows = avail_h / (int)char_h;
    if (max_rows < 1) max_rows = 1;
    int list_w = (int)cw - SCROLL_W;

    int disp_count = fm->search_active ? fm->search_count : fm->entry_count;
    for (int i = fm->scroll_offset; i < disp_count && (i - fm->scroll_offset) < max_rows; i++) {
        int row = i - fm->scroll_offset;
        int ei = fm->search_active ? fm->search_indices[i] : i;
        int ey = list_y + row * char_h;
        uint32_t bg = (ei == fm->sel_index) ? THEME_SELECTION : fb_rgb(30,30,35);
        fb_fill_rect(cx + 2, ey, (uint32_t)(list_w - 4), char_h, bg);

        char prefix = fm->entry_types[ei] ? '/' : ' ';
        char display[68];
        snprintf(display, sizeof(display), "%c %s", prefix, fm->entries[ei]);
        uint32_t fg = fm->entry_types[ei] ? fb_rgb(100,200,255) : THEME_TEXT;
        font_draw_string(cx + 4, ey, display, fg, bg);
    }

    // Scrollbar
    int sb_x = cx + list_w;
    int sb_y = list_y;
    int sb_h = avail_h;
    fb_fill_rect(sb_x, sb_y, SCROLL_W, (uint32_t)sb_h, fb_rgb(40,40,45));

    if (disp_count > max_rows) {
        int thumb_top = (fm->scroll_offset * sb_h) / disp_count;
        int thumb_h = (max_rows * sb_h) / disp_count;
        if (thumb_h < 8) thumb_h = 8;
        if (thumb_top + thumb_h > sb_h) thumb_top = sb_h - thumb_h;
        fb_fill_rect(sb_x, sb_y + thumb_top, SCROLL_W, (uint32_t)thumb_h, fb_rgb(100,100,110));
        fb_fill_rect(sb_x, sb_y + thumb_top, SCROLL_W, 1, fb_rgb(140,140,150));
        fb_fill_rect(sb_x, sb_y + thumb_top + thumb_h - 1, SCROLL_W, 1, fb_rgb(70,70,80));
    }

    // Status bar
    int status_y = cy + ch - HEADER_H;
    fb_fill_rect(cx, status_y, cw, HEADER_H, THEME_PANEL_HEADER);

    if (fm->input_mode) {
        // Show input prompt
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "%s %s_", fm->status, fm->input_buf);
        font_draw_string(cx + 4, status_y + (HEADER_H - char_h) / 2, prompt, fb_rgb(255,255,100), THEME_PANEL_HEADER);
    } else {
        font_draw_string(cx + 4, status_y + (HEADER_H - char_h) / 2, fm->status, fb_rgb(180,180,180), THEME_PANEL_HEADER);
    }

    // Drag ghost
    if (fm->drag_active && fm->drag_file_idx >= 0 && fm->drag_file_idx < disp_count) {
        int gx = fm->drag_cur_x + 8;
        int gy = fm->drag_cur_y + 8;
        int gw = 180, gh = FONT_HEIGHT + 6;
        if (gx + gw > (int)fb_get_width()) gx = (int)fb_get_width() - gw;
        if (gy + gh > (int)fb_get_height()) gy = (int)fb_get_height() - gh;
        fb_fill_rect(gx, gy, (uint32_t)gw, (uint32_t)gh, THEME_SELECTION);
        fb_fill_rect(gx, gy, (uint32_t)gw, 1, fb_rgb(140,180,255));
        fb_fill_rect(gx, gy + gh - 1, (uint32_t)gw, 1, fb_rgb(140,180,255));
        fb_fill_rect(gx, gy, 1, (uint32_t)gh, fb_rgb(140,180,255));
        fb_fill_rect(gx + gw - 1, gy, 1, (uint32_t)gh, fb_rgb(140,180,255));
        char label[72];
        int drag_ei = fm->search_active ? fm->search_indices[fm->drag_file_idx] : fm->drag_file_idx;
        snprintf(label, sizeof(label), "%c %s  [%s]",
            (drag_ei >= 0 && drag_ei < fm->entry_count && fm->entry_types[drag_ei]) ? '/' : ' ',
            drag_ei >= 0 && drag_ei < fm->entry_count ? fm->entries[drag_ei] : "?",
            fm->drag_mode == 2 ? "COPY" : "MOVE");
        font_draw_string(gx + 4, gy + 3, label, fb_rgb(255,255,255), THEME_SELECTION);
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
                font_draw_string(cmx + 10, iy + 2, ctx_items[i], THEME_BORDER, bg);
            else
                font_draw_string(cmx + 10, iy + 2, ctx_items[i], THEME_TEXT, bg);
        }
    }
}

void fileman_win_click(window_t* win, int mx, int my, int btn) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;
    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);
    uint32_t cw = win->w, ch = win->h;
    uint32_t char_h = FONT_HEIGHT;

    // During drag, ignore click events (drop handled in mousemove on button release)
    if (fm->drag_active) {
        return;
    }

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

    int search_bar_h = fm->search_active ? HEADER_H : 0;
    int disp_count = fm->search_active ? fm->search_count : fm->entry_count;

    // Right-click: show context menu
    if (btn == 2) {
        fm->ctx_hover = -1;

        // Check if click is in file list area
        int list_header_y = cy + TOOLBAR_H + HEADER_H + search_bar_h;
        int list_y = list_header_y + char_h + 4;
        int avail_h = (int)(ch - TOOLBAR_H - HEADER_H - search_bar_h - char_h - 4 - HEADER_H - 4);
        int max_rows = avail_h / (int)char_h;
        if (max_rows < 1) max_rows = 1;

        if (my >= list_y && my < list_y + max_rows * (int)char_h) {
            int idx = (my - list_y) / (int)char_h + fm->scroll_offset;
            if (idx >= 0 && idx < disp_count) {
                int real_idx = fm->search_active ? fm->search_indices[idx] : idx;
                fm->sel_index = real_idx;
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
        // Find/Search button
        if (rel_x >= 424 && rel_x < 484) {
            if (fm->search_active) {
                fm->search_active = 0;
                fm->search_pattern[0] = '\0';
                fm->scroll_offset = 0;
                fileman_apply_search(fm);
                snprintf(fm->status, sizeof(fm->status), "Search closed");
            } else {
                fm->search_active = 1;
                fm->search_pattern[0] = '\0';
                fm->scroll_offset = 0;
                fileman_apply_search(fm);
                snprintf(fm->status, sizeof(fm->status), "Type to search (Esc to close)");
            }
            return;
        }
    }

    // File list area
    int list_header_y = cy + TOOLBAR_H + HEADER_H + search_bar_h;
    int list_y = list_header_y + char_h + 4;
    int avail_h = (int)(ch - TOOLBAR_H - HEADER_H - search_bar_h - char_h - 4 - HEADER_H - 4);
    int max_rows = avail_h / (int)char_h;
    if (max_rows < 1) max_rows = 1;
    int list_w = (int)cw - SCROLL_W;

    // Scrollbar click
    int sb_x = cx + list_w;
    if (mx >= sb_x && mx < sb_x + SCROLL_W && my >= list_y && my < list_y + avail_h) {
        if (disp_count > max_rows) {
            int rel_y = my - list_y;
            fm->scroll_offset = (rel_y * disp_count) / avail_h;
            if (fm->scroll_offset > disp_count - max_rows)
                fm->scroll_offset = disp_count - max_rows;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
        }
        return;
    }

    if (my >= list_y && my < list_y + max_rows * (int)char_h) {
        int idx = (my - list_y) / (int)char_h + fm->scroll_offset;
        if (idx >= 0 && idx < disp_count) {
            int real_idx = fm->search_active ? fm->search_indices[idx] : idx;
            fm->sel_index = real_idx;
            if (fm->entry_types[real_idx]) {
                fm->last_click_idx = -1;
                fileman_cd(fm, fm->entries[real_idx], win);
            } else {
                char path[256];
                fileman_get_path(fm, fm->entries[real_idx], path, sizeof(path));
                uint32_t now = get_ticks();
                if (fm->last_click_idx == real_idx && (now - fm->last_click_tick) < 400) {
                    // Second click on the same file within the double-click window:
                    // open it in a Text Editor window.
                    if (compositor_open_editor(path))
                        snprintf(fm->status, sizeof(fm->status), "Opened in editor: %s", fm->entries[real_idx]);
                    else
                        snprintf(fm->status, sizeof(fm->status), "Cannot open editor");
                    fm->last_click_idx = -1;   // don't re-fire on a third click
                } else {
                    // First click: select + preview the first bytes in the status bar.
                    int fd = vfs_open(path, 0, 0);
                    if (fd >= 0) {
                        char buf[512];
                        int n = vfs_read(fd, buf, sizeof(buf)-1);
                        vfs_close(fd);
                        if (n > 0) {
                            buf[n] = '\0';
                            snprintf(fm->status, sizeof(fm->status), "%s (%d bytes) - double-click to edit: %.170s", fm->entries[real_idx], n, buf);
                        } else {
                            snprintf(fm->status, sizeof(fm->status), "%s (empty) - double-click to edit", fm->entries[real_idx]);
                        }
                    }
                    fm->last_click_tick = now;
                    fm->last_click_idx = real_idx;
                }
            }
        }
    }
}

// Map a y coordinate to the entry index at that list row (or -1 if outside the
// list). Uses the same geometry as draw/click, so drop-target detection matches
// what the user sees.
static int fileman_row_at(fileman_win_t* fm, window_t* win, int my) {
    int cy = WIN_CLIENT_Y(win);
    uint32_t char_h = FONT_HEIGHT;
    int search_bar_h = fm->search_active ? HEADER_H : 0;
    int list_header_y = cy + TOOLBAR_H + HEADER_H + search_bar_h;
    int list_y = list_header_y + (int)char_h + 4;
    int avail_h = (int)(win->h - TOOLBAR_H - HEADER_H - search_bar_h - (int)char_h - 4 - HEADER_H - 4);
    int max_rows = avail_h / (int)char_h;
    if (max_rows < 1) max_rows = 1;
    int disp_count = fm->search_active ? fm->search_count : fm->entry_count;
    if (my < list_y || my >= list_y + max_rows * (int)char_h) return -1;
    int idx = (my - list_y) / (int)char_h + fm->scroll_offset;
    if (idx < 0 || idx >= disp_count) return -1;
    return fm->search_active ? fm->search_indices[idx] : idx;
}

void fileman_win_mousemove(window_t* win, int mx, int my, int btns) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;

    int search_bar_h = fm->search_active ? HEADER_H : 0;
    int disp_count = fm->search_active ? fm->search_count : fm->entry_count;

    // Track button state
    if (btns & 1) {
        if (!fm->mouse_down) {
            // Left button just pressed (first mousemove with btn down)
            fm->mouse_down = 1;
            fm->drag_start_x = mx;
            fm->drag_start_y = my;
            // Determine which file was under cursor at press time
            int cy = WIN_CLIENT_Y(win);
            uint32_t char_h = FONT_HEIGHT;
            int list_header_y = cy + TOOLBAR_H + HEADER_H + search_bar_h;
            int list_y = list_header_y + char_h + 4;
            int avail_h = (int)(win->h - TOOLBAR_H - HEADER_H - search_bar_h - char_h - 4 - HEADER_H - 4);
            int max_rows = avail_h / (int)char_h;
            if (max_rows < 1) max_rows = 1;
            if (my >= list_y && my < list_y + max_rows * (int)char_h) {
                int idx = (my - list_y) / (int)char_h + fm->scroll_offset;
                if (idx >= 0 && idx < disp_count)
                    fm->drag_file_idx = fm->search_active ? fm->search_indices[idx] : idx;
                else
                    fm->drag_file_idx = -1;
            } else {
                fm->drag_file_idx = -1;
            }
        }
        // Check for drag activation (distance threshold)
        if (fm->mouse_down && !fm->drag_active && fm->drag_file_idx >= 0) {
            int dx = mx - fm->drag_start_x;
            int dy = my - fm->drag_start_y;
            if (dx * dx + dy * dy > 36) { // 6px threshold
                fm->drag_active = 1;
                fm->drag_mode = is_ctrl_pressed() ? 2 : 1; // Ctrl = copy, else move
                fm->drag_cur_x = mx;
                fm->drag_cur_y = my;
            }
        }
    } else {
        // Button released — perform the drop.
        if (fm->drag_active) {
            int src_idx = (fm->drag_file_idx >= 0 && fm->drag_file_idx < fm->entry_count)
                ? fm->drag_file_idx : -1;
            if (src_idx >= 0) {
                char src[256];
                fileman_get_path(fm, fm->entries[src_idx], src, sizeof(src));
                const char* basename = src;
                for (int i = 0; src[i]; i++)
                    if (src[i] == '/') basename = &src[i] + 1;

                // Destination directory: if dropped onto a folder row, move/copy the
                // file INTO that folder; otherwise it stays in the current directory.
                char dstdir[256];
                int tgt = fileman_row_at(fm, win, my);
                if (tgt >= 0 && tgt != src_idx && fm->entry_types[tgt]) {
                    fileman_get_path(fm, fm->entries[tgt], dstdir, sizeof(dstdir));
                } else {
                    strncpy(dstdir, fm->cwd, sizeof(dstdir));
                    dstdir[sizeof(dstdir) - 1] = '\0';
                }
                char dst[256];
                if (strcmp(dstdir, "/") == 0)
                    snprintf(dst, sizeof(dst), "/%s", basename);
                else
                    snprintf(dst, sizeof(dst), "%s/%s", dstdir, basename);

                if (strcmp(src, dst) == 0) {
                    snprintf(fm->status, sizeof(fm->status), "Dropped in place");
                } else if (fm->drag_mode == 2) {          // Ctrl-drag → copy
                    if (vfs_cp(src, dst) == 0)
                        snprintf(fm->status, sizeof(fm->status), "Copied %s -> %s", basename, dstdir);
                    else
                        snprintf(fm->status, sizeof(fm->status), "Copy failed (name exists?)");
                } else {                                   // plain drag → move
                    vfs_rename(src, dst);
                    snprintf(fm->status, sizeof(fm->status), "Moved %s -> %s", basename, dstdir);
                }
                fileman_refresh(fm);
            }
        }
        fm->mouse_down = 0;
        fm->drag_active = 0;
        fm->drag_file_idx = -1;
    }

    if (fm->drag_active) {
        fm->drag_cur_x = mx;
        fm->drag_cur_y = my;
    }

    // Context menu hover
    if (fm->ctx_open) {
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
}

void fileman_win_key(window_t* win, int key) {
    fileman_win_t* fm = (fileman_win_t*)win->reserved;
    if (!fm) return;

    // Ctrl shortcuts (when not in input mode)
    if (!fm->input_mode && is_ctrl_pressed()) {
        if (key == 'f' || key == 'F') {
            // Toggle search
            if (fm->search_active) {
                fm->search_active = 0;
                fm->search_pattern[0] = '\0';
                fm->scroll_offset = 0;
                fileman_apply_search(fm);
                snprintf(fm->status, sizeof(fm->status), "Search closed");
            } else {
                fm->search_active = 1;
                fm->input_mode = 0; // Ensure not in input mode
                fm->search_pattern[0] = '\0';
                fm->scroll_offset = 0;
                fileman_apply_search(fm);
                snprintf(fm->status, sizeof(fm->status), "Type search pattern (Esc to close)");
            }
            return;
        }
        if (key == 'c' || key == 'C') {
            if (fm->sel_index >= 0) {
                fileman_get_path(fm, fm->entries[fm->sel_index], fm->clipboard_path, sizeof(fm->clipboard_path));
                fm->clipboard_mode = 1;
                snprintf(fm->status, sizeof(fm->status), "Copied: %s", fm->entries[fm->sel_index]);
            }
            return;
        }
        if (key == 'x' || key == 'X') {
            if (fm->sel_index >= 0) {
                fileman_get_path(fm, fm->entries[fm->sel_index], fm->clipboard_path, sizeof(fm->clipboard_path));
                fm->clipboard_mode = 2;
                snprintf(fm->status, sizeof(fm->status), "Cut: %s", fm->entries[fm->sel_index]);
            }
            return;
        }
        if (key == 'v' || key == 'V') {
            ctx_paste(fm);
            return;
        }
        if (key == 'a' || key == 'A') {
            // Select all — just select first, user can navigate
            if (fm->entry_count > 0) {
                fm->sel_index = 0;
                fm->scroll_offset = 0;
                snprintf(fm->status, sizeof(fm->status), "%d entries", fm->entry_count);
            }
            return;
        }
    }

    // Search input mode
    if (fm->search_active && !fm->input_mode) {
        if (key == 0x1B) {
            fm->search_active = 0;
            fm->search_pattern[0] = '\0';
            fm->scroll_offset = 0;
            fileman_apply_search(fm);
            snprintf(fm->status, sizeof(fm->status), "Search closed");
            return;
        }
        if (key == '\b') {
            int len = strlen(fm->search_pattern);
            if (len > 0) {
                fm->search_pattern[len - 1] = '\0';
                fm->scroll_offset = 0;
                fileman_apply_search(fm);
            }
            return;
        }
        if (key >= 0x20 && key <= 0x7E) {
            int len = strlen(fm->search_pattern);
            if (len < 62) {
                fm->search_pattern[len] = (char)key;
                fm->search_pattern[len + 1] = '\0';
                fm->scroll_offset = 0;
                fileman_apply_search(fm);
            }
            return;
        }
    }

    // Navigation keys when not in input mode
    if (!fm->input_mode) {
        int disp_count = fm->search_active ? fm->search_count : fm->entry_count;
        if (disp_count == 0) return;

        // Calculate visible rows (same as in draw)
        int char_h = FONT_HEIGHT;
        uint32_t ch = win->h;
        int search_bar_h = fm->search_active ? HEADER_H : 0;
        int avail_h = (int)(ch - TOOLBAR_H - HEADER_H - search_bar_h - char_h - 4 - HEADER_H - 4);
        int max_rows = avail_h / char_h;
        if (max_rows < 1) max_rows = 1;

        if (key == KEY_DOWN) {
            if (fm->sel_index < 0) fm->sel_index = 0;
            else if (fm->sel_index < disp_count - 1) {
                int real_next = fm->search_active ? fm->search_indices[fm->sel_index + 1] : fm->sel_index + 1;
                fm->sel_index = real_next;
            }
            if (fm->sel_index >= fm->scroll_offset + max_rows)
                fm->scroll_offset = fm->sel_index - max_rows + 1;
            return;
        }
        if (key == KEY_UP) {
            if (fm->sel_index < 0) fm->sel_index = 0;
            else if (fm->sel_index > 0) {
                int real_prev = fm->search_active ? fm->search_indices[fm->sel_index - 1] : fm->sel_index - 1;
                fm->sel_index = real_prev;
            }
            if (fm->sel_index < fm->scroll_offset)
                fm->scroll_offset = fm->sel_index;
            return;
        }
        if (key == KEY_PGDN) {
            fm->scroll_offset += max_rows;
            if (fm->scroll_offset >= disp_count)
                fm->scroll_offset = disp_count - 1;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
            fm->sel_index = fm->search_active ? fm->search_indices[fm->scroll_offset] : fm->scroll_offset;
            return;
        }
        if (key == KEY_PGUP) {
            fm->scroll_offset -= max_rows;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
            fm->sel_index = fm->search_active ? fm->search_indices[fm->scroll_offset] : fm->scroll_offset;
            return;
        }
        if (key == KEY_HOME) {
            fm->scroll_offset = 0;
            fm->sel_index = fm->search_active ? fm->search_indices[0] : 0;
            return;
        }
        if (key == KEY_END) {
            fm->sel_index = fm->search_active ? fm->search_indices[disp_count - 1] : disp_count - 1;
            fm->scroll_offset = fm->sel_index - max_rows + 1;
            if (fm->scroll_offset < 0) fm->scroll_offset = 0;
            return;
        }
        if (key == '\n' && fm->sel_index >= 0) {
            if (fm->entry_types[fm->sel_index]) {
                fileman_cd(fm, fm->entries[fm->sel_index], win);
            } else {
                // Enter on a file opens it in the Text Editor (like double-click).
                char path[256];
                fileman_get_path(fm, fm->entries[fm->sel_index], path, sizeof(path));
                if (compositor_open_editor(path))
                    snprintf(fm->status, sizeof(fm->status), "Opened in editor: %s", fm->entries[fm->sel_index]);
                else
                    snprintf(fm->status, sizeof(fm->status), "Cannot open editor");
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
                // Rename. The selection was validated when the rename STARTED,
                // but it can be cleared while the user is still typing the new
                // name — fileman_refresh() drops it when a search filter no
                // longer matches, and deleting resets it to -1. Committing then
                // indexed entries[-1], reading 64 bytes from before the array
                // and handing them to vfs_rename as a path. Re-check here, the
                // same way every other consumer of sel_index already does.
                if (fm->sel_index < 0 || fm->sel_index >= fm->entry_count) {
                    snprintf(fm->status, sizeof(fm->status), "Rename cancelled: selection lost");
                    fm->input_mode = 0;
                    fm->input_pos = 0;
                    fm->input_buf[0] = '\0';
                    return;
                }
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
