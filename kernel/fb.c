#include "kernel.h"

static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_bpp = 0;
// fb_addr is the CURRENT draw target that every fb_* primitive (and the
// compositor's cursor code, via fb_get_addr) writes to. By default it is the
// hardware LFB (fb_hw) so early, one-shot rendering — bootsplash, the login
// screen — paints straight to the screen with no need to "present". Once the
// compositor calls fb_enable_backbuffer(), fb_addr is redirected to an
// off-screen buffer (fb_back) and the compositor blits a whole finished frame
// to fb_hw in one shot via fb_present() — this is what kills the flicker (the
// user never sees the half-drawn background between elements).
static void* fb_addr = NULL;
static void* fb_hw   = NULL;   // real hardware linear framebuffer
static void* fb_back = NULL;   // off-screen back buffer (NULL = draw direct)
static int   fb_back_wanted = 0;   // re-establish the back buffer after a mode change

int  fb_enable_backbuffer(void);   // fwd

void fb_init(uint32_t width, uint32_t height, uint32_t bpp, void* addr) {
    fb_width = width;
    fb_height = height;
    fb_bpp = bpp;
    fb_hw = addr;
    // A mode change invalidates any back buffer sized for the old resolution.
    if (fb_back) { kfree(fb_back); fb_back = NULL; }
    fb_addr = addr;                        // default: draw straight to the LFB
    if (fb_back_wanted) fb_enable_backbuffer();   // resize the back buffer to match
}

// Turn on double buffering: allocate a back buffer the size of the framebuffer
// and redirect all drawing to it. Returns 1 on success (or if already on), 0 if
// it couldn't allocate / the mode isn't a 32bpp LFB (callers then just keep
// drawing direct). After this, drawing is invisible until fb_present().
int fb_enable_backbuffer(void) {
    fb_back_wanted = 1;
    if (fb_back) return 1;
    if (!fb_hw || fb_bpp != 32) return 0;
    size_t bytes = (size_t)fb_width * fb_height * 4;
    void* buf = kmalloc(bytes);
    if (!buf) { printf("[FB] back buffer alloc failed (%u KB) — direct rendering\n",
                        (uint32_t)(bytes / 1024)); return 0; }
    fb_back = buf;
    fb_addr = fb_back;
    printf("[FB] double buffering on — back buffer %ux%u (%u KB)\n",
           fb_width, fb_height, (uint32_t)(bytes / 1024));
    return 1;
}

// Blit the finished back buffer to the hardware framebuffer in one pass. No-op
// when double buffering isn't enabled (drawing already went straight to the LFB).
void fb_present(void) {
    if (!fb_back || !fb_hw) return;
    memcpy_asm(fb_hw, fb_back, (size_t)fb_width * fb_height * 4);
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
    // VBE 32-bit LFB is BGRX (byte 0 = B, byte 1 = G, byte 2 = R, byte 3 = X)
    if (fb_bpp == 32) return (0xFF << 24) | (r << 16) | (g << 8) | b;
    return (r << 16) | (g << 8) | b;
}
