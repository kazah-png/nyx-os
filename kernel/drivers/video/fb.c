#include "../../core/kernel.h"

static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_bpp = 0;
// Hardware row stride in PIXELS. Equals fb_width for the QEMU Bochs LFB (pitch = width*4), but a
// bootloader/GOP framebuffer on real hardware can have a PADDED pitch (pitch > width*4), so the
// blit to fb_hw must step rows by this, not by fb_width. Internal drawing + the back buffer stay
// width-contiguous; only the fb_hw boundary (fb_present, the debug banner) uses fb_hw_stride.
static uint32_t fb_hw_stride = 0;
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

// Display rotation (0/90/180/270 degrees clockwise), for panels physically mounted
// rotated — e.g. a portrait 8" UMPC panel that scans out a landscape LFB, so an
// unrotated desktop appears sideways. When nonzero, the WHOLE GUI renders into fb_back
// at the swapped logical size (fb_width/fb_height) and fb_present() is the single point
// that rotates it onto the hardware LFB (fb_hw_w x fb_hw_h). Enabled via the "rotate=" boot
// cmdline. Default 0 → every path below stays byte-identical to an unrotated build.
static int      fb_rot  = 0;
static uint32_t fb_hw_w = 0;   // hardware framebuffer dims (== fb_width/height when unrotated)
static uint32_t fb_hw_h = 0;

void fb_set_rotation(int deg) {
    int d = ((deg % 360) + 360) % 360;
    fb_rot = (d / 90) * 90;    // snap to 0/90/180/270
}

// Fullscreen userspace ownership (v5.9.29 — the DOOM-milestone graphics enabler).
// A program that calls SYS_FBPRESENT is driving the whole screen; while it keeps
// presenting, the compositor's fb_present() yields so the desktop doesn't overwrite
// the app's frames. Self-releasing: fb_fs_last is the tick of the last present, and
// ownership lapses FB_FS_TIMEOUT_MS after the app stops presenting (i.e. exits), at
// which point the desktop returns — no reap hook or explicit release needed.
#define FB_FS_TIMEOUT_MS 500
static volatile uint32_t fb_fs_last = 0;
int fb_fullscreen_active(void) {
    return fb_fs_last != 0 && (get_ticks() - fb_fs_last) < FB_FS_TIMEOUT_MS;
}

void fb_init(uint32_t width, uint32_t height, uint32_t bpp, void* addr) {
    fb_hw_w = width;                        // remember the true hardware dimensions
    fb_hw_h = height;
    fb_bpp = bpp;
    fb_hw_stride = width;                   // Bochs LFB: pitch == width*4 (no padding)
    fb_hw = addr;
    if (fb_rot == 90 || fb_rot == 270) {    // logical screen = the panel's upright (swapped) size
        fb_width = height; fb_height = width;
    } else {
        fb_width = width;  fb_height = height;
    }
    // A mode change invalidates any back buffer sized for the old resolution.
    if (fb_back) { kfree(fb_back); fb_back = NULL; }
    if (fb_rot != 0) {
        // Rotation REQUIRES the back buffer: all drawing goes to logical-space fb_back and
        // fb_present() rotates it. Drawing straight to fb_hw would be unrotated wrong-stride
        // garbage, so force it on (and keep it on — fb_use_lfb_direct becomes a no-op).
        fb_back_wanted = 1;
        fb_addr = addr;
        fb_enable_backbuffer();             // repoints fb_addr -> fb_back
    } else {
        fb_addr = addr;                     // default: draw straight to the LFB
        if (fb_back_wanted) fb_enable_backbuffer();   // resize the back buffer to match
    }
}

// Like fb_init, but for a bootloader/GOP framebuffer whose hardware row stride (pitch) may be
// wider than width*4 — the real-hardware / UEFI path. `stride_px` = pitch / 4.
void fb_init_ex(uint32_t width, uint32_t height, uint32_t bpp, void* addr, uint32_t stride_px) {
    fb_init(width, height, bpp, addr);
    fb_hw_stride = stride_px ? stride_px : width;
}

// Early "we have the framebuffer" signal, painted STRAIGHT to the hardware LFB before any
// compositor exists: a Nyx-purple bar with a white border across the top. On a real machine it
// proves graphics came up the instant they do — and because it steps rows by fb_hw_stride, a
// WRONG pitch shows as a visible diagonal/skew, which is itself the diagnostic.
void fb_debug_banner(void) {
    if (!fb_hw) return;
    uint32_t W = fb_hw_w ? fb_hw_w : fb_width;    // hardware space (== fb_width/height unrotated)
    uint32_t H = fb_hw_h ? fb_hw_h : fb_height;
    uint32_t bar = H < 48 ? H : 48;
    for (uint32_t y = 0; y < bar; y++) {
        uint32_t* row = (uint32_t*)fb_hw + (size_t)y * fb_hw_stride;
        uint32_t col = (y < 6 || y >= bar - 6) ? 0x00FFFFFF : 0x00825AD2;  // white border / Nyx purple
        for (uint32_t x = 0; x < W; x++) row[x] = col;
    }
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
    if (fb_fullscreen_active()) return;   // a userspace app owns the screen — don't clobber it
    if (!fb_back || !fb_hw) return;
    // Publishing only makes sense while drawing is actually GOING to the back
    // buffer. Once fb_use_lfb_direct() has repointed drawing at the hardware
    // (logout, panic), the back buffer holds nothing but a stale frame from
    // before — and a present from a straggling caller would paint it over the
    // login screen or the panic report. Tie the publish to the invariant that
    // makes it meaningful rather than trusting every caller to know.
    if (fb_addr != fb_back) return;
    if (fb_rot == 0) {
        if (fb_hw_stride == fb_width) {
            memcpy_asm(fb_hw, fb_back, (size_t)fb_width * fb_height * 4);   // contiguous fast path
        } else {
            // padded hardware rows (GOP pitch > width): publish row by row
            for (uint32_t y = 0; y < fb_height; y++)
                memcpy_asm((uint8_t*)fb_hw + (size_t)y * fb_hw_stride * 4,
                           (uint32_t*)fb_back + (size_t)y * fb_width, (size_t)fb_width * 4);
        }
        return;
    }
    // Rotated publish: scatter each logical back-buffer pixel to its hardware position.
    // fb_back is fb_width x fb_height (logical/upright); fb_hw is fb_hw_w x fb_hw_h (stride
    // fb_hw_stride). 90 = clockwise, 270 = counter-clockwise, 180 = flip.
    const uint32_t* src = (const uint32_t*)fb_back;
    uint32_t* hw = (uint32_t*)fb_hw;
    for (uint32_t ly = 0; ly < fb_height; ly++) {
        const uint32_t* srow = src + (size_t)ly * fb_width;
        for (uint32_t lx = 0; lx < fb_width; lx++) {
            uint32_t hx, hy;
            if (fb_rot == 90)       { hx = fb_hw_w - 1 - ly; hy = lx; }
            else if (fb_rot == 270) { hx = ly;               hy = fb_hw_h - 1 - lx; }
            else /* 180 */          { hx = fb_hw_w - 1 - lx; hy = fb_hw_h - 1 - ly; }
            hw[(size_t)hy * fb_hw_stride + hx] = srow[lx];
        }
    }
}

// SYS_FBINFO: hand the screen geometry to a userspace program so it can size its
// render buffer (DOOM's DG_Init reads this).
void fb_query(uint32_t* w, uint32_t* h, uint32_t* bpp) {
    if (w)   *w   = fb_width;
    if (h)   *h   = fb_height;
    if (bpp) *bpp = fb_bpp;
}

// Publish ONLY a sub-rectangle of the back buffer to hardware. The compositor uses this to
// move the cursor without blitting the whole ~3 MB screen every pointer step (a full
// fb_present was the single biggest per-frame cost). Clamps to the framebuffer; the rare
// rotated path falls back to a full present (correctness over speed).
void fb_present_rect(int x, int y, int w, int h) {
    if (fb_fullscreen_active()) return;
    if (!fb_back || !fb_hw) return;
    if (fb_addr != fb_back) return;
    if (fb_rot != 0) { fb_present(); return; }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0 || x >= (int)fb_width || y >= (int)fb_height) return;
    if (x + w > (int)fb_width)  w = (int)fb_width  - x;
    if (y + h > (int)fb_height) h = (int)fb_height - y;
    for (int row = 0; row < h; row++) {
        uint32_t fy = (uint32_t)(y + row);
        memcpy_asm((uint8_t*)fb_hw + ((size_t)fy * fb_hw_stride + (uint32_t)x) * 4,
                   (const uint32_t*)fb_back + (size_t)fy * fb_width + (uint32_t)x,
                   (size_t)w * 4);
    }
}

// SYS_FBPRESENT: blit a 32bpp source buffer (sw x sh) straight to the hardware
// framebuffer, nearest-neighbour scaled to the full screen, and take fullscreen
// ownership so the compositor yields. `src` is a KERNEL buffer — the syscall handler
// copies the user buffer in first (validated) so this never dereferences user VAs.
// Nearest-neighbour keeps it a single integer-only pass with no per-pixel division
// in the inner loop (the source x/y steps are precomputed per column/row could be,
// but the div is cheap enough here and keeps it obviously correct).
void fb_present_kbuf(const uint32_t* src, uint32_t sw, uint32_t sh) {
    if (!fb_hw || fb_bpp != 32 || !src || sw == 0 || sh == 0) return;
    uint32_t* dst = (uint32_t*)fb_hw;
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t sy = (uint32_t)((uint64_t)y * sh / fb_height);
        const uint32_t* srow = src + (uint64_t)sy * sw;
        uint32_t* drow = dst + (uint64_t)y * fb_width;
        for (uint32_t x = 0; x < fb_width; x++) {
            uint32_t sx = (uint32_t)((uint64_t)x * sw / fb_width);
            drow[x] = srow[sx];
        }
    }
    uint32_t t = get_ticks();
    fb_fs_last = t ? t : 1;                 // nonzero => fb_fullscreen_active() sees it
}

// Repoint all drawing straight at the hardware framebuffer. The panic screen
// uses this: the compositor's present loop is dead by the time we panic, so
// anything drawn to the back buffer would never be published — draw direct so
// it's on the visible screen immediately (no fb_present needed).
void fb_use_lfb_direct(void) {
    if (fb_rot != 0) return;   // rotated: keep drawing to fb_back; fb_present does the rotation
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

// ---- Rounded-bottom clip region -------------------------------------------
// When enabled, fb_fill_rect / fb_blit mask their writes to a rectangle whose two
// BOTTOM corners are rounded to radius clip_r (the top stays square — a window's
// top is rounded by its title bar instead). The compositor turns it on around a
// window's body + app-content draws so that content can't square off the rounded
// bottom the frame traces. Off by default, and the clip-off path in each
// primitive is kept byte-identical to before.
static int clip_on = 0;
static int clip_x0, clip_y0, clip_x1, clip_y1, clip_r;

void fb_set_round_clip(int x, int y, int w, int h, int r) {
    clip_on = 1;
    clip_x0 = x; clip_y0 = y;
    clip_x1 = x + w - 1; clip_y1 = y + h - 1;
    clip_r = r;
}
void fb_clear_clip(void) { clip_on = 0; }

// Allowed horizontal span [*lo, *hi) for row `py` under the active clip: empty if
// the row is outside it, narrowed by the arc inset within clip_r of the bottom.
static void clip_span(int py, int* lo, int* hi) {
    if (py < clip_y0 || py > clip_y1) { *lo = *hi = 0; return; }
    int l = clip_x0, r = clip_x1 + 1;               // [l, r)
    int from_bottom = clip_y1 - py;
    if (from_bottom < clip_r) {
        int inset = fb_corner_inset(from_bottom, clip_r);   // fb.c, declared in kernel.h
        l += inset; r -= inset;
    }
    if (l < 0) l = 0;
    if (r > (int)fb_width) r = (int)fb_width;
    *lo = l; *hi = r;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb_addr) return;
    if (x >= fb_width || y >= fb_height) return;
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_height) h = fb_height - y;

    if (fb_bpp == 32) {
        if (!clip_on) {
            uint32_t* ptr = (uint32_t*)fb_addr + y * fb_width + x;
            for (uint32_t row = 0; row < h; row++) {
                for (uint32_t col = 0; col < w; col++)
                    ptr[col] = color;
                ptr += fb_width;
            }
        } else {
            for (uint32_t row = 0; row < h; row++) {
                int py = (int)(y + row), lo, hi;
                clip_span(py, &lo, &hi);
                int a = (int)x, b = (int)(x + w);
                if (a < lo) a = lo;
                if (b > hi) b = hi;
                if (a >= b) continue;
                uint32_t* ptr = (uint32_t*)fb_addr + (uint32_t)py * fb_width + (uint32_t)a;
                for (int col = 0; col < b - a; col++)
                    ptr[col] = color;
            }
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
            if (!clip_on) {
                for (uint32_t col = 0; col < w; col++)
                    dst[col] = src32[col];
            } else {
                int lo, hi;
                clip_span((int)(dy + row), &lo, &hi);
                int a = (int)dx, b = (int)(dx + w);
                if (a < lo) a = lo;
                if (b > hi) b = hi;
                for (int c = a - (int)dx; c < b - (int)dx; c++)
                    dst[c] = src32[c];
            }
            dst += fb_width;
            src32 += src_stride;
        }
    }
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb_width, fb_height, color);
}

// Fill a rectangle with a vertical gradient: `top` at the first row, `bottom` at
// the last, linearly interpolated per row. 32bpp only; falls back to a flat
// `top` fill otherwise. Colours are the 32bpp BGRX form fb_rgb() produces, so the
// channels sit at <<16 (R), <<8 (G), <<0 (B) with the alpha byte forced opaque.
void fb_fill_vgrad(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   uint32_t top, uint32_t bottom) {
    if (fb_bpp != 32 || h == 0) { fb_fill_rect(x, y, w, h, top); return; }
    int tr = (int)((top >> 16) & 0xFF), tg = (int)((top >> 8) & 0xFF), tb = (int)(top & 0xFF);
    int dr = (int)((bottom >> 16) & 0xFF) - tr;
    int dg = (int)((bottom >>  8) & 0xFF) - tg;
    int db = (int)( bottom        & 0xFF) - tb;
    int den = (h > 1) ? (int)(h - 1) : 1;
    for (uint32_t row = 0; row < h; row++) {
        int r = tr + dr * (int)row / den;
        int g = tg + dg * (int)row / den;
        int b = tb + db * (int)row / den;
        fb_fill_rect(x, y + row, w, 1,
                     (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    }
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

// Integer square root (no libm in the kernel). Shared by the rounded-corner
// helpers below and the desktop/login circles.
uint32_t fb_isqrt(uint32_t x) {
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

// Horizontal inset of a rounded corner of radius R at vertical distance `d` from
// the rounded end (d = 0 is the very edge row, fully cut; d = R-1 is nearly
// square). Pixels nearer the corner than this inset lie outside the arc and are
// simply not painted, so whatever was drawn beneath shows through — there is no
// saved backing store to carve against, so the only clean option is to never
// touch those pixels. Callers that round only some corners (a title bar's top)
// use this directly, row by row; the two helpers below use it for all four.
int fb_corner_inset(int d, int R) {
    if (R <= 0 || d >= R) return 0;
    int dy = R - d;
    return R - (int)fb_isqrt((uint32_t)(R * R - dy * dy));
}

// Fill a rectangle with all four corners rounded to radius R.
void fb_fill_round_rect(int x, int y, int w, int h, int R, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    if (R * 2 > w) R = w / 2;
    if (R * 2 > h) R = h / 2;
    for (int row = 0; row < h; row++) {
        int d = -1;
        if (row < R)            d = row;
        else if (row >= h - R)  d = h - 1 - row;
        int in = (d >= 0) ? fb_corner_inset(d, R) : 0;
        fb_fill_rect(x + in, y + row, w - 2 * in, 1, col);
    }
}

// 1px rounded outline matching fb_fill_round_rect's shape.
void fb_stroke_round_rect(int x, int y, int w, int h, int R, uint32_t col) {
    if (w <= 0 || h <= 0) return;
    if (R * 2 > w) R = w / 2;
    if (R * 2 > h) R = h / 2;
    fb_fill_rect(x + R, y,         w - 2 * R, 1, col);
    fb_fill_rect(x + R, y + h - 1, w - 2 * R, 1, col);
    fb_fill_rect(x,         y + R, 1, h - 2 * R, col);
    fb_fill_rect(x + w - 1, y + R, 1, h - 2 * R, col);
    for (int row = 0; row < R; row++) {
        int in = fb_corner_inset(row, R);
        fb_put_pixel((uint32_t)(x + in),         (uint32_t)(y + row),         col);
        fb_put_pixel((uint32_t)(x + w - 1 - in), (uint32_t)(y + row),         col);
        fb_put_pixel((uint32_t)(x + in),         (uint32_t)(y + h - 1 - row), col);
        fb_put_pixel((uint32_t)(x + w - 1 - in), (uint32_t)(y + h - 1 - row), col);
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
