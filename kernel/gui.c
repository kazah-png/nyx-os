#include "kernel.h"

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

static int quit = 0;

static void draw_pixel(int x, int y, uint32_t color) {
    uint32_t w = fb_get_width(), h = fb_get_height();
    if (x < 0 || x >= (int)w || y < 0 || y >= (int)h) return;
    ((uint32_t*)fb_get_addr())[y * w + x] = color;
}

static void save_bg(int mx, int my, uint32_t* bg) {
    uint32_t w = fb_get_width();
    for (int y = 0; y < CURSOR_H && my + y < (int)fb_get_height(); y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fb_get_width(); x++)
            bg[y * CURSOR_W + x] = ((uint32_t*)fb_get_addr())[(my + y) * w + (mx + x)];
}

static void restore_bg(int mx, int my, uint32_t* bg) {
    uint32_t w = fb_get_width();
    for (int y = 0; y < CURSOR_H && my + y < (int)fb_get_height(); y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fb_get_width(); x++)
            ((uint32_t*)fb_get_addr())[(my + y) * w + (mx + x)] = bg[y * CURSOR_W + x];
}

static uint32_t cursor_bg_buf[CURSOR_H * CURSOR_W];

void gui_demo(void) {
    uint32_t w = fb_get_width(), h = fb_get_height();
    uint32_t colors[] = {
        fb_rgb(255,0,0), fb_rgb(0,255,0), fb_rgb(0,0,255),
        fb_rgb(255,255,0), fb_rgb(255,0,255), fb_rgb(0,255,255),
    };
    int color_idx = 0;
    quit = 0;

    // Draw instruction bar
    fb_fill_rect(0, 0, w, 20, fb_rgb(40,40,40));
    for (int i = 0; i < (int)(sizeof(colors)/sizeof(colors[0])); i++)
        fb_fill_rect(4 + i * 28, 4, 24, 12, colors[i]);
    fb_fill_rect(4 + color_idx * 28, 2, 24, 16, fb_rgb(255,255,255));

    char press_esc[] = "Press ESC to exit";
    for (int i = 0; press_esc[i]; i++)
        draw_pixel(w - 180 + i * 8, 6, fb_rgb(180,180,180));

    int prev_draw_x = -1, prev_draw_y = -1;
    int cursor_x = w / 2, cursor_y = h / 2;
    mouse_set_pos(cursor_x, cursor_y);

    while (!quit) {
        char k = getchar_poll();
        if (k == 0x1B) { quit = 1; break; }
        if (k >= '1' && k <= '6') {
            color_idx = k - '1';
            fb_fill_rect(4 + (k - '1') * 28, 2, 24, 16, fb_rgb(255,255,255));
        }

        int mx = mouse_get_x();
        int my = mouse_get_y();

        // Restore background under old cursor
        restore_bg(cursor_x, cursor_y, cursor_bg_buf);

        // Clamp
        if (mx < 0) mx = 0;
        if (mx >= (int)w) mx = (int)w - 1;
        if (my < 0) my = 0;
        if (my >= (int)h) my = (int)h - 1;

        uint8_t btns = mouse_get_buttons();

        // Draw when left button held
        if (btns & 1) {
            if (prev_draw_x >= 0 && prev_draw_y >= 0) {
                int x0 = prev_draw_x, y0 = prev_draw_y;
                int x1 = mx, y1 = my;
                int steps = (x1 > x0 ? x1 - x0 : x0 - x1) > (y1 > y0 ? y1 - y0 : y0 - y1)
                    ? (x1 > x0 ? x1 - x0 : x0 - x1)
                    : (y1 > y0 ? y1 - y0 : y0 - y1);
                for (int i = 0; i <= steps; i++) {
                    int x = x0 + (x1 - x0) * i / steps;
                    int y = y0 + (y1 - y0) * i / steps;
                    if (y > 20) draw_pixel(x, y, colors[color_idx]);
                }
            } else {
                if (my > 20) draw_pixel(mx, my, colors[color_idx]);
            }
        }
        prev_draw_x = (btns & 1) ? mx : -1;
        prev_draw_y = (btns & 1) ? my : -1;

        // Save background at new cursor position
        save_bg(mx, my, cursor_bg_buf);
        cursor_x = mx;
        cursor_y = my;

        // Draw cursor
        uint32_t* fb = (uint32_t*)fb_get_addr();
        for (int y = 0; y < CURSOR_H && (uint32_t)(my + y) < h; y++) {
            for (int x = 0; x < CURSOR_W && (uint32_t)(mx + x) < w; x++) {
                if (cursor_data[y][x] == 1)
                    fb[(my + y) * w + (mx + x)] = fb_rgb(0,0,0);
                else if (cursor_data[y][x] == 2)
                    fb[(my + y) * w + (mx + x)] = fb_rgb(255,255,255);
            }
        }

        sleep(10);
    }

    // Restore text mode
    init_screen();
    clear_screen();
    printf("GUI demo exited.\n");
}
