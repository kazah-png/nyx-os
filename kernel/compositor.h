#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "kernel.h"

#define MAX_WINDOWS        32
#define TITLE_H            22
#define CLOSE_W            16
#define MAX_TITLE          48
#define MIN_WIN_W          120
#define MIN_WIN_H          80
#define RESIZE_MARGIN      6
#define WORKSPACE_COUNT    4
#define TASKBAR_H          36
#define START_W            160
#define START_H            400
#define CLOCK_W            80

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
    void* reserved;
};

void compositor_init(void);
window_t* window_create(int x, int y, uint32_t w, uint32_t h, const char* title, window_draw_fn draw);
void window_destroy(int id);
void window_focus(int id);
void window_move(int id, int x, int y);
void window_resize(int id, uint32_t w, uint32_t h);
void window_minimize(int id);
void window_maximize(int id);
void window_restore(int id);
void window_set_workspace(int id, int ws);
int  window_get_count(void);
int  window_get_ids(int* ids, int max);
void compositor_run(void);
void compositor_quit(void);
int compositor_is_running(void);
window_t* compositor_open_editor(const char* path);  // open Text Editor, optionally with a file
extern int compositor_logout_requested;              // user menu "Log out" -> boot loop re-shows login

#endif
