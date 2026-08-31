// ============================================================
// fill_win.c - Nyx Fill: a 2D fill-rate / overdraw perf demo (v6.4.110)
// ============================================================
// A P4 rendering-performance-testing window - the pure 2D-fill sibling of Nyx Rotor
// (3D points) and Nyx Fractal (compute). Each frame paints the whole client area
// `passes` times over with a scrolling purple->lilac gradient (intentional overdraw)
// and times just the fill; a HUD reports the pixels drawn per frame, the frame time,
// and the derived fill rate in Mpx/s. +/- change the overdraw factor so the pixel
// throughput - and the frame time - scale with the load. All-integer (the kernel is
// built -mno-sse, no floats).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "fill_win.h"
#include "../../drivers/video/font.h"

#define FILL_PAL_N 64          // power of two: the band index masks cheaply and loops
#define FILL_BAND   4          // gradient band height in px (WIN_H is a multiple of it)
#define FILL_MIN    1
#define FILL_MAX   64

typedef struct {
    int passes;                         // overdraw factor (the +/- perf knob)
    int phase;                          // scroll offset, advances each tick
    uint32_t pal[FILL_PAL_N];
    uint32_t last_ft_ms, last_fps, last_mpps;
} fill_ctx_t;

void* fill_create_ctx(void) {
    fill_ctx_t* c = (fill_ctx_t*)kmalloc(sizeof(fill_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->passes = 4;
    // A seamless purple<->lilac ramp: a triangle over the 64 entries so entry 0 and 63
    // meet, giving a smooth scrolling pulse rather than a hard seam at the wrap.
    for (int i = 0; i < FILL_PAL_N; i++) {
        int t = (i < FILL_PAL_N / 2) ? i : (FILL_PAL_N - 1 - i);   // 0..31 triangle
        int r = 24 + t * (210 - 24) / (FILL_PAL_N / 2);
        int g = 12 + t * (190 - 12) / (FILL_PAL_N / 2);
        int b = 44 + t * (250 - 44) / (FILL_PAL_N / 2);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    return c;
}

void fill_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    fill_ctx_t* c = (fill_ctx_t*)win->reserved;
    if (!c) return;

    const int nb = FILL_WIN_H / FILL_BAND;      // gradient bands (covers the whole height)

    // The timed section: overdraw the client `passes` times. This is the benchmark.
    uint32_t t0 = get_ticks();
    for (int p = 0; p < c->passes; p++)
        for (int b = 0; b < nb; b++) {
            int idx = (b + c->phase + p) & (FILL_PAL_N - 1);
            fb_fill_rect(cx, cy + b * FILL_BAND, FILL_WIN_W, FILL_BAND, c->pal[idx]);
        }
    uint32_t el = get_ticks() - t0;
    c->last_ft_ms = el;
    c->last_fps   = el ? (1000u / el) : 999u;

    // pixels drawn this frame = W * H * passes; fill rate = pixels / seconds, in Mpx/s
    // (pixels / (ms * 1000) == megapixels per second). Fits in uint32 (<= ~11M).
    uint32_t px = (uint32_t)FILL_WIN_W * (uint32_t)(nb * FILL_BAND) * (uint32_t)c->passes;
    c->last_mpps = el ? (px / (el * 1000u)) : 0;

    // Benchmark HUD (top-right), same frame style as Nyx Rotor. Drawn after (and not
    // counted in) the timed fill, so the HUD cost never skews the measurement.
    int hw = 168, hh = 108, hx = cx + FILL_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6,  "Nyx Fill", fb_rgb(230, 210, 250));
    snprintf(line, sizeof(line), "overdraw %dx", c->passes);
    font_draw_string_trans(hx + 8, hy + 24, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "px/frame %uK", px / 1000u);
    font_draw_string_trans(hx + 8, hy + 40, line, fb_rgb(205, 198, 224));
    if (el) snprintf(line, sizeof(line), "frame %u ms", el);
    else    snprintf(line, sizeof(line), "frame <1 ms");
    font_draw_string_trans(hx + 8, hy + 56, line, fb_rgb(205, 198, 224));
    if (el) snprintf(line, sizeof(line), "fill %u Mpx/s", c->last_mpps);
    else    snprintf(line, sizeof(line), "fill (sub-ms)");
    font_draw_string_trans(hx + 8, hy + 72, line, fb_rgb(182, 176, 208));
    font_draw_string_trans(hx + 8, hy + 90, "+/- overdraw", fb_rgb(150, 144, 178));
}

int fill_win_tick(window_t* win) {
    fill_ctx_t* c = (fill_ctx_t*)win->reserved;
    if (!c) return 0;
    c->phase = (c->phase + 1) & (FILL_PAL_N - 1);   // scroll the gradient
    return 1;                                        // continuous benchmark: repaint each tick
}

void fill_win_key(window_t* win, int key) {
    fill_ctx_t* c = (fill_ctx_t*)win->reserved;
    if (!c) return;
    if      (key == '+' || key == '=') { if (c->passes < FILL_MAX) c->passes++; }
    else if (key == '-' || key == '_') { if (c->passes > FILL_MIN) c->passes--; }
}
