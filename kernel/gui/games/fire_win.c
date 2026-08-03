// ============================================================
// fire_win.c - the classic "doom fire" effect as an animated window (v6.4.55)
// ============================================================
// Fabien Sanglard's doom-fire algorithm: a heat grid whose bottom row is held at
// maximum, propagated upward each frame with random cooling + horizontal drift,
// then mapped through a black->red->orange->yellow->white palette. Requested from
// the user backlog (the /fire //matrix //lava effect family). Doubles as a P4
// visual perf demo — every frame re-propagates and re-fills the whole grid, which
// stresses the software framebuffer fill. Entirely integer (kernel is -mno-sse).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "fire_win.h"
#include "../../drivers/video/font.h"

#define FIRE_LEVELS 37       // palette size: 0 = black (cold) .. 36 = white (hottest)

typedef struct {
    uint8_t  heat[FIRE_H][FIRE_W];   // heat / palette index per cell
    uint32_t pal[FIRE_LEVELS];       // fire palette (fb_rgb is a runtime fn, so built here)
    uint32_t rng;                    // xorshift RNG state
} fire_ctx_t;

// A gradient from black through the reds and oranges to yellow and white — the
// look of the original doom fire. Built at runtime because fb_rgb is a function.
static void fire_init_pal(fire_ctx_t* c) {
    for (int i = 0; i < FIRE_LEVELS; i++) {
        int r, g, b;
        if (i == 0)          { r = 7;  g = 7;  b = 7;  }          // near-black embers
        else if (i < 12)     { r = 20 + i * 20; g = i * 2;       b = 0; }        // black -> deep red
        else if (i < 26)     { r = 255;         g = (i - 11) * 18; b = 0; }      // red -> orange -> yellow
        else                 { r = 255;         g = 255;         b = (i - 25) * 22; }  // yellow -> white
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

static uint32_t fire_rand(fire_ctx_t* c) {
    uint32_t x = c->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->rng = x ? x : 0x9e3779b9u;
    return c->rng;
}

void* fire_create_ctx(void) {
    fire_ctx_t* c = (fire_ctx_t*)kmalloc(sizeof(fire_ctx_t));
    if (!c) return NULL;
    for (int y = 0; y < FIRE_H; y++)
        for (int x = 0; x < FIRE_W; x++)
            c->heat[y][x] = 0;                       // all cold
    for (int x = 0; x < FIRE_W; x++)
        c->heat[FIRE_H - 1][x] = FIRE_LEVELS - 1;    // bottom row is the flame source
    c->rng = 0x1234abcdu;
    fire_init_pal(c);
    return c;
}

// One propagation pass: for each cell, cool it a little and push it up into the
// row above with a small random horizontal drift — the whole effect in one loop.
static void fire_step(fire_ctx_t* c) {
    for (int x = 0; x < FIRE_W; x++)
        c->heat[FIRE_H - 1][x] = FIRE_LEVELS - 1;    // keep the source hot
    for (int y = FIRE_H - 1; y >= 1; y--) {
        for (int x = 0; x < FIRE_W; x++) {
            uint32_t r = fire_rand(c);
            int rnd = (int)(r % 3);                  // 0, 1, 2
            int nv  = (int)c->heat[y][x] - (rnd & 1);
            if (nv < 0) nv = 0;
            int tx = x + (rnd - 1);                  // drift -1, 0, +1
            if (tx < 0) tx = 0; else if (tx >= FIRE_W) tx = FIRE_W - 1;
            c->heat[y - 1][tx] = (uint8_t)nv;
        }
    }
}

void fire_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    fire_ctx_t* c = (fire_ctx_t*)win->reserved;
    if (!c) return;
    for (int y = 0; y < FIRE_H; y++)
        for (int x = 0; x < FIRE_W; x++)
            fb_fill_rect(cx + x * FIRE_CELL, cy + y * FIRE_CELL, FIRE_CELL, FIRE_CELL,
                         c->pal[c->heat[y][x]]);
    font_draw_string_trans(cx + 8, cy + 6, "Nyx Fire", fb_rgb(255, 240, 200));
}

// Animate: advance the fire one step every game tick and always repaint.
int fire_win_tick(window_t* win) {
    fire_ctx_t* c = (fire_ctx_t*)win->reserved;
    if (!c) return 0;
    fire_step(c);
    return 1;
}
