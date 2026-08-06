// ============================================================
// blobs_win.c - Nyx Blobs: a metaballs perf demo (v6.4.124)
// ============================================================
// A P4 rendering-performance-testing window - the organic scalar-field sibling of Nyx
// Fractal (escape-time) and Nyx Fill (flat overdraw), and distinct from Nyx Lava's
// sine-wave plasma. N soft "blobs" drift and bounce around the client; each frame
// evaluates a metaball field - a sum of inverse-square falloffs from every blob - at
// every 2x2 cell, then maps the field through a night-purple -> lilac ramp so the blobs
// glow and MERGE where their fields overlap. The timed field loop is the benchmark; a
// HUD reports the per-frame field evaluations, the frame time and the derived rate in
// Mev/s. +/- change the blob count so the per-sample cost (N divisions) - and the frame
// time - scale with the load. All-integer (the kernel is built -mno-sse, no floats).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "blobs_win.h"
#include "../../drivers/video/font.h"

#define BLOBS_STEP     2                       // evaluate the field on a 2x2 grid
#define BLOBS_MIN      2
#define BLOBS_MAX      16
#define BLOBS_PAL_N    64
#define BLOBS_STRENGTH 4096                     // a blob's field at its centre
#define BLOBS_SPREAD   400                      // falloff denominator: bigger = softer/wider glow (so blobs merge)

typedef struct { int x, y, vx, vy; } blob_t;   // x,y,vx,vy in 1/16 px (Q4 fixed point)

typedef struct {
    int      count;                             // active blobs (the +/- perf knob)
    blob_t   blob[BLOBS_MAX];
    uint32_t pal[BLOBS_PAL_N];
    uint32_t last_ft_ms, last_fps, last_mevs;
} blobs_ctx_t;

void* blobs_create_ctx(void) {
    blobs_ctx_t* c = (blobs_ctx_t*)kmalloc(sizeof(blobs_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->count = 5;
    // A night-purple -> lilac ramp: field 0 (empty sky) is dark, a blob core is bright.
    for (int i = 0; i < BLOBS_PAL_N; i++) {
        int t = i;                              // 0..63: dark night sky -> soft lilac core
        int r = 16 + (210 - 16) * t / (BLOBS_PAL_N - 1);
        int g = 12 + (182 - 12) * t / (BLOBS_PAL_N - 1);
        int b = 26 + (248 - 26) * t / (BLOBS_PAL_N - 1);
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    // Deterministic spread of blobs with a gentle drift (fixed seed => stable first frame).
    uint32_t s = 0x51ED2C0Fu;
    for (int i = 0; i < BLOBS_MAX; i++) {
        s = s * 1103515245u + 12345u; int x  = (int)((s >> 9) % (uint32_t)BLOBS_WIN_W);
        s = s * 1103515245u + 12345u; int y  = (int)((s >> 9) % (uint32_t)BLOBS_WIN_H);
        s = s * 1103515245u + 12345u; int vx = (int)((s >> 9) % 33u) - 16;   // +/- ~1 px per tick
        s = s * 1103515245u + 12345u; int vy = (int)((s >> 9) % 33u) - 16;
        if (vx == 0) vx = 9;
        if (vy == 0) vy = -9;
        c->blob[i].x = x << 4; c->blob[i].y = y << 4; c->blob[i].vx = vx; c->blob[i].vy = vy;
    }
    return c;
}

void blobs_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    blobs_ctx_t* c = (blobs_ctx_t*)win->reserved;
    if (!c) return;

    int px_now[BLOBS_MAX], py_now[BLOBS_MAX];
    for (int k = 0; k < c->count; k++) { px_now[k] = c->blob[k].x >> 4; py_now[k] = c->blob[k].y >> 4; }

    // The timed section: evaluate the metaball field over the whole client on a 2x2 grid.
    uint32_t t0 = get_ticks();
    for (int py = 0; py < BLOBS_WIN_H; py += BLOBS_STEP) {
        for (int px = 0; px < BLOBS_WIN_W; px += BLOBS_STEP) {
            int field = 0;
            for (int k = 0; k < c->count; k++) {
                int dx = px - px_now[k], dy = py - py_now[k];
                int d2 = dx * dx + dy * dy;
                field += BLOBS_STRENGTH / (d2 / BLOBS_SPREAD + 1);
            }
            int idx = field >> 6;                       // field / 64
            if (idx > BLOBS_PAL_N - 1) idx = BLOBS_PAL_N - 1;
            fb_fill_rect(cx + px, cy + py, BLOBS_STEP, BLOBS_STEP, c->pal[idx]);
        }
    }
    uint32_t el = get_ticks() - t0;
    c->last_ft_ms = el;
    c->last_fps   = el ? (1000u / el) : 999u;

    // field evaluations this frame = cells * blobs; rate in mega-evals/second.
    uint32_t cells = (uint32_t)(BLOBS_WIN_W / BLOBS_STEP) * (uint32_t)(BLOBS_WIN_H / BLOBS_STEP);
    uint32_t evals = cells * (uint32_t)c->count;
    c->last_mevs   = el ? (evals / (el * 1000u)) : 0;

    // Benchmark HUD (top-right), same frame style as Nyx Fill / Nyx Life. Drawn after
    // (and not counted in) the timed field loop, so the HUD cost never skews the number.
    int hw = 176, hh = 110, hx = cx + BLOBS_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6,  "Nyx Blobs", fb_rgb(230, 210, 250));
    snprintf(line, sizeof(line), "blobs %d", c->count);
    font_draw_string_trans(hx + 8, hy + 24, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "evals %uK", evals / 1000u);
    font_draw_string_trans(hx + 8, hy + 40, line, fb_rgb(205, 198, 224));
    if (el) snprintf(line, sizeof(line), "frame %u ms", el);
    else    snprintf(line, sizeof(line), "frame <1 ms");
    font_draw_string_trans(hx + 8, hy + 56, line, fb_rgb(205, 198, 224));
    if (el) snprintf(line, sizeof(line), "field %u Mev/s", c->last_mevs);
    else    snprintf(line, sizeof(line), "field (sub-ms)");
    font_draw_string_trans(hx + 8, hy + 72, line, fb_rgb(182, 176, 208));
    font_draw_string_trans(hx + 8, hy + 90, "+/- blobs", fb_rgb(150, 144, 178));
}

int blobs_win_tick(window_t* win) {
    blobs_ctx_t* c = (blobs_ctx_t*)win->reserved;
    if (!c) return 0;
    const int XMAX = (BLOBS_WIN_W - 1) << 4, YMAX = (BLOBS_WIN_H - 1) << 4;
    for (int i = 0; i < BLOBS_MAX; i++) {          // advance all blobs (bounce off the walls)
        blob_t* b = &c->blob[i];
        b->x += b->vx; b->y += b->vy;
        if (b->x < 0)    { b->x = 0;    b->vx = -b->vx; }
        if (b->x > XMAX) { b->x = XMAX; b->vx = -b->vx; }
        if (b->y < 0)    { b->y = 0;    b->vy = -b->vy; }
        if (b->y > YMAX) { b->y = YMAX; b->vy = -b->vy; }
    }
    return 1;                                       // continuous benchmark: repaint each tick
}

void blobs_win_key(window_t* win, int key) {
    blobs_ctx_t* c = (blobs_ctx_t*)win->reserved;
    if (!c) return;
    if      (key == '+' || key == '=') { if (c->count < BLOBS_MAX) c->count++; }
    else if (key == '-' || key == '_') { if (c->count > BLOBS_MIN) c->count--; }
}
