// ============================================================
// life_win.c - Nyx Life: Conway's Game of Life perf testbed (v6.4.118)
// ============================================================
// A P4 rendering / compute performance-testing window - the cellular-automaton sibling
// of Nyx Fill (fill-rate) and Nyx Fractal (per-pixel math). A bounded LIFE_GW x LIFE_GH
// grid steps by the classic B3/S23 rule (a cell survives on 2-3 live neighbours, a dead
// cell is born on exactly 3); each frame advances `speed` generations, times just the
// stepper, then renders live cells as CELL x CELL blocks. The HUD reports the generation
// counter, the live-cell count, the step time and the derived generations/second; + / -
// change generations-per-frame so the compute load - and the frame time - scale. The
// grid stencil is memory-bound (a different bottleneck from Nyx Fill's ALU-free fill and
// Nyx Fractal's per-pixel iteration), which is the point of a varied perf-testbed suite.
// Two-tone render: fresh births glow brighter (lilac) than survivors (deep purple).
// All-integer (the kernel is built -mno-sse, no floats).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "life_win.h"
#include "../../drivers/video/font.h"

#define LIFE_CELL     4                          // px per cell (WIN_W/H are multiples)
#define LIFE_GW       (LIFE_WIN_W / LIFE_CELL)   // 120 cells across
#define LIFE_GH       (LIFE_WIN_H / LIFE_CELL)   // 90  cells down
#define LIFE_NCELLS   (LIFE_GW * LIFE_GH)        // 10800 cells
#define LIFE_SPEED_MIN 1
#define LIFE_SPEED_MAX 32
#define LIFE_DENSITY   77                        // soup fill: 77/256 ~= 30% alive

// Cell values: 0 = dead, 1 = alive & survived last step, 2 = alive & newly born. Any
// non-zero counts as alive; the 1-vs-2 split only drives the two-tone render colour.
typedef struct {
    uint8_t  buf0[LIFE_NCELLS];
    uint8_t  buf1[LIFE_NCELLS];
    uint8_t* cur;                                // current generation (points at buf0/1)
    uint8_t* nxt;                                // scratch for the next generation
    int      speed;                              // generations advanced per frame (perf knob)
    int      paused;
    uint32_t rng;                                // LCG state for the soup seed
    uint32_t gen;                                // total generations since the seed
    int      live;                               // live-cell count after the last step
    uint32_t last_ms;                            // time for `speed` generations
    uint32_t last_gps;                           // derived generations / second
} life_ctx_t;

static uint32_t life_rand(life_ctx_t* c) {
    c->rng = c->rng * 1103515245u + 12345u;      // glibc LCG; we take the high bits
    return c->rng;
}

// Seed a fresh random soup at ~LIFE_DENSITY and reset the generation counter.
static void life_seed_soup(life_ctx_t* c, uint32_t seed) {
    c->rng = seed ? seed : 1u;
    int live = 0;
    for (int i = 0; i < LIFE_NCELLS; i++) {
        uint32_t r = (life_rand(c) >> 16) & 0xFF;
        if (r < LIFE_DENSITY) { c->cur[i] = 1; live++; }
        else                    c->cur[i] = 0;
    }
    c->live = live;
    c->gen  = 0;
}

// One B3/S23 generation over the bounded grid (out-of-grid neighbours count as dead).
// Reads cur, writes nxt, then swaps them; returns the new live-cell count.
static int life_step(life_ctx_t* c) {
    const uint8_t* cur = c->cur;
    uint8_t* nxt = c->nxt;
    int live = 0;
    for (int y = 0; y < LIFE_GH; y++) {
        int y0 = (y > 0)            ? y - 1 : 0;
        int y1 = (y < LIFE_GH - 1)  ? y + 1 : y;
        for (int x = 0; x < LIFE_GW; x++) {
            int x0 = (x > 0)           ? x - 1 : 0;
            int x1 = (x < LIFE_GW - 1) ? x + 1 : x;
            int n = 0;
            for (int yy = y0; yy <= y1; yy++) {
                const uint8_t* row = cur + yy * LIFE_GW;
                for (int xx = x0; xx <= x1; xx++)
                    if (row[xx]) n++;
            }
            int i     = y * LIFE_GW + x;
            int alive = cur[i] != 0;
            if (alive) n--;                        // the loop above counted self; drop it
            int next  = (alive && (n == 2 || n == 3)) || (!alive && n == 3);
            if (next) { nxt[i] = alive ? 1 : 2; live++; }
            else        nxt[i] = 0;
        }
    }
    c->cur = nxt;                                  // swap: nxt becomes current
    c->nxt = (uint8_t*)cur;
    c->gen++;
    return live;
}

void* life_create_ctx(void) {
    life_ctx_t* c = (life_ctx_t*)kmalloc(sizeof(life_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->cur    = c->buf0;
    c->nxt    = c->buf1;
    c->speed  = 1;
    c->paused = 0;
    life_seed_soup(c, 0xA5D3F00Du);                // deterministic first frame
    return c;
}

void life_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    life_ctx_t* c = (life_ctx_t*)win->reserved;
    if (!c) return;

    const uint32_t bg       = fb_rgb(14, 10, 22);   // deep night purple
    const uint32_t col_surv = fb_rgb(150, 110, 210); // survivors: deep lilac
    const uint32_t col_born = fb_rgb(224, 208, 250); // fresh births glow brighter

    fb_fill_rect(cx, cy, LIFE_WIN_W, LIFE_WIN_H, bg);

    const uint8_t* g = c->cur;
    for (int y = 0; y < LIFE_GH; y++)
        for (int x = 0; x < LIFE_GW; x++) {
            uint8_t v = g[y * LIFE_GW + x];
            if (!v) continue;
            fb_fill_rect(cx + x * LIFE_CELL, cy + y * LIFE_CELL, LIFE_CELL, LIFE_CELL,
                         v == 2 ? col_born : col_surv);
        }

    // Benchmark HUD (top-right), same frame style as Nyx Fill / Nyx Rotor. Drawn after
    // (and not counted in) the timed step, so the HUD cost never skews the measurement.
    int hw = 176, hh = 120, hx = cx + LIFE_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6,  "Nyx Life", fb_rgb(230, 210, 250));
    snprintf(line, sizeof(line), "gen %u", c->gen);
    font_draw_string_trans(hx + 8, hy + 24, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "cells %d", c->live);
    font_draw_string_trans(hx + 8, hy + 40, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "speed %dx/f", c->speed);
    font_draw_string_trans(hx + 8, hy + 56, line, fb_rgb(205, 198, 224));
    if      (c->paused)   snprintf(line, sizeof(line), "step paused");
    else if (c->last_ms)  snprintf(line, sizeof(line), "step %u ms", c->last_ms);
    else                  snprintf(line, sizeof(line), "step <1 ms");
    font_draw_string_trans(hx + 8, hy + 72, line, fb_rgb(182, 176, 208));
    if      (c->paused)   snprintf(line, sizeof(line), "gens/s --");
    else if (c->last_ms)  snprintf(line, sizeof(line), "gens/s %u", c->last_gps);
    else                  snprintf(line, sizeof(line), "gens/s fast");
    font_draw_string_trans(hx + 8, hy + 88, line, fb_rgb(182, 176, 208));
    font_draw_string_trans(hx + 8, hy + 104, "+/- r=seed p c", fb_rgb(150, 144, 178));
}

int life_win_tick(window_t* win) {
    life_ctx_t* c = (life_ctx_t*)win->reserved;
    if (!c || c->paused) return 0;                 // paused: nothing changed, no repaint

    uint32_t t0 = get_ticks();
    int live = c->live;
    for (int s = 0; s < c->speed; s++)
        live = life_step(c);
    uint32_t el = get_ticks() - t0;

    c->live     = live;
    c->last_ms  = el;
    c->last_gps = el ? ((uint32_t)c->speed * 1000u / el) : 0;
    return 1;                                       // stepped -> repaint
}

void life_win_key(window_t* win, int key) {
    life_ctx_t* c = (life_ctx_t*)win->reserved;
    if (!c) return;
    if      (key == '+' || key == '=') { if (c->speed < LIFE_SPEED_MAX) c->speed++; }
    else if (key == '-' || key == '_') { if (c->speed > LIFE_SPEED_MIN) c->speed--; }
    else if (key == 'r' || key == 'R') { life_seed_soup(c, get_ticks() ^ 0x9E3779B9u); }
    else if (key == 'p' || key == 'P') { c->paused = !c->paused; }
    else if (key == 'c' || key == 'C') {
        for (int i = 0; i < LIFE_NCELLS; i++) c->cur[i] = 0;
        c->live = 0; c->gen = 0;
    }
}
