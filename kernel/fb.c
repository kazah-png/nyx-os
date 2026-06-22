#include "kernel.h"

static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_bpp = 0;
static void* fb_addr = NULL;

void fb_init(uint32_t width, uint32_t height, uint32_t bpp, void* addr) {
    fb_width = width;
    fb_height = height;
    fb_bpp = bpp;
    fb_addr = addr;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_addr || x >= fb_width || y >= fb_height) return;
    if (fb_bpp == 32) {
        ((uint32_t*)fb_addr)[y * fb_width + x] = color;
    } else if (fb_bpp == 24) {
        uint8_t* p = (uint8_t*)fb_addr + (y * fb_width + x) * 3;
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
    } else if (fb_bpp == 8) {
        ((uint8_t*)fb_addr)[y * fb_width + x] = (uint8_t)color;
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb_addr) return;
    if (x >= fb_width || y >= fb_height) return;
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_height) h = fb_height - y;

    if (fb_bpp == 32) {
        uint32_t* ptr = (uint32_t*)fb_addr + y * fb_width + x;
        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++)
                ptr[col] = color;
            ptr += fb_width;
        }
    } else if (fb_bpp == 8) {
        uint8_t* ptr = (uint8_t*)fb_addr + y * fb_width + x;
        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++)
                ptr[col] = (uint8_t)color;
            ptr += fb_width;
        }
    }
}

void fb_blit(const void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h,
             uint32_t dx, uint32_t dy) {
    if (!fb_addr || !src) return;
    if (fb_bpp == 32) {
        uint32_t* dst = (uint32_t*)fb_addr + dy * fb_width + dx;
        uint32_t* src32 = (uint32_t*)src + sy * w + sx;
        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++)
                dst[col] = src32[col];
            dst += fb_width;
            src32 += w;
        }
    }
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb_width, fb_height, color);
}

uint32_t fb_get_width(void) { return fb_width; }
uint32_t fb_get_height(void) { return fb_height; }
void* fb_get_addr(void) { return fb_addr; }

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (fb_bpp == 32) return (0xFF << 24) | (b << 16) | (g << 8) | r;
    return (b << 16) | (g << 8) | r;
}
