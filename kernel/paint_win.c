#include "theme.h"
#include "kernel.h"
#include "compositor.h"
#include "paint_win.h"
#include "font.h"

#define SWATCH_SIZE 20
#define SWATCH_PAD 4
#define COLOR_START_X 150
#define CANVAS_OFFSET_Y (PAINT_TOOLBAR_H + PAINT_STATUS_H)

static uint32_t preset_colors[PAINT_NUM_COLORS];
static int preset_colors_init = 0;

static void init_preset_colors(void) {
    if (preset_colors_init) return;
    preset_colors_init = 1;
    preset_colors[0] = fb_rgb(0,0,0);
    preset_colors[1] = fb_rgb(255,255,255);
    preset_colors[2] = fb_rgb(255,0,0);
    preset_colors[3] = fb_rgb(0,255,0);
    preset_colors[4] = fb_rgb(0,0,255);
    preset_colors[5] = fb_rgb(255,255,0);
    preset_colors[6] = fb_rgb(255,128,0);
    preset_colors[7] = fb_rgb(128,0,255);
    preset_colors[8] = fb_rgb(0,255,255);
    preset_colors[9] = fb_rgb(128,128,128);
}

static void draw_circle(paint_win_t* pw, int cx, int cy, int r) {
    uint32_t s = 0;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < PAINT_CANVAS_W && py >= 0 && py < PAINT_CANVAS_H) {
                    if (!pw->brush_style) { s = ((px / 8) + (py / 8)) & 1 ? fb_rgb(200,200,200) : fb_rgb(240,240,240); }
                    else if (pw->brush_style == 1) { s = pw->brush_color; }
                    pw->canvas[py * PAINT_CANVAS_W + px] = s;
                }
            }
        }
    }
}

static void draw_line(paint_win_t* pw, int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        draw_circle(pw, x, y, pw->brush_size / 2);
    }
}

paint_win_t* paint_create_ctx(void) {
    init_preset_colors();
    paint_win_t* pw = (paint_win_t*)kmalloc(sizeof(paint_win_t));
    if (!pw) return NULL;
    // Initialize with checkerboard background
    for (int yy = 0; yy < PAINT_CANVAS_H; yy++)
        for (int xx = 0; xx < PAINT_CANVAS_W; xx++)
            pw->canvas[yy * PAINT_CANVAS_W + xx] = ((xx / 8) + (yy / 8)) & 1
                ? fb_rgb(200,200,200) : fb_rgb(240,240,240);
    pw->brush_size = 4;
    pw->brush_color = fb_rgb(0,0,0);
    pw->brush_style = 1;
    pw->last_x = 0; pw->last_y = 0;
    snprintf(pw->status, sizeof(pw->status), "Brush: %dpx", pw->brush_size);
    return pw;
}

void paint_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)ch;
    paint_win_t* pw = (paint_win_t*)win->reserved;
    if (!pw) return;

    // Toolbar background
    fb_fill_rect(cx, cy, cw, PAINT_TOOLBAR_H, THEME_ROW_DIV);

    // Brush size label and +/- buttons
    char buf[32];
    snprintf(buf, sizeof(buf), "Brush: %d", pw->brush_size);
    font_draw_string(cx + 6, cy + (PAINT_TOOLBAR_H - FONT_HEIGHT) / 2, buf, THEME_TEXT, THEME_ROW_DIV);

    // [-] button
    int bx = cx + 90, by = cy + 4;
    fb_fill_rect(bx, by, 22, PAINT_TOOLBAR_H - 8, fb_rgb(70,70,75));
    font_draw_string(bx + 5, by + 3, "-", fb_rgb(255,255,255), fb_rgb(70,70,75));

    // [+] button
    bx = cx + 116;
    fb_fill_rect(bx, by, 22, PAINT_TOOLBAR_H - 8, fb_rgb(70,70,75));
    font_draw_string(bx + 5, by + 3, "+", fb_rgb(255,255,255), fb_rgb(70,70,75));

    // Color swatches
    for (int i = 0; i < PAINT_NUM_COLORS; i++) {
        int sx = cx + COLOR_START_X + i * (SWATCH_SIZE + SWATCH_PAD);
        int sy = cy + (PAINT_TOOLBAR_H - SWATCH_SIZE) / 2;
        fb_fill_rect(sx, sy, SWATCH_SIZE, SWATCH_SIZE, preset_colors[i]);
        if (pw->brush_color == preset_colors[i])
            fb_fill_rect(sx - 1, sy - 1, SWATCH_SIZE + 2, SWATCH_SIZE + 2, fb_rgb(255,255,255));
    }

    // Clear button
    int clr_x = cx + COLOR_START_X + PAINT_NUM_COLORS * (SWATCH_SIZE + SWATCH_PAD) + 10;
    int clr_w = BUTTONS_WIDTH;
    fb_fill_rect(clr_x, cy + 4, clr_w, PAINT_TOOLBAR_H - 8, fb_rgb(120,40,40));
    font_draw_string(clr_x + 8, cy + (PAINT_TOOLBAR_H - FONT_HEIGHT) / 2, "Clear", fb_rgb(255,220,220), fb_rgb(120,40,40));

    // Erase button
    int ers_x = cx + COLOR_START_X + PAINT_NUM_COLORS * (SWATCH_SIZE + SWATCH_PAD) + 20 + clr_w;
    int ers_w = BUTTONS_WIDTH;
    fb_fill_rect(ers_x, cy + 4, ers_w, PAINT_TOOLBAR_H - 8, fb_rgb(120,40,40));
    font_draw_string(ers_x + 8, cy + (PAINT_TOOLBAR_H - FONT_HEIGHT) / 2, "Erase", fb_rgb(255,220,220), fb_rgb(120,40,40));

    // Canvas area
    int canvas_x = cx + (int)(cw - PAINT_CANVAS_W) / 2;
    int canvas_y = cy + CANVAS_OFFSET_Y;
    if (canvas_x < cx) canvas_x = cx;
    if (canvas_y < cy + CANVAS_OFFSET_Y) canvas_y = cy + CANVAS_OFFSET_Y;

    // Blit canvas buffer (pre-rendered with checkerboard + brush strokes)
    fb_blit(pw->canvas, 0, 0, PAINT_CANVAS_W, PAINT_CANVAS_H, canvas_x, canvas_y);

    // Canvas border
    fb_fill_rect(canvas_x - 1, canvas_y - 1, PAINT_CANVAS_W + 2, 1, THEME_BORDER);
    fb_fill_rect(canvas_x - 1, canvas_y + PAINT_CANVAS_H, PAINT_CANVAS_W + 2, 1, THEME_BORDER);
    fb_fill_rect(canvas_x - 1, canvas_y, 1, PAINT_CANVAS_H, THEME_BORDER);
    fb_fill_rect(canvas_x + PAINT_CANVAS_W, canvas_y, 1, PAINT_CANVAS_H, THEME_BORDER);

    // Status bar
    int status_y = cy + CANVAS_OFFSET_Y + PAINT_CANVAS_H;
    fb_fill_rect(cx, status_y, cw, PAINT_STATUS_H, THEME_WINDOW_BG);
    font_draw_string(cx + 4, status_y + 2, pw->status, fb_rgb(180,180,200), THEME_WINDOW_BG);
}

void paint_win_click(window_t* win, int mx, int my, int btn) {
    paint_win_t* pw = (paint_win_t*)win->reserved;
    if (!pw) return;
    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);   /* content, not title bar */
    uint32_t cw = win->w;
    (void)win->h;

    // Left click
    if (btn == 1) {
        // Toolbar clicks
        if (my >= cy && my < cy + PAINT_TOOLBAR_H + TITLE_H) {
            int rx = mx - cx;

            // [-] button at 90
            if (rx >= 90 && rx < 112 && pw->brush_size > PAINT_MIN_BRUSH) {
                pw->brush_size--;
                snprintf(pw->status, sizeof(pw->status), "Brush: %dpx", pw->brush_size);
                return;
            }
            // [+] button at 116
            if (rx >= 116 && rx < 138 && pw->brush_size < PAINT_MAX_BRUSH) {
                pw->brush_size++;
                snprintf(pw->status, sizeof(pw->status), "Brush: %dpx", pw->brush_size);
                return;
            }
            // Color swatches
            for (int i = 0; i < PAINT_NUM_COLORS; i++) {
                int sx = COLOR_START_X + i * (SWATCH_SIZE + SWATCH_PAD);
                if (rx >= sx && rx < sx + SWATCH_SIZE) {
                    pw->brush_color = preset_colors[i];
                    return;
                }
            }
            // Clear button
            int clr_x = COLOR_START_X + PAINT_NUM_COLORS * (SWATCH_SIZE + SWATCH_PAD) + 10;
            if (rx >= clr_x && rx < clr_x + 60) {
                for (int yy = 0; yy < PAINT_CANVAS_H; yy++)
                    for (int xx = 0; xx < PAINT_CANVAS_W; xx++)
                        pw->canvas[yy * PAINT_CANVAS_W + xx] = ((xx / 8) + (yy / 8)) & 1
                            ? fb_rgb(200,200,200) : fb_rgb(240,240,240);
                snprintf(pw->status, sizeof(pw->status), "Canvas cleared");
                return;
            }

            // Erase button
            int ers_x = COLOR_START_X + PAINT_NUM_COLORS * (SWATCH_SIZE + SWATCH_PAD) + 20 + BUTTONS_WIDTH;
            if (rx >= ers_x && rx < ers_x + BUTTONS_WIDTH) {
                if (pw->brush_style) {
                    snprintf(pw->status, sizeof(pw->status), "Erase mode: on");
                    pw->brush_style = 0;
                } else {
                    snprintf(pw->status, sizeof(pw->status), "Erase mode: off");
                    pw->brush_style = 1;
                }
                return;
            }
            return;
        }

        // Canvas area click
        int canvas_x = cx + (int)(cw - PAINT_CANVAS_W) / 2;
        int canvas_y = cy + CANVAS_OFFSET_Y;
        if (canvas_x < cx) canvas_x = cx;
        if (canvas_y < cy + CANVAS_OFFSET_Y) canvas_y = cy + CANVAS_OFFSET_Y;

        if (mx >= canvas_x && mx < canvas_x + PAINT_CANVAS_W &&
            my >= canvas_y && my < canvas_y + PAINT_CANVAS_H) {
            int px = mx - canvas_x;
            int py = my - canvas_y;
            draw_circle(pw, px, py, pw->brush_size / 2);
            pw->last_x = px;
            pw->last_y = py;
        }
    }
}

void paint_win_key(window_t* win, int key) {
    paint_win_t* pw = (paint_win_t*)win->reserved;
    if (!pw) return;

    if (key == '+' || key == '=') {
        if (pw->brush_size < PAINT_MAX_BRUSH) {
            pw->brush_size++;
            snprintf(pw->status, sizeof(pw->status), "Brush: %dpx", pw->brush_size);
        }
    } else if (key == '-' || key == '_') {
        if (pw->brush_size > PAINT_MIN_BRUSH) {
            pw->brush_size--;
            snprintf(pw->status, sizeof(pw->status), "Brush: %dpx", pw->brush_size);
        }
    } else if (key == 'c' || key == 'C') {
        for (int yy = 0; yy < PAINT_CANVAS_H; yy++)
            for (int xx = 0; xx < PAINT_CANVAS_W; xx++)
                pw->canvas[yy * PAINT_CANVAS_W + xx] = ((xx / 8) + (yy / 8)) & 1
                    ? fb_rgb(200,200,200) : fb_rgb(240,240,240);
        snprintf(pw->status, sizeof(pw->status), "Canvas cleared");
    }
}

void paint_win_pressed(window_t* win, int mx, int my, int btns) {
    (void)btns;
    paint_win_t* pw = (paint_win_t*)win->reserved;
    if (!pw) return;
    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);
    uint32_t cw = win->w;

    int canvas_x = cx + (int)(cw - PAINT_CANVAS_W) / 2;
    int canvas_y = cy + CANVAS_OFFSET_Y;
    if (canvas_x < cx) canvas_x = cx;
    if (canvas_y < cy + CANVAS_OFFSET_Y) canvas_y = cy + CANVAS_OFFSET_Y;

    if (mx >= canvas_x && mx < canvas_x + PAINT_CANVAS_W &&
        my >= canvas_y && my < canvas_y + PAINT_CANVAS_H) {
        int px = mx - canvas_x;
        int py = my - canvas_y;
        draw_line(pw, pw->last_x, pw->last_y, px, py);
        pw->last_x = px;
        pw->last_y = py;
    }
}