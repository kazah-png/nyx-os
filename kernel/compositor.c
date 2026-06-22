#include "kernel.h"
#include "compositor.h"

static window_t* windows[MAX_WINDOWS];
static int window_count = 0;
static int next_id = 1;
static int focused_id = 0;
static int drag_id = 0;
static int quit = 0;

// Cursor data from gui.c
#define CURSOR_W 12
#define CURSOR_H 16
static const uint8_t cursor_data[CURSOR_H][CURSOR_W] = {
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,1,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
};
static uint32_t cursor_bg[CURSOR_H * CURSOR_W];
static int cursor_x, cursor_y;

static void compositor_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t w = fb_get_width(), h = fb_get_height();
    if (x >= w || y >= h) return;
    ((uint32_t*)fb_get_addr())[y * w + x] = color;
}

static void save_cursor_bg(int mx, int my) {
    uint32_t w = fb_get_width();
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++)
            cursor_bg[y * CURSOR_W + x] = ((uint32_t*)fb_get_addr())[(my + y) * w + (mx + x)];
}

static void restore_cursor_bg(int mx, int my) {
    uint32_t w = fb_get_width();
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++)
            ((uint32_t*)fb_get_addr())[(my + y) * w + (mx + x)] = cursor_bg[y * CURSOR_W + x];
}

static void draw_cursor(int mx, int my) {
    uint32_t w = fb_get_width(), h = fb_get_height();
    uint32_t* fb = (uint32_t*)fb_get_addr();
    for (int y = 0; y < CURSOR_H && (uint32_t)(my + y) < h; y++)
        for (int x = 0; x < CURSOR_W && (uint32_t)(mx + x) < w; x++) {
            if (cursor_data[y][x] == 1)
                fb[(my + y) * w + (mx + x)] = fb_rgb(0,0,0);
            else if (cursor_data[y][x] == 2)
                fb[(my + y) * w + (mx + x)] = fb_rgb(255,255,255);
        }
}

static void draw_titlebar(window_t* win) {
    uint32_t bg = win->focused ? fb_rgb(0,60,120) : fb_rgb(80,80,80);
    fb_fill_rect(win->x, win->y, win->w, TITLE_H, bg);
    font_draw_string(win->x + 4, win->y + 2, win->title, fb_rgb(255,255,255), bg);
    fb_fill_rect(win->x + win->w - CLOSE_W - 2, win->y + 2, CLOSE_W, CLOSE_W,
                 fb_rgb(200,40,40));
    compositor_draw_pixel(win->x + win->w - CLOSE_W - 2 + 4, win->y + 2 + 4, fb_rgb(255,255,255));
    compositor_draw_pixel(win->x + win->w - CLOSE_W - 2 + 5, win->y + 2 + 4, fb_rgb(255,255,255));
    compositor_draw_pixel(win->x + win->w - CLOSE_W - 2 + 4, win->y + 2 + 5, fb_rgb(255,255,255));
    compositor_draw_pixel(win->x + win->w - CLOSE_W - 2 + 5, win->y + 2 + 5, fb_rgb(255,255,255));
}

static int titlebar_hit(window_t* win, int mx, int my) {
    return mx >= win->x && mx < win->x + (int)win->w
        && my >= win->y && my < win->y + TITLE_H;
}

static int close_hit(window_t* win, int mx, int my) {
    return mx >= win->x + (int)win->w - CLOSE_W - 2
        && mx < win->x + (int)win->w - 2
        && my >= win->y + 2 && my < win->y + CLOSE_W + 2;
}

static int window_hit(window_t* win, int mx, int my) {
    return mx >= win->x && mx < win->x + (int)win->w
        && my >= win->y && my < win->y + (int)(win->h + TITLE_H);
}

static void redraw_all(void) {
    fb_clear(fb_rgb(40,40,80));

    // Sort windows by z-order
    window_t* sorted[MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] && windows[i]->visible)
            sorted[n++] = windows[i];
    }
    // Simple insertion sort
    for (int i = 1; i < n; i++) {
        window_t* key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j]->z_order > key->z_order) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    for (int i = 0; i < n; i++) {
        window_t* win = sorted[i];
        // Client area
        fb_fill_rect(win->x, win->y + TITLE_H, win->w, win->h, fb_rgb(30,30,30));
        // Border
        fb_fill_rect(win->x - 1, win->y - 1, win->w + 2, 1, fb_rgb(160,160,160));
        fb_fill_rect(win->x - 1, win->y + TITLE_H, 1, win->h, fb_rgb(160,160,160));
        fb_fill_rect(win->x + win->w, win->y + TITLE_H, 1, win->h, fb_rgb(160,160,160));
        fb_fill_rect(win->x - 1, win->y + TITLE_H + win->h, win->w + 2, 1, fb_rgb(160,160,160));
        draw_titlebar(win);
        if (win->draw) win->draw(win);
    }
}

static void demo_draw(window_t* win) {
    int client_y = win->y + TITLE_H + 5;
    font_draw_string(win->x + 5, client_y, "This is a NyxOS window!",
                     fb_rgb(200,200,200), fb_rgb(30,30,30));
    font_draw_string(win->x + 5, client_y + 18, "Drag title bar to move.",
                     fb_rgb(160,160,160), fb_rgb(30,30,30));
    font_draw_string(win->x + 5, client_y + 36, "Click X to close.",
                     fb_rgb(160,160,160), fb_rgb(30,30,30));
    char buf[32];
    itoa(win->id, buf, 10);
    font_draw_string(win->x + 5, client_y + 54, buf,
                     fb_rgb(255,255,0), fb_rgb(30,30,30));
}

static window_t* find_window(int id) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->id == id)
            return windows[i];
    return NULL;
}

int compositor_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        windows[i] = NULL;
    window_count = 0;
    next_id = 1;
    focused_id = 0;
    drag_id = 0;
    quit = 0;
    return 0;
}

window_t* window_create(int x, int y, uint32_t w, uint32_t h, const char* title) {
    if (window_count >= MAX_WINDOWS) return NULL;
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->z_order = window_count;
    win->visible = 1;
    win->dirty = 1;
    win->dragging = 0;
    win->focused = 0;
    win->id = next_id++;
    win->draw = demo_draw;
    int sl = (int)strlen(title);
    if (sl >= MAX_TITLE) sl = MAX_TITLE - 1;
    memcpy(win->title, title, sl);
    win->title[sl] = '\0';
    windows[slot] = win;
    window_count++;
    window_focus(win->id);
    return win;
}

void window_close(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] == win) {
            windows[i] = NULL;
            break;
        }
    }
    window_count--;
    kfree(win);
    if (focused_id == id) focused_id = 0;
}

void window_focus(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    win->focused = 1;
    win->z_order = window_count + 10;
    focused_id = id;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i] && windows[i]->id != id)
            windows[i]->focused = 0;
    }
}

void window_move(int id, int x, int y) {
    window_t* win = find_window(id);
    if (!win) return;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + win->w > fw) x = fw - win->w;
    if (y + win->h + TITLE_H > fh) y = fh - win->h - TITLE_H;
    win->x = x;
    win->y = y;
}

void compositor_run(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    quit = 0;
    cursor_x = fw / 2;
    cursor_y = fh / 2;
    mouse_set_pos(cursor_x, cursor_y);

    // Desktop text
    font_draw_string(fw / 2 - 80, fh - 30, "NyxOS Desktop - Esc to exit",
                     fb_rgb(120,120,160), fb_rgb(40,40,80));

    redraw_all();
    save_cursor_bg(cursor_x, cursor_y);
    draw_cursor(cursor_x, cursor_y);

    while (!quit) {
        char k = getchar_poll();
        if (k == 0x1B) { quit = 1; break; }

        int mx = mouse_get_x();
        int my = mouse_get_y();
        uint8_t btns = mouse_get_buttons();

        // Restore cursor background
        restore_cursor_bg(cursor_x, cursor_y);

        // Clamp mouse
        if (mx < 0) mx = 0;
        if (mx >= (int)fw) mx = fw - 1;
        if (my < 0) my = 0;
        if (my >= (int)fh) my = fh - 1;

        if (drag_id) {
            if (btns & 1) {
                window_t* win = find_window(drag_id);
                if (win) {
                    window_move(drag_id, mx - win->drag_off_x, my - win->drag_off_y);
                    redraw_all();
                }
            } else {
                drag_id = 0;
            }
        } else if (btns & 1) {
            // Check windows topmost first (highest z)
            window_t* hit = NULL;
            window_t* sorted[MAX_WINDOWS];
            int n = 0;
            for (int i = 0; i < MAX_WINDOWS; i++)
                if (windows[i] && windows[i]->visible)
                    sorted[n++] = windows[i];
            for (int i = 0; i < n; i++)
                for (int j = i + 1; j < n; j++)
                    if (sorted[i]->z_order < sorted[j]->z_order) {
                        window_t* t = sorted[i];
                        sorted[i] = sorted[j];
                        sorted[j] = t;
                    }
            for (int i = 0; i < n; i++) {
                if (window_hit(sorted[i], mx, my)) {
                    hit = sorted[i];
                    break;
                }
            }
            if (hit) {
                window_focus(hit->id);
                if (close_hit(hit, mx, my)) {
                    window_close(hit->id);
                    redraw_all();
                } else if (titlebar_hit(hit, mx, my)) {
                    hit->dragging = 1;
                    hit->drag_off_x = mx - hit->x;
                    hit->drag_off_y = my - hit->y;
                    drag_id = hit->id;
                }
            }
        }

        cursor_x = mx;
        cursor_y = my;
        save_cursor_bg(cursor_x, cursor_y);
        draw_cursor(cursor_x, cursor_y);

        sleep(10);
    }

    // Clean up
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i]) { kfree(windows[i]); windows[i] = NULL; }
    }
    window_count = 0;
    init_screen();
    clear_screen();
}
