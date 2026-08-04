// ============================================================
// julia_win.c - Nyx Julia: a fixed-point Julia-set perf demo (v6.4.74)
// ============================================================
// A P4 rendering-performance-testing window, sibling to Nyx Fractal (mandel_win.c).
// It renders the Julia set with integer Q8.24 fixed-point complex arithmetic (the
// kernel is built -mno-sse, so no floats). For a Julia set the constant c is fixed
// and z0 = the pixel; here c ANIMATES around a circle of radius ~0.7885 (the classic
// Julia-morph path), so the set continuously changes shape and the render load rises
// and falls on its own — connected sets (c inside the Mandelbrot) fill more interior
// pixels to the iteration cap and render slower. A benchmark HUD reports the measured
// per-frame render time, the derived render FPS, the iteration cap, and the live c.
// On-brand: a purple Nyx palette, interior a near-black violet.
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "julia_win.h"
#include "../../drivers/video/font.h"

#define JFX     24                       // fixed-point fractional bits (Q8.24)
#define JONE    ((int64_t)1 << JFX)      // 1.0 in fixed point
#define JPAL_N  256                      // cyclic palette size
#define J_ITER  128                      // iteration cap (fixed — the view doesn't zoom)

typedef struct {
    uint8_t  iter[JULIA_ROWS][JULIA_COLS];   // iteration count per cell (>= cap => interior)
    uint32_t pal[JPAL_N];                    // purple cyclic palette (fb_rgb is a runtime fn)
    int      t;                              // animation phase 0..255 (c orbits once per 256 ticks)
    int64_t  cr, ci;                         // current c = (cr, ci) (also shown in the HUD)
    uint32_t last_ft_ms;                     // last measured per-frame render time (ms)
    uint32_t last_fps;                       // derived render FPS
} julia_ctx_t;

// Parabolic integer sine: phase a in [0,256) -> ~sin scaled to [-1024,1024]. The first
// half [0,128) is a downward parabola peaking +1024 at 64; the second half is its
// negative mirror. Good enough to drive c smoothly around a circle, no floats.
static int jsin(int a) {
    a &= 255;
    int neg = 0;
    if (a >= 128) { a -= 128; neg = 1; }
    int y = 4 * a * (128 - a) * 1024 / (128 * 128);   // 0 at the ends, 1024 at a=64
    if (y > 1024) y = 1024;
    return neg ? -y : y;
}
static int jcos(int a) { return jsin(a + 64); }

void* julia_create_ctx(void) {
    julia_ctx_t* c = (julia_ctx_t*)kmalloc(sizeof(julia_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->t = 0;
    // The same purple Nyx palette as Nyx Fractal: a triangle ramp kept in the violet
    // family (green low), so the iteration bands read as shades of purple.
    for (int i = 0; i < JPAL_N; i++) {
        int lvl = (i < 128) ? i * 2 : (255 - i) * 2;   // 0..254..0
        int r = 50 + (lvl * 180) / 255;
        int g = 8  + (lvl * 60)  / 255;
        int b = 80 + (lvl * 175) / 255;
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    return c;
}

void julia_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    julia_ctx_t* c = (julia_ctx_t*)win->reserved;
    if (!c) return;
    uint32_t interior = fb_rgb(8, 6, 16);              // near-black violet (Nyx identity)
    for (int y = 0; y < JULIA_ROWS; y++)
        for (int x = 0; x < JULIA_COLS; x++) {
            int it = c->iter[y][x];
            uint32_t col = (it >= J_ITER) ? interior : c->pal[(it * 8) & (JPAL_N - 1)];
            fb_fill_rect(cx + x * JULIA_CELL, cy + y * JULIA_CELL, JULIA_CELL, JULIA_CELL, col);
        }

    // Benchmark HUD (top-right): a live read of the renderer under the morphing load.
    int hw = 128, hh = 90;
    int hx = cx + (int)JULIA_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6, "Nyx Julia", fb_rgb(230, 210, 250));
    if (c->last_ft_ms) snprintf(line, sizeof(line), "frame %u ms", c->last_ft_ms);
    else               snprintf(line, sizeof(line), "frame <1 ms");
    font_draw_string_trans(hx + 8, hy + 22, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "render FPS %u", c->last_fps);
    font_draw_string_trans(hx + 8, hy + 38, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "iter %d", J_ITER);
    font_draw_string_trans(hx + 8, hy + 54, line, fb_rgb(182, 176, 208));
    // c to 3 decimals, via integer milli-units (no floats): cr,ci * 1000 / 1.0.
    int crm = (int)(c->cr * 1000 / JONE);
    int cim = (int)(c->ci * 1000 / JONE);
    snprintf(line, sizeof(line), "c %d,%d e-3", crm, cim);
    font_draw_string_trans(hx + 8, hy + 70, line, fb_rgb(182, 176, 208));
}

// Animate: advance c one step around its circle, re-render the whole Julia set for
// the new c (timed with get_ticks() — the PIT is 1 kHz, so a tick is 1 ms), always
// repaint. This morphing is the load the HUD measures.
int julia_win_tick(window_t* win) {
    julia_ctx_t* c = (julia_ctx_t*)win->reserved;
    if (!c) return 0;

    c->t = (c->t + 1) & 255;                           // one full orbit per 256 ticks
    int64_t R = (int64_t)7885 * JONE / 10000;          // radius 0.7885 (classic Julia morph)
    c->cr = R * jcos(c->t) / 1024;
    c->ci = R * jsin(c->t) / 1024;

    // Fixed view of the plane, ~[-1.6,1.6] x [-1.2,1.2] (square cells: same pixel size
    // both axes, so a 4:3 grid shows a 4:3 slice centred on the origin).
    int64_t pix = ((int64_t)32 * JONE / 10) / JULIA_COLS;   // span 3.2 across the columns
    if (pix < 1) pix = 1;
    int64_t re0 = -(int64_t)(JULIA_COLS / 2) * pix;
    int64_t im0 = -(int64_t)(JULIA_ROWS / 2) * pix;
    int64_t esc = (int64_t)4 * JONE;                        // escape radius squared = 4.0

    uint32_t t0 = get_ticks();
    for (int y = 0; y < JULIA_ROWS; y++) {
        int64_t zi0 = im0 + (int64_t)y * pix;
        for (int x = 0; x < JULIA_COLS; x++) {
            int64_t zr = re0 + (int64_t)x * pix;
            int64_t zi = zi0;
            int it = 0;
            for (; it < J_ITER; it++) {
                int64_t zr2 = (zr * zr) >> JFX;
                int64_t zi2 = (zi * zi) >> JFX;
                if (zr2 + zi2 > esc) break;
                int64_t zri = (zr * zi) >> JFX;
                zi = 2 * zri + c->ci;                       // z = z^2 + c, c fixed for this frame
                zr = zr2 - zi2 + c->cr;
            }
            c->iter[y][x] = (uint8_t)it;
        }
    }
    uint32_t el = get_ticks() - t0;
    c->last_ft_ms = el;
    c->last_fps   = el ? (1000u / el) : 999u;
    return 1;
}
