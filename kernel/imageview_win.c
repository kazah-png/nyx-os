#include "kernel.h"
#include "compositor.h"
#include "imageview_win.h"
#include "font.h"

#define TOOLBAR_H 26
#define STATUS_H 18

static void generate_test_pattern(uint8_t* buf, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t* px = (uint32_t*)(buf + (y * w + x) * 4);
            if (y < h / 6) {
                // White
                *px = 0xFFFFFFFF;
            } else if (y < 2 * h / 6) {
                // Yellow
                *px = 0xFF00FFFF;
            } else if (y < 3 * h / 6) {
                // Cyan
                *px = 0xFFFF00FF;
            } else if (y < 4 * h / 6) {
                // Green
                *px = 0xFF00FF00;
            } else if (y < 5 * h / 6) {
                // Magenta
                *px = 0xFFFF00FF;
            } else {
                // Red
                *px = 0xFF0000FF;
            }
            // Add gradient overlay from left to right
            uint8_t gradient = (uint8_t)((x * 255) / w);
            uint8_t r = (*px >> 0) & 0xFF;
            uint8_t g = (*px >> 8) & 0xFF;
            uint8_t b = (*px >> 16) & 0xFF;
            r = (uint8_t)((r * (255 - gradient)) / 255);
            g = (uint8_t)((g * (255 - gradient)) / 255);
            b = (uint8_t)((b * (255 - gradient)) / 255);
            *px = fb_rgb(r, g, b);
        }
    }
    // Add checkerboard grid lines
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (x % 64 < 2 || y % 64 < 2) {
                uint32_t* px = (uint32_t*)(buf + (y * w + x) * 4);
                uint8_t r = ((*px >> 0) & 0xFF) / 2;
                uint8_t g = ((*px >> 8) & 0xFF) / 2;
                uint8_t b = ((*px >> 16) & 0xFF) / 2;
                *px = fb_rgb(r, g, b);
            }
        }
    }
}

imageview_win_t* imageview_create_ctx(void) {
    imageview_win_t* iv = (imageview_win_t*)kmalloc(sizeof(imageview_win_t));
    if (!iv) return NULL;
    memset_asm(iv, 0, sizeof(imageview_win_t));
    iv->img_w = 512;
    iv->img_h = 384;
    iv->zoom = 1.0f;
    // Compute the allocation size in size_t: img_w/img_h are uint32_t, so
    // img_w * img_h * 4 would otherwise be evaluated in 32-bit and could wrap
    // before widening to kmalloc's size_t argument (CodeQL cpp/integer-
    // multiplication-cast-to-long). Casting the first operand keeps it 64-bit.
    iv->pixels = (uint8_t*)kmalloc((size_t)iv->img_w * iv->img_h * 4);
    if (iv->pixels) {
        generate_test_pattern(iv->pixels, iv->img_w, iv->img_h);
    }
    snprintf(iv->status, sizeof(iv->status), "512x384 test pattern | +/- zoom, arrows to pan");
    strncpy(iv->filename, "test_pattern", sizeof(iv->filename) - 1);
    return iv;
}

void imageview_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    imageview_win_t* iv = (imageview_win_t*)win->reserved;
    if (!iv || !iv->pixels) return;

    // Toolbar
    fb_fill_rect(cx, cy, cw, TOOLBAR_H, fb_rgb(55,55,60));
    font_draw_string(cx + 8, cy + (TOOLBAR_H - FONT_HEIGHT) / 2, iv->filename,
                     fb_rgb(200,200,220), fb_rgb(55,55,60));

    int area_y = cy + TOOLBAR_H;
    int area_h = (int)ch - TOOLBAR_H - STATUS_H;

    // Fill background
    fb_fill_rect(cx, area_y, cw, area_h, fb_rgb(25,25,28));

    // Draw the image using blit
    int draw_x = cx + 4 + iv->offset_x;
    int draw_y = area_y + 4 + iv->offset_y;
    uint32_t disp_w = (uint32_t)(iv->img_w * iv->zoom);
    uint32_t disp_h = (uint32_t)(iv->img_h * iv->zoom);

    // For simplicity, just draw nearest-neighbor scaled
    for (uint32_t dy = 0; dy < disp_h && (uint32_t)draw_y + dy < (uint32_t)(area_y + area_h); dy++) {
        uint32_t src_y = (dy * iv->img_h) / disp_h;
        if (src_y >= iv->img_h) src_y = iv->img_h - 1;
        for (uint32_t dx = 0; dx < disp_w && (uint32_t)draw_x + dx < cx + cw; dx++) {
            uint32_t src_x = (dx * iv->img_w) / disp_w;
            if (src_x >= iv->img_w) src_x = iv->img_w - 1;
            uint32_t color = *(uint32_t*)(iv->pixels + (src_y * iv->img_w + src_x) * 4);
            fb_put_pixel((uint32_t)draw_x + dx, (uint32_t)draw_y + dy, color);
        }
    }

    // Border around image
    fb_fill_rect((uint32_t)(draw_x - 1), (uint32_t)(draw_y - 1), disp_w + 2, 1, fb_rgb(100,100,120));
    fb_fill_rect((uint32_t)(draw_x - 1), (uint32_t)(draw_y - 1), 1, disp_h + 2, fb_rgb(100,100,120));
    fb_fill_rect((uint32_t)(draw_x + disp_w), (uint32_t)(draw_y - 1), 1, disp_h + 2, fb_rgb(100,100,120));
    fb_fill_rect((uint32_t)(draw_x - 1), (uint32_t)(draw_y + disp_h), disp_w + 2, 1, fb_rgb(100,100,120));

    // Status bar
    int status_y = cy + (int)ch - STATUS_H;
    fb_fill_rect(cx, status_y, cw, STATUS_H, fb_rgb(45,45,50));
    char st[64];
    snprintf(st, sizeof(st), "%s | zoom: %d%%", iv->status, (int)(iv->zoom * 100));
    font_draw_string(cx + 4, (uint32_t)status_y + 2, st, fb_rgb(180,200,220), fb_rgb(45,45,50));
}

void imageview_win_key(window_t* win, int key) {
    imageview_win_t* iv = (imageview_win_t*)win->reserved;
    if (!iv) return;

    if (key == '+' || key == '=') {
        if (iv->zoom < 4.0f) {
            iv->zoom *= 1.25f;
            if (iv->zoom > 4.0f) iv->zoom = 4.0f;
        }
    } else if (key == '-') {
        if (iv->zoom > 0.25f) {
            iv->zoom /= 1.25f;
            if (iv->zoom < 0.25f) iv->zoom = 0.25f;
        }
    } else if (key == KEY_LEFT) {
        iv->offset_x += 20;
    } else if (key == KEY_RIGHT) {
        iv->offset_x -= 20;
    } else if (key == KEY_UP) {
        iv->offset_y += 20;
    } else if (key == KEY_DOWN) {
        iv->offset_y -= 20;
    } else if (key == 'r' || key == 'R') {
        iv->offset_x = 0;
        iv->offset_y = 0;
        iv->zoom = 1.0f;
    }
}
