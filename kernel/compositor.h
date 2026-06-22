#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "kernel.h"

#define MAX_WINDOWS 16
#define TITLE_H 20
#define CLOSE_W 16
#define MAX_TITLE 32

typedef struct window window_t;

typedef void (*window_draw_fn)(window_t* win);

struct window {
    int x, y;
    uint32_t w, h;
    char title[MAX_TITLE];
    int z_order;
    int visible;
    int dirty;
    int dragging;
    int drag_off_x, drag_off_y;
    int focused;
    int id;
    window_draw_fn draw;
};

int compositor_init(void);
window_t* window_create(int x, int y, uint32_t w, uint32_t h, const char* title);
void window_close(int id);
void window_focus(int id);
void window_move(int id, int x, int y);
void compositor_run(void);

#endif
