#include "../core/theme.h"
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "imageview_win.h"
#include "../../drivers/video/font.h"
#include "../../image/image.h"
#include "../../image/png.h"
#include "../../image/bmp.h"
#include "../../image/gif.h"
#include "../../image/jpeg.h"

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
    fb_fill_rect(cx, cy, cw, TOOLBAR_H, THEME_ROW_DIV);
    font_draw_string(cx + 8, cy + (TOOLBAR_H - FONT_HEIGHT) / 2, iv->filename,
                     fb_rgb(200,200,220), THEME_ROW_DIV);

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
    fb_fill_rect(cx, status_y, cw, STATUS_H, THEME_WINDOW_BG);
    char st[64];
    snprintf(st, sizeof(st), "%s | zoom: %d%%", iv->status, (int)(iv->zoom * 100));
    font_draw_string(cx + 4, (uint32_t)status_y + 2, st, fb_rgb(180,200,220), THEME_WINDOW_BG);
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

// ---------------------------------------------------------------------------
// Real image loading (PNG / BMP / GIF / JPEG) + animated-GIF playback.
// ---------------------------------------------------------------------------

// basename: the part of `path` after the last '/'.
static const char* imageview_basename(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; p++)
        if (*p == '/') b = p + 1;
    return b;
}

// Free whatever image is currently loaded. For an animated GIF the ctx owns the frame array
// (gif_anim_free releases every frame + the array); for a static image `pixels` is owned directly.
// The test pattern set up by imageview_create_ctx is a static (owned) buffer and frees the same way.
static void imageview_free_image(imageview_win_t* iv) {
    if (iv->anim.frames) {
        gif_anim_free(&iv->anim);       // zeroes anim.frames / anim.nframes
        iv->pixels = NULL;              // was aliasing a frame, not separately owned
    } else if (iv->pixels) {
        kfree(iv->pixels);
        iv->pixels = NULL;
    }
    iv->cur_frame = 0;
    iv->anim_ms = 0;
    iv->loops_done = 0;
}

int imageview_open_file(imageview_win_t* iv, const char* path) {
    if (!iv || !path || !path[0]) return -1;

    int fd = vfs_open(path, 0, 0);
    if (fd < 0) {
        snprintf(iv->status, sizeof(iv->status), "cannot open %s", imageview_basename(path));
        return -1;
    }
    uint32_t size = vfs_fsize(fd);
    uint8_t* data = vfs_fdata(fd);
    if (!data || size < 8) {
        vfs_close(fd);
        snprintf(iv->status, sizeof(iv->status), "empty/short: %s", imageview_basename(path));
        return -1;
    }

    // Dispatch by magic bytes and decode. Decode BEFORE vfs_close — `data` is only valid until the
    // fd is closed — and the decoders allocate their own output pixels, independent of `data`.
    image_t    im; im.pixels = NULL; im.width = 0; im.height = 0;
    gif_anim_t ga; memset_asm(&ga, 0, sizeof(ga));
    int animated = 0, rc = -1;
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        rc = png_decode(data, size, &im);
    } else if (data[0] == 'B' && data[1] == 'M') {
        rc = bmp_decode(data, size, &im);
    } else if (data[0] == 0xFF && data[1] == 0xD8) {
        rc = jpeg_decode(data, size, &im);
    } else if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        rc = gif_decode_anim(data, size, &ga);
        animated = 1;
    }
    vfs_close(fd);

    if (animated) {
        if (rc != 0 || ga.nframes < 1 || !ga.frames) {
            if (ga.frames) gif_anim_free(&ga);
            snprintf(iv->status, sizeof(iv->status), "decode failed: %s", imageview_basename(path));
            return -1;
        }
    } else if (rc != 0 || !im.pixels) {
        snprintf(iv->status, sizeof(iv->status), "unsupported/corrupt: %s", imageview_basename(path));
        return -1;
    }

    // Success — replace the previous image and reset the view.
    imageview_free_image(iv);
    strncpy(iv->filename, imageview_basename(path), sizeof(iv->filename) - 1);
    iv->filename[sizeof(iv->filename) - 1] = 0;
    iv->offset_x = 0;
    iv->offset_y = 0;
    iv->zoom = 1.0f;

    if (animated) {
        iv->anim = ga;                          // take ownership of the frame array
        iv->img_w = ga.width;
        iv->img_h = ga.height;
        iv->pixels = ga.frames[0].pixels;       // display frame 0 (owned by iv->anim)
        if (ga.nframes > 1)
            snprintf(iv->status, sizeof(iv->status), "%ux%u GIF, %d frames", ga.width, ga.height, ga.nframes);
        else
            snprintf(iv->status, sizeof(iv->status), "%ux%u GIF", ga.width, ga.height);
    } else {
        iv->anim.frames = NULL;
        iv->anim.nframes = 0;
        iv->pixels = im.pixels;                 // owned
        iv->img_w = im.width;
        iv->img_h = im.height;
        snprintf(iv->status, sizeof(iv->status), "%ux%u", im.width, im.height);
    }
    return 0;
}

// ~30fps compositor tick: advance an animated GIF once the current frame's delay has elapsed, and
// repoint `pixels` at the new frame. A finite NETSCAPE loop count freezes the animation on its last
// frame once it has played that many times. Returns 1 only when the visible frame actually flips, so
// a static image (or a finished animation) never forces a repaint.
int imageview_win_tick(window_t* win) {
    imageview_win_t* iv = (imageview_win_t*)win->reserved;
    if (!iv || !iv->anim.frames || iv->anim.nframes < 2) return 0;                 // static / single frame
    if (iv->anim.loop_count != 0 && iv->loops_done >= iv->anim.loop_count) return 0; // done looping: frozen

    iv->anim_ms += 33;   // one compositor tick (~33 ms, matching Selene's SELENE_TICK_MS)
    uint32_t need = (uint32_t)iv->anim.frames[iv->cur_frame].delay_cs * 10;         // centiseconds -> ms
    if (iv->anim_ms < need) return 0;

    iv->anim_ms = 0;
    if (iv->cur_frame + 1 >= iv->anim.nframes) {                                    // finishing a loop
        if (iv->anim.loop_count != 0 && ++iv->loops_done >= iv->anim.loop_count)
            return 0;                                                              // last loop: stay on final frame
        iv->cur_frame = 0;
    } else {
        iv->cur_frame++;
    }
    iv->pixels = iv->anim.frames[iv->cur_frame].pixels;
    return 1;
}

// Free the decoded image when the window closes (window_destroy then frees the ctx struct itself).
void imageview_win_close(window_t* win) {
    imageview_win_t* iv = (imageview_win_t*)win->reserved;
    if (!iv) return;
    imageview_free_image(iv);
}
