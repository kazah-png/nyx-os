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
    if (fb_back) {
        // Re-arming with a buffer that already exists. This used to `return 1`
        // and nothing else — leaving fb_addr wherever it happened to point.
        // After a logout that is the HARDWARE framebuffer (fb_use_lfb_direct
        // sent the login screen there), so the second desktop of a session
        // drew every element straight onto the visible screen — the flicker
        // double buffering exists to prevent — while fb_present kept blitting
        // this buffer, still holding the PREVIOUS user's frozen desktop, on
        // top of it every frame. Repoint, and clear it: nothing from the
        // session that just logged out may survive into the next one.
        fb_addr = fb_back;
        memset_asm(fb_back, 0, (size_t)fb_width * fb_height * 4);
        return 1;
    }
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
    // Publishing only makes sense while drawing is actually GOING to the back
    // buffer. Once fb_use_lfb_direct() has repointed drawing at the hardware
    // (logout, panic), the back buffer holds nothing but a stale frame from
    // before — and a present from a straggling caller would paint it over the
    // login screen or the panic report. Tie the publish to the invariant that
    // makes it meaningful rather than trusting every caller to know.
    if (fb_addr != fb_back) return;
    memcpy_asm(fb_hw, fb_back, (size_t)fb_width * fb_height * 4);
}

// Repoint all drawing straight at the hardware framebuffer. The panic screen
// uses this: the compositor's present loop is dead by the time we panic, so
// anything drawn to the back buffer would never be published — draw direct so
// it's on the visible screen immediately (no fb_present needed).
void fb_use_lfb_direct(void) {
    if (fb_hw) fb_addr = fb_hw;
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
        // Clip to the framebuffer. This function had NO bounds check at all, so
        // any caller blitting near the right or bottom edge — a window dragged
        // there, or Paint's 512x384 canvas — wrote straight past the end of the
        // buffer into whatever the allocator had placed after it.
        //
        // dx/dy are unsigned, so an off-screen negative coordinate arrives here
        // as a huge value and is caught by the same test.
        if (dx >= fb_width || dy >= fb_height) return;   // entirely off-screen
        uint32_t src_stride = w;                         // rows are ORIGINAL w apart
        uint32_t max_w = fb_width - dx;
        uint32_t max_h = fb_height - dy;
        if (w > max_w) w = max_w;                        // narrow the copy...
        if (h > max_h) h = max_h;                        // ...but not the stride

        uint32_t* dst = (uint32_t*)fb_addr + dy * fb_width + dx;
        uint32_t* src32 = (uint32_t*)src + sy * src_stride + sx;
        for (uint32_t row = 0; row < h; row++) {
            for (uint32_t col = 0; col < w; col++)
                dst[col] = src32[col];
            dst += fb_width;
            src32 += src_stride;
        }
    }
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb_width, fb_height, color);
}

// Darken a rectangle in place by mixing its pixels toward black. `shade` is the
// black overlay's alpha, 0..255 (0 = no change, 255 = solid black). Reads the
// current draw target, so it composites over whatever was already drawn there —
// this is what makes window drop shadows fall onto the desktop and lower windows.
// 32bpp only; a no-op otherwise (shadows are a cosmetic layer, not correctness).
// Coordinates are signed and clipped, so a shadow feathered past the top-left
// origin (x-2, y-2) is fine.
void fb_darken_rect(int x, int y, int w, int h, uint8_t shade) {
    if (!fb_addr || fb_bpp != 32 || shade == 0 || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= (int)fb_width || y >= (int)fb_height) return;
    if (x + w > (int)fb_width)  w = (int)fb_width  - x;
    if (y + h > (int)fb_height) h = (int)fb_height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t keep = 255 - shade;   // how much of the original survives, 0..255
    uint32_t* ptr = (uint32_t*)fb_addr + (uint32_t)y * fb_width + (uint32_t)x;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint32_t p = ptr[col];
            uint32_t r = ((p >> 16) & 0xFF) * keep / 255;
            uint32_t g = ((p >>  8) & 0xFF) * keep / 255;
            uint32_t b = ( p        & 0xFF) * keep / 255;
            ptr[col] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
        }
        ptr += fb_width;
    }
}

uint32_t fb_get_width(void) { return fb_width; }
uint32_t fb_get_height(void) { return fb_height; }
void* fb_get_addr(void) { return fb_addr; }

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    // VBE 32-bit LFB is BGRX (byte 0 = B, byte 1 = G, byte 2 = R, byte 3 = X)
    if (fb_bpp == 32) return (0xFF << 24) | (r << 16) | (g << 8) | b;
    return (r << 16) | (g << 8) | b;
}
