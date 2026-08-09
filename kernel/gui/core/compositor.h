#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "../../core/kernel.h"

#define MAX_WINDOWS        32
#define TITLE_H            22

/* THE client-area origin. A window's (x, y) is the top-left of its TITLE BAR,
 * and its content starts TITLE_H below that. The draw callbacks are handed the
 * client origin already computed, but every hit-test handler worked it out for
 * itself from win->y — and simply forgot the title bar. That is why clicks in
 * Paint, the Text Editor and Sound Test all landed exactly TITLE_H = 22 px above
 * what was drawn: four separate symptoms of one missing term.
 *
 * Exported here so there is ONE definition of where a window's content begins,
 * rather than each module re-deriving it and being right or wrong on its own. */
#define WIN_CLIENT_X(w) ((int)(w)->x)
#define WIN_CLIENT_Y(w) ((int)(w)->y + TITLE_H)
#define CLOSE_W            16
#define MAX_TITLE          48
#define MIN_WIN_W          120
#define MIN_WIN_H          80
#define RESIZE_MARGIN      6
#define WORKSPACE_COUNT    4
#define TASKBAR_H          36
#define START_W            160
#define START_H            400
#define CLOCK_W            140   /* wide enough for "Www HH:MM  DD/MM" (weekday + time + date) */

enum {
    RESIZE_NONE,
    RESIZE_RIGHT,
    RESIZE_BOTTOM,
    RESIZE_CORNER,
    RESIZE_LEFT,
    RESIZE_TOP,
    RESIZE_LEFT_TOP,
    RESIZE_RIGHT_TOP,
    RESIZE_LEFT_BOTTOM,
};

enum {
    WSTATE_NORMAL,
    WSTATE_MINIMIZED,
    WSTATE_MAXIMIZED,
    WSTATE_SNAP_LEFT,   // tiled to the left half of the usable area
    WSTATE_SNAP_RIGHT,  // tiled to the right half
    WSTATE_SNAP_TL,     // top-left quarter
    WSTATE_SNAP_TR,     // top-right quarter
    WSTATE_SNAP_BL,     // bottom-left quarter
    WSTATE_SNAP_BR,     // bottom-right quarter
};

typedef struct window window_t;

typedef void (*window_draw_fn)(window_t* win, int clip_x, int clip_y, uint32_t clip_w, uint32_t clip_h);

struct window {
    int x, y;
    uint32_t w, h;
    int normal_x, normal_y;
    uint32_t normal_w, normal_h;
    char title[MAX_TITLE];
    int z_order;
    int visible;
    int state;
    int dragging;
    int drag_off_x, drag_off_y;
    int resizing;
    int resize_dir;
    int resize_start_x, resize_start_y;
    uint32_t resize_start_w, resize_start_h;
    int focused;
    int id;
    int workspace;
    int has_close;
    int has_min;
    int has_max;
    window_draw_fn draw;
    void (*on_key)(struct window* win, int key);
    void (*on_click)(struct window* win, int mx, int my, int btn);
    void (*on_pressed)(struct window* win, int mx, int my, int btn);
    void (*on_mousemove)(struct window* win, int mx, int my, int btns);
    int  (*on_tick)(struct window* win);   // periodic ~30fps tick; returns 1 if it changed something needing a redraw (0 = idle, no repaint)
    void (*on_close)(struct window* win);  // optional: called by window_destroy BEFORE freeing `reserved`, so an app can release ctx sub-allocations
    void* reserved;
};

void compositor_init(void);
window_t* window_create(int x, int y, uint32_t w, uint32_t h, const char* title, window_draw_fn draw);
// Change the screen mode and re-flow icons + open windows onto it. Callers must
// pass a mode the hardware actually supports — vbe_set_mode validates nothing.
void display_set_mode(uint32_t w, uint32_t h);
void window_destroy(int id);
void window_focus(int id);
void window_move(int id, int x, int y);
void window_resize(int id, uint32_t w, uint32_t h);
void window_minimize(int id);
void window_maximize(int id);
void window_restore(int id);
void window_snap(int id, int left);   // tile the window to the left (1) or right (0) half
void window_set_workspace(int id, int ws);
int  window_get_count(void);
int  window_get_ids(int* ids, int max);
void compositor_run(void);
void compositor_quit(void);
int compositor_is_running(void);
window_t* compositor_open_editor(const char* path);  // open Text Editor, optionally with a file
extern int compositor_logout_requested;              // user menu "Log out" -> boot loop re-shows login

#endif
