#include "kernel.h"
#include "compositor.h"
#include "font.h"
#include "terminal_win.h"
#include "fileman_win.h"

static window_t* windows[MAX_WINDOWS];
static int window_count = 0;
static int next_id = 100;
static int focused_id = 0;
static int drag_id = 0;
static int resize_id = 0;
static int quit = 0;
static int compositor_active = 0;
static int current_workspace = 0;

static uint32_t taskbar_bg, taskbar_fg, taskbar_hl;
static uint32_t desktop_bg, title_active, title_inactive;

static int start_menu_open = 0;
static int mouse_x = 0, mouse_y = 0;
static uint8_t mouse_btns = 0;

#define CURSOR_W 12
#define CURSOR_H 16
static const uint8_t cursor_data[CURSOR_H][CURSOR_W] = {
    {1,1,0,0,0,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},{1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},{1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},{1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},{1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,2,2,2,2,1},{1,2,2,2,2,2,1,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},{1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,1,2,2,1,0,0,0},{0,0,0,0,0,0,1,2,2,1,0,0},
};
static uint32_t cursor_bg_buf[CURSOR_H * CURSOR_W];
static int cursor_saved = 0;

static inline uint32_t win_client_x(window_t* w) { return w->x; }
static inline uint32_t win_client_y(window_t* w) { return w->y + TITLE_H; }
static inline uint32_t win_total_h(window_t* w) { return w->h + TITLE_H; }

static void compositor_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (x >= fw || y >= fh) return;
    ((uint32_t*)fb_get_addr())[y * fw + x] = color;
}

static void save_cursor_bg(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++)
            cursor_bg_buf[y * CURSOR_W + x] = ((uint32_t*)fb_get_addr())[(my + y) * fw + (mx + x)];
    cursor_saved = 1;
}

static void restore_cursor_bg(int mx, int my) {
    if (!cursor_saved) return;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++)
            ((uint32_t*)fb_get_addr())[(my + y) * fw + (mx + x)] = cursor_bg_buf[y * CURSOR_W + x];
}

static void draw_cursor(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    uint32_t* fb = (uint32_t*)fb_get_addr();
    for (int y = 0; y < CURSOR_H && (uint32_t)(my + y) < fh; y++)
        for (int x = 0; x < CURSOR_W && (uint32_t)(mx + x) < fw; x++) {
            if (cursor_data[y][x] == 1)
                fb[(my + y) * fw + (mx + x)] = fb_rgb(0,0,0);
            else if (cursor_data[y][x] == 2)
                fb[(my + y) * fw + (mx + x)] = fb_rgb(255,255,255);
        }
}

static void draw_button(int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* label) {
    fb_fill_rect(x, y, w, h, bg);
    if (label) {
        uint32_t tw = strlen(label) * FONT_WIDTH;
        font_draw_string(x + (w - tw) / 2, y + (h - FONT_HEIGHT) / 2, label, fg, bg);
    }
}

static void draw_x_button(int x, int y, int size, uint32_t color) {
    fb_fill_rect(x, y, size, size, fb_rgb(200,40,40));
    int pad = 4;
    for (int i = 0; i < size - pad * 2; i++) {
        compositor_draw_pixel(x + pad + i, y + pad + i, color);
        compositor_draw_pixel(x + pad + i + 1, y + pad + i, color);
        compositor_draw_pixel(x + size - pad - 1 - i, y + pad + i, color);
        compositor_draw_pixel(x + size - pad - 1 - i - 1, y + pad + i, color);
    }
}

static void draw_min_button(int x, int y, int size, uint32_t color) {
    fb_fill_rect(x, y, size, size, fb_rgb(60,60,60));
    int cy = y + size / 2;
    for (int i = 0; i < size - 6; i++)
        compositor_draw_pixel(x + 3 + i, cy, color);
}

static void draw_max_button(int x, int y, int size, uint32_t color) {
    fb_fill_rect(x, y, size, size, fb_rgb(60,120,60));
    for (int i = 1; i < size - 3; i++) {
        compositor_draw_pixel(x + 3 + i, y + 3, color);
        compositor_draw_pixel(x + 3 + i, y + size - 4, color);
        compositor_draw_pixel(x + 3, y + 3 + i, color);
        compositor_draw_pixel(x + size - 4, y + 3 + i, color);
    }
}

static void draw_titlebar(window_t* win) {
    uint32_t bg = win->focused ? title_active : title_inactive;
    fb_fill_rect(win->x, win->y, win->w, TITLE_H, bg);

    int y_off = win->y + (TITLE_H - FONT_HEIGHT) / 2;
    font_draw_string(win->x + 4, y_off, win->title, fb_rgb(255,255,255), bg);

    int bx = win->x + win->w - CLOSE_W - 2;
    if (win->has_close) {
        draw_x_button(bx, win->y + 3, CLOSE_W, fb_rgb(255,255,255));
        bx -= CLOSE_W + 2;
    }
    if (win->has_max) {
        draw_max_button(bx, win->y + 3, CLOSE_W, fb_rgb(255,255,255));
        bx -= CLOSE_W + 2;
    }
    if (win->has_min) {
        draw_min_button(bx, win->y + 3, CLOSE_W, fb_rgb(255,255,255));
    }
}

static void draw_window_frame(window_t* win) {
    fb_fill_rect(win->x - 1, win->y - 1, win->w + 2, 1, fb_rgb(180,180,180));
    fb_fill_rect(win->x - 1, win->y + TITLE_H + win->h, win->w + 2, 1, fb_rgb(80,80,80));
    fb_fill_rect(win->x - 1, win->y, 1, win->h + TITLE_H, fb_rgb(180,180,180));
    fb_fill_rect(win->x + win->w, win->y, 1, win->h + TITLE_H, fb_rgb(80,80,80));
}

static int titlebar_hit(window_t* win, int mx, int my) {
    return mx >= win->x && mx < win->x + (int)win->w
        && my >= win->y && my < win->y + TITLE_H;
}

static int close_hit(window_t* win, int mx, int my) {
    if (!win->has_close) return 0;
    int bx = win->x + win->w - CLOSE_W - 2;
    return mx >= bx && mx < bx + CLOSE_W
        && my >= win->y + 3 && my < win->y + 3 + CLOSE_W;
}

static int max_hit(window_t* win, int mx, int my) {
    if (!win->has_max) return 0;
    int bx = win->x + win->w - CLOSE_W - 2 - (win->has_close ? CLOSE_W + 2 : 0);
    return mx >= bx && mx < bx + CLOSE_W
        && my >= win->y + 3 && my < win->y + 3 + CLOSE_W;
}

static int min_hit(window_t* win, int mx, int my) {
    if (!win->has_min) return 0;
    int bx = win->x + win->w - CLOSE_W - 2 - (win->has_close ? CLOSE_W + 2 : 0) - (win->has_max ? CLOSE_W + 2 : 0);
    return mx >= bx && mx < bx + CLOSE_W
        && my >= win->y + 3 && my < win->y + 3 + CLOSE_W;
}

static int window_hit(window_t* win, int mx, int my) {
    return mx >= win->x && mx < win->x + (int)win->w
        && my >= win->y && my < win->y + (int)win_total_h(win);
}

static int resize_hit(window_t* win, int mx, int my, int* dir) {
    int rx = win->x + win->w, by = win->y + win_total_h(win);
    int lx = win->x, ty = win->y;
    int client_top = win->y + TITLE_H;

    int on_left   = mx >= lx - RESIZE_MARGIN && mx < lx + RESIZE_MARGIN;
    int on_right  = mx >= rx - RESIZE_MARGIN && mx < rx + RESIZE_MARGIN;
    int on_top    = my >= ty - RESIZE_MARGIN && my < ty + RESIZE_MARGIN;
    int on_bottom = my >= by - RESIZE_MARGIN && my < by + RESIZE_MARGIN;
    int in_client = my >= client_top && my < by;
    int in_vrange = my >= ty && my < by;

    if (on_top && on_left)    { *dir = RESIZE_LEFT_TOP;     return 1; }
    if (on_top && on_right)   { *dir = RESIZE_RIGHT_TOP;    return 1; }
    if (on_bottom && on_left) { *dir = RESIZE_LEFT_BOTTOM;  return 1; }
    if (on_bottom && on_right){ *dir = RESIZE_CORNER;       return 1; }
    if (on_left && in_vrange) { *dir = RESIZE_LEFT;         return 1; }
    if (on_right && in_client){ *dir = RESIZE_RIGHT;        return 1; }
    if (on_top && in_vrange)  { *dir = RESIZE_TOP;          return 1; }
    if (on_bottom && in_vrange){ *dir = RESIZE_BOTTOM;      return 1; }
    return 0;
}

static int start_hit(int mx, int my) {
    return mx >= 2 && mx < 2 + 80 && my >= (int)(fb_get_height() - TASKBAR_H) && my < (int)fb_get_height();
}

__attribute__((unused)) static int taskbar_clock_hit(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    return mx >= (int)(fw - CLOCK_W - 4) && mx < (int)(fw - 2)
        && my >= (int)(fh - TASKBAR_H) && my < (int)fh;
}

static int taskbar_win_hit(int mx, int my, int* id) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int bx = 90;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i] || !windows[i]->visible) continue;
        if (windows[i]->workspace != current_workspace) continue;
        int bw = 150;
        if (bx + bw > (int)(fw - CLOCK_W - 8)) bw = (int)(fw - CLOCK_W - 8) - bx;
        if (bw < 40) break;
        if (mx >= bx && mx < bx + bw && my >= (int)(fh - TASKBAR_H + 4) && my < (int)fh - 4) {
            *id = windows[i]->id;
            return 1;
        }
        bx += bw + 2;
    }
    return 0;
}

static int start_menu_hit(int mx, int my) {
    if (!start_menu_open) return 0;
    uint32_t fh = fb_get_height();
    int sm_x = 2;
    int sm_y = (int)fh - TASKBAR_H - START_H;
    return mx >= sm_x && mx < sm_x + START_W && my >= sm_y && my < sm_y + START_H;
}

static int start_menu_item_hit(int mx, int my, int* idx) {
    if (!start_menu_open) return 0;
    uint32_t fh = fb_get_height();
    int sm_x = 2;
    int sm_y = (int)fh - TASKBAR_H - START_H;
    if (mx < sm_x || mx >= sm_x + START_W || my < sm_y || my >= sm_y + START_H) return 0;
    *idx = (my - sm_y - 8) / 28;
    if (*idx < 0 || *idx >= 12) return 0;
    return 1;
}

static void draw_taskbar(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int tb_y = fh - TASKBAR_H;

    fb_fill_rect(0, tb_y, fw, TASKBAR_H, taskbar_bg);
    fb_fill_rect(0, tb_y, fw, 1, fb_rgb(100,100,100));

    draw_button(2, tb_y + 4, 80, TASKBAR_H - 8, start_menu_open ? taskbar_hl : taskbar_bg, fb_rgb(255,255,255), "Menu");

    int bx = 90;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i] || !windows[i]->visible) continue;
        if (windows[i]->workspace != current_workspace && windows[i]->state != WSTATE_MINIMIZED) continue;
        int bw = 150;
        if (bx + bw > (int)(fw - CLOCK_W - 8)) bw = (int)(fw - CLOCK_W - 8) - bx;
        if (bw < 40) break;
        uint32_t bbg = windows[i]->focused ? taskbar_hl : taskbar_bg;
        fb_fill_rect(bx, tb_y + 4, bw, TASKBAR_H - 8, bbg);
        if (windows[i]->state == WSTATE_MINIMIZED) {
            fb_fill_rect(bx, tb_y + 4, bw, TASKBAR_H - 8, fb_rgb(50,50,55));
        }
        if (windows[i]->title[0]) {
            int tw = strlen(windows[i]->title) * FONT_WIDTH;
            if (tw > bw - 8) tw = bw - 8;
            font_draw_string(bx + 4, tb_y + (TASKBAR_H - FONT_HEIGHT) / 2, windows[i]->title, fb_rgb(220,220,220), bbg);
        }
        bx += bw + 2;
    }

    uint32_t now = get_ticks();
    uint32_t min = (now / 60000) % 60;
    uint32_t hour = (now / 3600000) % 24;
    char timebuf[16];
    snprintf(timebuf, sizeof(timebuf), "%02u:%02u", hour, min);
    fb_fill_rect(fw - CLOCK_W - 4, tb_y + 4, CLOCK_W, TASKBAR_H - 8, fb_rgb(30,30,35));
    font_draw_string(fw - CLOCK_W - 2 + (CLOCK_W - strlen(timebuf) * FONT_WIDTH) / 2,
                     tb_y + (TASKBAR_H - FONT_HEIGHT) / 2, timebuf, fb_rgb(180,180,200), fb_rgb(30,30,35));
}

static void draw_start_menu(void) {
    if (!start_menu_open) return;
    uint32_t fh = fb_get_height();
    int sm_x = 2, sm_y = fh - TASKBAR_H - START_H;

    fb_fill_rect(sm_x, sm_y, START_W, START_H, fb_rgb(45,45,50));
    fb_fill_rect(sm_x, sm_y, START_W, 1, fb_rgb(100,100,100));
    fb_fill_rect(sm_x, sm_y + START_H - 1, START_W, 1, fb_rgb(100,100,100));
    fb_fill_rect(sm_x, sm_y, 1, START_H, fb_rgb(100,100,100));
    fb_fill_rect(sm_x + START_W - 1, sm_y, 1, START_H, fb_rgb(100,100,100));

    fb_fill_rect(sm_x, sm_y, START_W, 24, fb_rgb(60,120,60));
    font_draw_string(sm_x + 8, sm_y + 4, "NyxOS Menu", fb_rgb(255,255,255), fb_rgb(60,120,60));

    const char* items[] = {
        "File Manager", "Text Editor", "Image Viewer", "Terminal",
        "Settings", "Package Manager", "DOOM", "Desktop Demo",
        "Paint Demo", "Sound Test", "About", "Shutdown",
    };
    for (int i = 0; i < 12; i++) {
        int iy = sm_y + 28 + i * 28;
        if ((uint32_t)(iy + 28) > fh - TASKBAR_H) break;
        fb_fill_rect(sm_x + 4, iy, START_W - 8, 26, fb_rgb(45,45,50));
        font_draw_string(sm_x + 12, iy + 5, items[i], fb_rgb(220,220,220), fb_rgb(45,45,50));
        fb_fill_rect(sm_x + 4, iy + 27, START_W - 8, 1, fb_rgb(55,55,60));
    }
}

static void draw_workspace_indicator(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int y = fh - TASKBAR_H - 10;
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        uint32_t c = (i == current_workspace) ? fb_rgb(100,200,100) : fb_rgb(80,80,80);
        fb_fill_rect(fw - CLOCK_W - 4 + i * 18, y, 14, 6, c);
    }
}

static void draw_background(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    uint32_t colors[4] = {
        fb_rgb(30,35,50), fb_rgb(35,40,55), fb_rgb(35,40,55), fb_rgb(30,35,50),
    };
    uint32_t band_h = fh / 6;
    for (uint32_t y = 0; y < fh; y++) {
        uint32_t c = colors[(y / band_h) % 4];
        for (uint32_t x = 0; x < fw; x++) {
            int32_t dx = (int32_t)x - (int32_t)fw / 2;
            int32_t dy = (int32_t)y - (int32_t)fh / 2;
            int32_t dist = (int32_t)((dx * dx + dy * dy) / 4000);
            uint32_t r = (c >> 16) & 0xFF;
            uint32_t g = (c >> 8) & 0xFF;
            uint32_t b = c & 0xFF;
            uint32_t udist = (uint32_t)dist;
            r = r > udist ? r - udist : 0;
            g = g > udist ? g + udist/2 : 0;
            b = b > udist ? b : 0;
            if (r > 60) r = 60;
            if (g > 80) g = 80;
            if (b > 100) b = 100;
            compositor_draw_pixel(x, y, fb_rgb(r, g, b));
        }
    }
}

static void init_desktop_icons(void);
static void draw_desktop_icons(void);
static void settings_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);

static void redraw_all(void) {
    draw_background();
    draw_desktop_icons();

    window_t* sorted[MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->visible && windows[i]->state != WSTATE_MINIMIZED
            && windows[i]->workspace == current_workspace)
            sorted[n++] = windows[i];

    for (int i = 1; i < n; i++) {
        window_t* key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j]->z_order > key->z_order) { sorted[j+1] = sorted[j]; j--; }
        sorted[j+1] = key;
    }

    for (int i = 0; i < n; i++) {
        window_t* win = sorted[i];
        fb_fill_rect(win->x, win->y + TITLE_H, win->w, win->h, fb_rgb(35,35,40));
        draw_window_frame(win);
        draw_titlebar(win);
        if (win->draw)
            win->draw(win, win->x, win->y + TITLE_H, win->w, win->h);
    }

    draw_workspace_indicator();
    draw_taskbar();
    draw_start_menu();
}

static window_t* find_window(int id) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->id == id) return windows[i];
    return NULL;
}

static void do_start_menu_action(int idx) {
    start_menu_open = 0;
    redraw_all();
    switch (idx) {
        case 0: // File Manager
            {
                window_t* fwin = window_create(100, 100, 550, 380, "File Manager", fileman_win_draw);
                if (fwin) {
                    fwin->reserved = fileman_create_ctx();
                    if (fwin->reserved) {
                        fwin->on_click = fileman_win_click;
                    }
                }
            }
            break;
        case 1: // Text Editor (placeholder)
            window_create(150, 120, 600, 400, "Text Editor", NULL);
            break;
        case 2: // Image Viewer
            window_create(200, 140, 400, 300, "Image Viewer", NULL);
            break;
        case 3: // Terminal
            {
                window_t* twin = window_create(80, 80, 640, 400, "Terminal", terminal_win_draw);
                if (twin) {
                    twin->reserved = terminal_create_ctx();
                    twin->on_key = terminal_win_key;
                }
            }
            break;
        case 4: // Settings
            window_create(160, 100, 500, 400, "Settings", settings_draw_fn);
            break;
        case 5: // Package Manager
            window_create(180, 120, 480, 360, "Package Manager", NULL);
            break;
        case 6: // DOOM
            window_create(0, 0, 320, 200, "DOOM", NULL);
            break;
        case 7: // Desktop Demo
            window_create(100, 100, 300, 200, "Desktop Demo", NULL);
            break;
        case 8: // Paint Demo
            window_create(150, 100, 400, 300, "Paint Demo", NULL);
            break;
        case 9: // Sound Test
            window_create(200, 150, 350, 200, "Sound Test", NULL);
            break;
        case 10: // About
            window_create(fb_get_width()/2-150, fb_get_height()/2-100, 300, 200, "About NyxOS", NULL);
            break;
        case 11: // Shutdown
            quit = 1;
            break;
    }
    redraw_all();
}

void compositor_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i] = NULL;
    window_count = 0; next_id = 100; focused_id = 0; drag_id = 0; resize_id = 0; quit = 0;
    current_workspace = 0; start_menu_open = 0; cursor_saved = 0;

    taskbar_bg = fb_rgb(25,25,30);
    taskbar_fg = fb_rgb(220,220,220);
    taskbar_hl = fb_rgb(50,55,65);
    desktop_bg = fb_rgb(30,35,50);
    title_active = fb_rgb(40,90,140);
    title_inactive = fb_rgb(65,65,70);
    init_desktop_icons();
}

window_t* window_create(int x, int y, uint32_t w, uint32_t h, const char* title, window_draw_fn draw) {
    if (window_count >= MAX_WINDOWS) return NULL;
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (!windows[i]) { slot = i; break; }
    if (slot < 0) return NULL;

    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    win->x = x; win->y = y; win->w = w < MIN_WIN_W ? MIN_WIN_W : w;
    win->h = h < MIN_WIN_H ? MIN_WIN_H : h;
    win->normal_x = x; win->normal_y = y; win->normal_w = win->w; win->normal_h = win->h;
    win->z_order = window_count;
    win->visible = 1; win->state = WSTATE_NORMAL;
    win->dragging = 0; win->resizing = 0;
    win->focused = 0; win->id = next_id++;
    win->workspace = current_workspace;
    win->has_close = 1; win->has_min = 1; win->has_max = 1;
    win->draw = draw;
    win->on_key = NULL;
    win->on_click = NULL;
    int sl = strlen(title);
    if (sl >= MAX_TITLE) sl = MAX_TITLE - 1;
    memcpy(win->title, title, sl);
    win->title[sl] = '\0';
    windows[slot] = win;
    window_count++;
    window_focus(win->id);
    return win;
}

static void focus_next_window(void) {
    // Find the highest-z-order visible, non-minimized window
    window_t* best = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i] || !windows[i]->visible) continue;
        if (windows[i]->state == WSTATE_MINIMIZED) continue;
        if (windows[i]->workspace != current_workspace) continue;
        if (!best || windows[i]->z_order > best->z_order)
            best = windows[i];
    }
    if (best) {
        window_focus(best->id);
    } else {
        focused_id = 0;
    }
}

void window_destroy(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] == win) { windows[i] = NULL; break; }
    window_count--;
    kfree(win);
    if (focused_id == id)
        focus_next_window();
}

void window_focus(int id) {
    window_t* win = find_window(id);
    if (!win || !win->visible || win->state == WSTATE_MINIMIZED) return;
    win->focused = 1;
    win->z_order = window_count + 10;
    focused_id = id;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->id != id)
            windows[i]->focused = 0;
}

void window_move(int id, int x, int y) {
    window_t* win = find_window(id);
    if (!win) return;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    // Allow off-screen during resize (clamp is done in window_resize)
    if (!win->resizing) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + (int)win->w > (int)fw) x = (int)fw - (int)win->w;
        if (y + (int)win_total_h(win) > (int)fh - (int)TASKBAR_H)
            y = (int)fh - (int)TASKBAR_H - (int)win_total_h(win);
    }
    win->x = x; win->y = y;
}

void window_resize(int id, uint32_t w, uint32_t h) {
    window_t* win = find_window(id);
    if (!win) return;
    win->w = w < MIN_WIN_W ? MIN_WIN_W : w;
    win->h = h < MIN_WIN_H ? MIN_WIN_H : h;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (win->x + win->w > fw) win->w = fw - win->x;
    if (win->y + win_total_h(win) > fh - TASKBAR_H) win->h = fh - TASKBAR_H - win->y - TITLE_H;
}

void window_minimize(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    win->state = WSTATE_MINIMIZED;
    win->visible = 1;
    win->focused = 0;
    if (focused_id == id) focused_id = 0;
}

void window_maximize(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    if (win->state == WSTATE_MAXIMIZED) { window_restore(id); return; }
    win->normal_x = win->x; win->normal_y = win->y;
    win->normal_w = win->w; win->normal_h = win->h;
    win->x = 0; win->y = 0;
    win->w = fb_get_width();
    win->h = fb_get_height() - TASKBAR_H - TITLE_H;
    win->state = WSTATE_MAXIMIZED;
}

void window_restore(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    win->x = win->normal_x; win->y = win->normal_y;
    win->w = win->normal_w; win->h = win->normal_h;
    win->state = WSTATE_NORMAL;
}

void window_set_workspace(int id, int ws) {
    window_t* win = find_window(id);
    if (!win) return;
    win->workspace = ws;
}

int window_get_count(void) { return window_count; }

int window_get_ids(int* ids, int max) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS && n < max; i++)
        if (windows[i]) ids[n++] = windows[i]->id;
    return n;
}

static void demo_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    font_draw_string(cx + 5, cy + 5, win->title, fb_rgb(200,200,200), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 25, "Drag title bar to move", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 45, "Resize from edges", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 65, "Minimize/Maximize/Close", fb_rgb(160,160,160), fb_rgb(35,35,40));
    char buf[32];
    snprintf(buf, sizeof(buf), "ID: %d", win->id);
    font_draw_string(cx + 5, cy + 85, buf, fb_rgb(255,255,0), fb_rgb(35,35,40));
    snprintf(buf, sizeof(buf), "WS: %d", win->workspace);
    font_draw_string(cx + 5, cy + 105, buf, fb_rgb(255,255,0), fb_rgb(35,35,40));
}

static void about_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win; (void)cw; (void)ch;
    font_draw_string(cx + 10, cy + 10, "NyxOS Desktop", fb_rgb(100,200,100), fb_rgb(35,35,40));
    font_draw_string(cx + 10, cy + 30, "Version 0.2.0", fb_rgb(200,200,200), fb_rgb(35,35,40));
    font_draw_string(cx + 10, cy + 60, "A lightweight desktop OS", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 10, cy + 80, "inspired by Linux Mint.", fb_rgb(160,160,160), fb_rgb(35,35,40));
}

static void settings_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win;
    fb_fill_rect(cx, cy, cw, ch, fb_rgb(30,30,35));
    char buf[128];
    int y = cy + 10;
    font_draw_string(cx + 10, y, "System Settings", fb_rgb(100,200,100), fb_rgb(30,30,35));
    y += 30;
    snprintf(buf, sizeof(buf), "Kernel: %s %s (%s)", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
    font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35));
    y += 20;
    snprintf(buf, sizeof(buf), "Memory: %d MB total, %d MB used, %d MB free",
        memory_total / (1024*1024), memory_used / (1024*1024),
        (memory_total - memory_used) / (1024*1024));
    font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35));
    y += 20;
    snprintf(buf, sizeof(buf), "Uptime: %d ticks (%d sec)", tick_count, tick_count / 1000);
    font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35));
    y += 20;
    snprintf(buf, sizeof(buf), "Heap: %d KB", KERNEL_HEAP_SIZE / 1024);
    font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35));
    y += 20;
    snprintf(buf, sizeof(buf), "Windows: %d / %d", window_count, MAX_WINDOWS);
    font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35));
}

#define NUM_DESKTOP_ICONS 6
#define ICON_SIZE 64
#define ICON_PAD 12
static const char* desktop_icon_names[] = {
    "Files", "Terminal", "DOOM", "Settings", "About", "Paint"
};
static int desktop_icon_actions[] = {0, 3, 6, 4, 10, 8};
static int desktop_icon_x[NUM_DESKTOP_ICONS];
static int desktop_icon_y[NUM_DESKTOP_ICONS];

static void init_desktop_icons(void) {
    for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
        desktop_icon_x[i] = 20 + i * (ICON_SIZE + ICON_PAD);
        desktop_icon_y[i] = 20;
    }
}

static int desktop_icon_hit(int mx, int my) {
    uint32_t fh = fb_get_height();
    if (my >= (int)(fh - 36)) return -1;  // taskbar area
    for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
        if (mx >= desktop_icon_x[i] && mx < desktop_icon_x[i] + ICON_SIZE &&
            my >= desktop_icon_y[i] && my < desktop_icon_y[i] + ICON_SIZE + 16)
            return i;
    }
    return -1;
}

static void draw_desktop_icons(void) {
    for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
        int x = desktop_icon_x[i];
        int y = desktop_icon_y[i];
        fb_fill_rect(x, y, ICON_SIZE, ICON_SIZE, fb_rgb(45,50,65));
        fb_fill_rect(x+1, y+1, ICON_SIZE-2, ICON_SIZE-2, fb_rgb(55,60,75));
        uint32_t icon_color[] = {
            fb_rgb(70,130,200), fb_rgb(0,200,0), fb_rgb(200,50,50),
            fb_rgb(200,200,50), fb_rgb(100,200,100), fb_rgb(200,100,200)
        };
        fb_fill_rect(x+12, y+8, ICON_SIZE-24, ICON_SIZE-24, icon_color[i % 6]);
        fb_fill_rect(x+18, y+14, ICON_SIZE-36, 3, fb_rgb(255,255,255));
        fb_fill_rect(x+18, y+22, ICON_SIZE-36, 3, fb_rgb(255,255,255));
        fb_fill_rect(x+18, y+30, ICON_SIZE-36, 3, fb_rgb(255,255,255));
        int tw = strlen(desktop_icon_names[i]) * 8;
        int tx = x + (ICON_SIZE - tw) / 2;
        if (tx < 0) tx = 0;
        font_draw_string(tx, y + ICON_SIZE + 2, desktop_icon_names[i], fb_rgb(220,220,220), fb_rgb(30,35,50));
    }
}

static void draw_welcome_windows(void) {
    window_create(80, 40, 400, 280, "Welcome to NyxOS", demo_draw_fn);
    window_create(200, 120, 450, 200, "About NyxOS", about_draw_fn);
    {
        window_t* twin = window_create(300, 200, 640, 400, "Terminal", terminal_win_draw);
        if (twin) {
            twin->reserved = terminal_create_ctx();
            twin->on_key = terminal_win_key;
        }
    }
}

void compositor_run(void) {
    compositor_active = 1;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    quit = 0;
    mouse_x = fw / 2; mouse_y = fh / 2;
    mouse_set_pos(mouse_x, mouse_y);

    draw_welcome_windows();
    redraw_all();
    save_cursor_bg(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);

    int redraw = 0;
    uint32_t clock_tick = 0;

    while (!quit) {
        kernel_poll_net();
        run_background_tasks();

        int k = getkey_poll();
        if (k == 0x1B) { quit = 1; break; }

        int mx = mouse_get_x();
        int my = mouse_get_y();
        uint8_t btns = mouse_get_buttons();

        restore_cursor_bg(mouse_x, mouse_y);

        if (mx < 0) mx = 0;
        if (mx >= (int)fw) mx = fw - 1;
        if (my < 0) my = 0;
        if (my >= (int)fh) my = fh - 1;

        if (drag_id && !(btns & 1)) {
            drag_id = 0; redraw = 1;
        }

        if (resize_id && !(btns & 1)) {
            resize_id = 0; redraw = 1;
        }

        if (btns & 1) {
            if (drag_id) {
                window_t* win = find_window(drag_id);
                if (win) {
                    window_move(drag_id, mx - win->drag_off_x, my - win->drag_off_y);
                    redraw = 1;
                }
            } else if (resize_id) {
                window_t* win = find_window(resize_id);
                if (win) {
                    int dx = mx - win->resize_start_x;
                    int dy = my - win->resize_start_y;
                    int nx = win->x, ny = win->y;
                    uint32_t nw = win->resize_start_w, nh = win->resize_start_h;
                    switch (win->resize_dir) {
                        case RESIZE_RIGHT:
                        case RESIZE_RIGHT_TOP:
                        case RESIZE_CORNER:
                            nw = win->resize_start_w + dx; break;
                        case RESIZE_LEFT:
                        case RESIZE_LEFT_TOP:
                        case RESIZE_LEFT_BOTTOM:
                            nw = (int)win->resize_start_w - dx;
                            nx = win->resize_start_x + dx;
                            break;
                    }
                    switch (win->resize_dir) {
                        case RESIZE_BOTTOM:
                        case RESIZE_CORNER:
                        case RESIZE_LEFT_BOTTOM:
                            nh = win->resize_start_h + dy; break;
                        case RESIZE_TOP:
                        case RESIZE_LEFT_TOP:
                        case RESIZE_RIGHT_TOP:
                            nh = (int)win->resize_start_h - dy;
                            ny = win->resize_start_y + dy;
                            break;
                    }
                    if ((int)nw < MIN_WIN_W) { nw = MIN_WIN_W; nx = win->x; }
                    if ((int)nh < MIN_WIN_H) { nh = MIN_WIN_H; ny = win->y; }
                    win->x = nx; win->y = ny;
                    window_resize(resize_id, nw, nh);
                    redraw = 1;
                }
            } else {
                window_t* hit = NULL;
                window_t* sorted[MAX_WINDOWS];
                int n = 0;
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (windows[i] && windows[i]->visible && windows[i]->state != WSTATE_MINIMIZED
                        && windows[i]->workspace == current_workspace)
                        sorted[n++] = windows[i];
                for (int i = 0; i < n; i++)
                    for (int j = i + 1; j < n; j++)
                        if (sorted[i]->z_order < sorted[j]->z_order) {
                            window_t* t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t;
                        }

                if (start_menu_open) {
                    int idx;
                    if (start_menu_item_hit(mx, my, &idx)) {
                        do_start_menu_action(idx);
                        redraw = 1;
                    } else if (!start_menu_hit(mx, my)) {
                        start_menu_open = 0;
                        redraw = 1;
                    }
                    goto done_click;
                }

                if (start_hit(mx, my)) {
                    start_menu_open = !start_menu_open;
                    redraw = 1;
                    goto done_click;
                }

                int tb_win_id;
                if (taskbar_win_hit(mx, my, &tb_win_id)) {
                    window_t* w = find_window(tb_win_id);
                    if (w) {
                        if (w->state == WSTATE_MINIMIZED) {
                            w->state = WSTATE_NORMAL;
                            w->workspace = current_workspace;
                            window_focus(tb_win_id);
                        } else if (w->focused) {
                            window_minimize(tb_win_id);
                        } else {
                            window_focus(tb_win_id);
                        }
                        redraw = 1;
                    }
                    goto done_click;
                }

                {
                    int icon_idx = desktop_icon_hit(mx, my);
                    if (icon_idx >= 0) {
                        do_start_menu_action(desktop_icon_actions[icon_idx]);
                        redraw = 1;
                        goto done_click;
                    }
                }

                for (int i = 0; i < n; i++) {
                    if (window_hit(sorted[i], mx, my)) {
                        hit = sorted[i]; break;
                    }
                }

                if (hit) {
                    window_focus(hit->id);
                    redraw = 1;

                    if (close_hit(hit, mx, my)) {
                        window_destroy(hit->id);
                    } else if (max_hit(hit, mx, my)) {
                        if (hit->state == WSTATE_MAXIMIZED) window_restore(hit->id);
                        else window_maximize(hit->id);
                    } else if (min_hit(hit, mx, my)) {
                        window_minimize(hit->id);
                    } else if (titlebar_hit(hit, mx, my)) {
                        hit->dragging = 1;
                        hit->drag_off_x = mx - hit->x;
                        hit->drag_off_y = my - hit->y;
                        drag_id = hit->id;
                    } else {
                        int rdir;
                        if (resize_hit(hit, mx, my, &rdir)) {
                            hit->resizing = 1;
                            hit->resize_dir = rdir;
                            hit->resize_start_x = mx;
                            hit->resize_start_y = my;
                            hit->resize_start_w = hit->w;
                            hit->resize_start_h = hit->h;
                            resize_id = hit->id;
                        } else if (hit->on_click) {
                            hit->on_click(hit, mx, my);
                            redraw = 1;
                        }
                    }
                }
            }
        } else {
            // Keyboard shortcuts (workspace switching) — NOT routed to windows
            if (k >= '1' && k <= '4') {
                current_workspace = k - '1';
                redraw = 1;
                goto done_click;
            }

            // Route all keys to focused window's key handler
            window_t* fwin = find_window(focused_id);
            if (fwin && fwin->on_key && k > 0) {
                fwin->on_key(fwin, k);
                redraw = 1;
            }
        }

done_click:
        mouse_x = mx; mouse_y = my;
        mouse_btns = btns;

        uint32_t now = get_ticks();
        if (now - clock_tick > 1000) {
            clock_tick = now;
            redraw = 1;
        }

        if (redraw) {
            redraw_all();
            redraw = 0;
        }

        save_cursor_bg(mouse_x, mouse_y);
        draw_cursor(mouse_x, mouse_y);
        sleep(10);
    }

    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i]) { kfree(windows[i]); windows[i] = NULL; }
    window_count = 0;
    init_screen();
    clear_screen();
    compositor_active = 0;
}

int compositor_is_running(void) {
    return compositor_active;
}

void compositor_quit(void) {
    quit = 1;
}
