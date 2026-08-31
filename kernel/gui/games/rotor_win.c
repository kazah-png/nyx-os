// ============================================================
// rotor_win.c - Nyx Rotor: a spinning 3D point-lattice perf demo (v6.4.99)
// ============================================================
// A P4 rendering-performance-testing window, the 3D-transform sibling of the 2D
// perf demos (particles/fractal/julia). An NxNxN lattice of points forming a cube
// tumbles on two axes; each point is rotated with fixed-point (Q12) sin/cos,
// perspective-projected and plotted, shaded from dim (far) to bright lilac (near).
// A benchmark HUD reports the measured per-frame render time and derived FPS; +/-
// change N so the point count (N^3) - and the frame-time - scale with the load.
// All-integer (the kernel is built -mno-sse, no floats).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "rotor_win.h"
#include "../../drivers/video/font.h"

#define ROT_PAL_N 64
#define ROT_NMIN   2
#define ROT_NMAX  12

typedef struct {
    int n;                              // lattice size N (N^3 points); the +/- perf knob
    int ax, ay;                         // rotation angles 0..255 (tumble on X and Y)
    uint32_t pal[ROT_PAL_N];
    uint32_t last_ft_ms, last_fps;
} rotor_ctx_t;

// Fixed-point sine, phase 0..255 -> -4096..4096 (Q12), parabolic (no table, no float).
static int isin(int a) {
    a &= 255; int s = 1;
    if (a >= 128) { a -= 128; s = -1; }
    return s * (a * (128 - a));         // peak 64*64 = 4096 = 1.0
}
static int icos(int a) { return isin(a + 64); }

// Clipped DDA line, for the wireframe cube edges (kept inside the window rect).
static void rot_line(int clx, int cly, int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx), ady = (dy < 0 ? -dy : dy);
    if (ady > steps) steps = ady;
    if (steps == 0) steps = 1;
    for (int s = 0; s <= steps; s++) {
        int px = x0 + dx * s / steps, py = y0 + dy * s / steps;
        if (px < clx || px >= clx + ROTOR_WIN_W || py < cly || py >= cly + ROTOR_WIN_H) continue;
        fb_fill_rect(px, py, 1, 1, col);
    }
}

void* rotor_create_ctx(void) {
    rotor_ctx_t* c = (rotor_ctx_t*)kmalloc(sizeof(rotor_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->n = 6;                           // 216 points by default; +/- scales it
    for (int i = 0; i < ROT_PAL_N; i++) {   // depth shade: dim violet (far) -> bright lilac (near)
        int r = 60  + i * 185 / ROT_PAL_N;
        int g = 28  + i * 120 / ROT_PAL_N;
        int b = 110 + i * 140 / ROT_PAL_N;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    return c;
}

void rotor_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    rotor_ctx_t* c = (rotor_ctx_t*)win->reserved;
    if (!c) return;
    fb_fill_rect(cx, cy, ROTOR_WIN_W, ROTOR_WIN_H, fb_rgb(8, 6, 16));    // deep night bg

    uint32_t t0 = get_ticks();
    int N = c->n, half = N - 1;
    int span = (N > 1) ? (240 / (N - 1)) : 240;      // lattice always spans ~+/-120 px
    int cay = icos(c->ay), say = isin(c->ay);
    int cax = icos(c->ax), sax = isin(c->ax);
    int W2 = ROTOR_WIN_W / 2, H2 = ROTOR_WIN_H / 2;
    const int CAMZ = 680, FOV = 520;      // gentle perspective — reads as a cube, not a tunnel
    for (int i = 0; i < N; i++) {
        int x = (i * 2 - half) * span / 2;
        for (int j = 0; j < N; j++) {
            int y = (j * 2 - half) * span / 2;
            for (int k = 0; k < N; k++) {
                int z = (k * 2 - half) * span / 2;
                // rotate around Y (spin), then X (tip) - a clean 3D tumble.
                int x1 = (x * cay + z * say) >> 12;
                int z1 = (-x * say + z * cay) >> 12;
                int y2 = (y * cax - z1 * sax) >> 12;
                int z2 = (y * sax + z1 * cax) >> 12;
                int denom = z2 + CAMZ; if (denom < 40) denom = 40;
                int sx = cx + W2 + (x1 * FOV) / denom;
                int sy = cy + H2 + (y2 * FOV) / denom;
                if (sx < cx || sx >= cx + ROTOR_WIN_W - 1 || sy < cy || sy >= cy + ROTOR_WIN_H - 1) continue;
                int idx = (z2 + 220) * ROT_PAL_N / 440;     // depth -220..220 -> shade 0..63
                if (idx < 0) idx = 0;
                if (idx >= ROT_PAL_N) idx = ROT_PAL_N - 1;
                int sz = (z2 > 0) ? 3 : 2;                    // near points a touch bigger
                fb_fill_rect(sx, sy, sz, sz, c->pal[idx]);
            }
        }
    }
    uint32_t el = get_ticks() - t0;
    c->last_ft_ms = el;
    c->last_fps   = el ? (1000u / el) : 999u;

    // Wireframe cube edges over the 8 corners (+/-120 on each axis) so the shape
    // reads unmistakably as a spinning cube even at axis-aligned angles.
    int csx[8], csy[8];
    for (int ci = 0; ci < 8; ci++) {
        int x = (ci & 1) ? 120 : -120, y = (ci & 2) ? 120 : -120, z = (ci & 4) ? 120 : -120;
        int x1 = (x * cay + z * say) >> 12, z1 = (-x * say + z * cay) >> 12;
        int y2 = (y * cax - z1 * sax) >> 12, z2 = (y * sax + z1 * cax) >> 12;
        int denom = z2 + CAMZ; if (denom < 40) denom = 40;
        csx[ci] = cx + W2 + (x1 * FOV) / denom;
        csy[ci] = cy + H2 + (y2 * FOV) / denom;
    }
    static const int edge[12][2] = {
        {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7}
    };
    for (int ei = 0; ei < 12; ei++)
        rot_line(cx, cy, csx[edge[ei][0]], csy[edge[ei][0]], csx[edge[ei][1]], csy[edge[ei][1]],
                 fb_rgb(150, 120, 210));

    // Benchmark HUD (top-right): render cost under the current point count.
    int hw = 156, hh = 92, hx = cx + (int)ROTOR_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6, "Nyx Rotor", fb_rgb(230, 210, 250));
    snprintf(line, sizeof(line), "points %d", N * N * N);
    font_draw_string_trans(hx + 8, hy + 22, line, fb_rgb(205, 198, 224));
    if (c->last_ft_ms) snprintf(line, sizeof(line), "frame %u ms", c->last_ft_ms);
    else               snprintf(line, sizeof(line), "frame <1 ms");
    font_draw_string_trans(hx + 8, hy + 38, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "render FPS %u", c->last_fps);
    font_draw_string_trans(hx + 8, hy + 54, line, fb_rgb(182, 176, 208));
    font_draw_string_trans(hx + 8, hy + 70, "+/- density", fb_rgb(150, 144, 178));
}

int rotor_win_tick(window_t* win) {
    rotor_ctx_t* c = (rotor_ctx_t*)win->reserved;
    if (!c) return 0;
    c->ay = (c->ay + 2) & 255;          // spin faster around the vertical
    c->ax = (c->ax + 1) & 255;          // tip slower over the horizontal
    return 1;                            // request a repaint every game tick
}

void rotor_win_key(window_t* win, int key) {
    rotor_ctx_t* c = (rotor_ctx_t*)win->reserved;
    if (!c) return;
    if      (key == '+' || key == '=') { if (c->n < ROT_NMAX) c->n++; }
    else if (key == '-' || key == '_') { if (c->n > ROT_NMIN) c->n--; }
}
