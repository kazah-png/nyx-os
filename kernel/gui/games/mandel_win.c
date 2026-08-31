// ============================================================
// mandel_win.c - Nyx Fractal: a fixed-point Mandelbrot perf demo (v6.4.66)
// ============================================================
// A P4 rendering-performance-testing window — a sibling to the Nyx Voxels testbed
// and the /fire //matrix //lava effects, and the "fractals (Mandelbrot/Julia)"
// item from the user backlog. It renders the Mandelbrot set with integer Q8.24
// fixed-point complex arithmetic (the kernel is built -mno-sse, so no floats) and
// AUTO-ZOOMS toward a fixed interesting point, ping-ponging in and out, so the
// render load rises and falls on its own. A benchmark HUD reports the measured
// per-frame render time, the derived render FPS, the current iteration cap, and
// the zoom factor: the point is to MEASURE the software renderer under a
// continuously varying load (deeper zoom -> higher iteration cap -> more work),
// which feeds P4. On-brand: a purple Nyx palette, interior a near-black violet.
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "mandel_win.h"
#include "../../drivers/video/font.h"

#define MFX     24                       // fixed-point fractional bits (Q8.24)
#define MONE    ((int64_t)1 << MFX)      // 1.0 in fixed point
#define MPAL_N  256                      // cyclic palette size (power of two -> wraps cleanly)

#define M_LMAX  170                      // max zoom level (ping-pong endpoint)
#define M_ITMIN 48                       // iteration cap at zoom 0
#define M_ITMAX 220                      // iteration cap at max zoom (< 256, fits uint8)

typedef struct {
    uint8_t  iter[MANDEL_ROWS][MANDEL_COLS];  // iteration count per cell (>= cap => interior)
    uint32_t pal[MPAL_N];                     // purple cyclic palette (fb_rgb is a runtime fn)
    int      zlevel;                          // current zoom level 0..M_LMAX
    int      zdir;                            // zoom direction: +1 zooming in, -1 zooming out
    int      cap;                             // current iteration cap (also shown in the HUD)
    uint32_t zoomx;                           // current zoom factor vs. the base view (HUD)
    uint32_t last_ft_ms;                      // last measured per-frame render time (ms)
    uint32_t last_fps;                        // derived render FPS
} mandel_ctx_t;

void* mandel_create_ctx(void) {
    mandel_ctx_t* c = (mandel_ctx_t*)kmalloc(sizeof(mandel_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->zlevel = 0;
    c->zdir   = 1;
    c->cap    = M_ITMIN;
    c->zoomx  = 1;
    // A purple Nyx palette: a triangle ramp (dark violet -> lavender-magenta ->
    // dark violet) so it tiles seamlessly as the iteration index cycles. Green is
    // kept low throughout so the bands stay in the purple family, never green/white.
    for (int i = 0; i < MPAL_N; i++) {
        int lvl = (i < 128) ? i * 2 : (255 - i) * 2;   // 0..254..0
        int r = 50 + (lvl * 180) / 255;                // 50..230
        int g = 8  + (lvl * 60)  / 255;                // 8..68  (low -> purple)
        int b = 80 + (lvl * 175) / 255;                // 80..255
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    return c;
}

void mandel_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    mandel_ctx_t* c = (mandel_ctx_t*)win->reserved;
    if (!c) return;
    uint32_t interior = fb_rgb(8, 6, 16);              // near-black violet (Nyx identity)
    for (int y = 0; y < MANDEL_ROWS; y++)
        for (int x = 0; x < MANDEL_COLS; x++) {
            int it = c->iter[y][x];
            uint32_t col = (it >= c->cap) ? interior : c->pal[(it * 8) & (MPAL_N - 1)];
            fb_fill_rect(cx + x * MANDEL_CELL, cy + y * MANDEL_CELL, MANDEL_CELL, MANDEL_CELL, col);
        }

    // Benchmark HUD (top-right): the whole reason this window exists — a live read
    // of the software renderer under the current (auto-zooming) load.
    int hw = 128, hh = 90;
    int hx = cx + (int)MANDEL_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6, "Nyx Fractal", fb_rgb(230, 210, 250));
    if (c->last_ft_ms) snprintf(line, sizeof(line), "frame %u ms", c->last_ft_ms);
    else               snprintf(line, sizeof(line), "frame <1 ms");
    font_draw_string_trans(hx + 8, hy + 22, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "render FPS %u", c->last_fps);
    font_draw_string_trans(hx + 8, hy + 38, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "iter %d", c->cap);
    font_draw_string_trans(hx + 8, hy + 54, line, fb_rgb(182, 176, 208));
    snprintf(line, sizeof(line), "zoom %ux", c->zoomx);
    font_draw_string_trans(hx + 8, hy + 70, line, fb_rgb(182, 176, 208));
}

// Animate: advance the ping-pong auto-zoom one step, re-render the whole set for
// the new view (timed with get_ticks() — the PIT is 1 kHz, so a tick is 1 ms), and
// always repaint. This is the load the HUD measures.
int mandel_win_tick(window_t* win) {
    mandel_ctx_t* c = (mandel_ctx_t*)win->reserved;
    if (!c) return 0;

    // Ping-pong the zoom level so the render load rises then falls, forever.
    c->zlevel += c->zdir;
    if (c->zlevel >= M_LMAX)   { c->zlevel = M_LMAX; c->zdir = -1; }
    else if (c->zlevel <= 0)   { c->zlevel = 0;      c->zdir = +1; }

    // span shrinks geometrically with the zoom level (0.96875^zlevel). Recomputed
    // from the level each frame (not accumulated) so it never drifts.
    int64_t span0 = (int64_t)3 * MONE;                 // base view width (re -2.245..0.755)
    int64_t span  = span0;
    for (int i = 0; i < c->zlevel; i++) span = span * 31 / 32;
    if (span < 1) span = 1;
    c->zoomx = (uint32_t)(span0 / span);

    int cap = M_ITMIN + (c->zlevel * (M_ITMAX - M_ITMIN)) / M_LMAX;   // deeper => more iters
    if (cap > 255) cap = 255;
    c->cap = cap;

    // Zoom toward the classic "seahorse valley" point (-0.745, 0.113); integer
    // fixed-point constants (no float literals in the source).
    int64_t cre = -((int64_t)745 * MONE) / 1000;
    int64_t cim =  ((int64_t)113 * MONE) / 1000;
    int64_t pix = span / MANDEL_COLS;                  // complex units per cell (square pixels)
    if (pix < 1) pix = 1;
    int64_t re0 = cre - (int64_t)(MANDEL_COLS / 2) * pix;
    int64_t im0 = cim - (int64_t)(MANDEL_ROWS / 2) * pix;
    int64_t esc = (int64_t)4 * MONE;                   // escape radius squared = 4.0

    uint32_t t0 = get_ticks();
    for (int y = 0; y < MANDEL_ROWS; y++) {
        int64_t ci = im0 + (int64_t)y * pix;
        for (int x = 0; x < MANDEL_COLS; x++) {
            int64_t cr = re0 + (int64_t)x * pix;
            int64_t zr = 0, zi = 0;
            int it = 0;
            for (; it < cap; it++) {
                int64_t zr2 = (zr * zr) >> MFX;
                int64_t zi2 = (zi * zi) >> MFX;
                if (zr2 + zi2 > esc) break;
                int64_t zri = (zr * zi) >> MFX;
                zi = 2 * zri + ci;
                zr = zr2 - zi2 + cr;
            }
            c->iter[y][x] = (uint8_t)it;
        }
    }
    uint32_t el = get_ticks() - t0;
    c->last_ft_ms = el;
    c->last_fps   = el ? (1000u / el) : 999u;
    return 1;
}
