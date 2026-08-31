// ============================================================
// lava_win.c - an animated plasma / "lava" effect window (v6.4.59)
// ============================================================
// The third of the user-backlog /fire //matrix //lava effects (after Nyx Fire and
// Nyx Matrix). A demoscene plasma: the value at each cell is the sum of several
// phase-shifted sine waves (in x, y, x+y, and radial distance) plus a time offset,
// mapped through a warm cyclic palette so it flows like molten lava. Everything is
// integer — the kernel is built -mno-sse, so it uses a parabolic sine, no floats.
// Also a P4 visual perf demo (it re-fills the whole grid every frame).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "lava_win.h"
#include "../../drivers/video/font.h"

#define LAVA_PAL_N 256           // cyclic palette size (a power of two so values wrap cleanly)

typedef struct {
    uint32_t pal[LAVA_PAL_N];    // warm cyclic palette (fb_rgb is a runtime fn, so built here)
    int t;                       // animation time (advanced each tick)
} lava_ctx_t;

// Parabolic integer sine: input phase in [0,256) -> ~sin scaled to [-127,127]. The
// first half [0,128) is a downward parabola peaking +127 at 64; the second half is
// its negative mirror. Close enough to a sine for a smooth plasma, no floats.
static int lava_sin(int a) {
    a &= 255;
    int neg = 0;
    if (a >= 128) { a -= 128; neg = 1; }
    int y = (4 * a * (128 - a)) / 128;      // 0 at the ends, 128 at a=64
    if (y > 127) y = 127;
    return neg ? -y : y;
}

// A cyclic lava palette: a black -> red -> orange -> yellow -> white "heat" ramp,
// mirrored into a triangle so it tiles seamlessly as the plasma value wraps. Green
// only rises AFTER red saturates (red -> orange -> yellow), so it never looks green
// — it stays molten.
static void lava_init_pal(lava_ctx_t* c) {
    for (int i = 0; i < LAVA_PAL_N; i++) {
        int lvl = (i < 128) ? i * 2 : (255 - i) * 2;   // 0..254..0 triangle (heat)
        int r, g, b;
        if (lvl < 85)       { r = lvl * 3;        g = 0;              b = 0; }            // black -> red
        else if (lvl < 170) { r = 255;            g = (lvl - 85) * 3; b = 0; }            // red -> yellow
        else                { r = 255;            g = 255;            b = (lvl - 170) * 3; } // yellow -> white
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
}

void* lava_create_ctx(void) {
    lava_ctx_t* c = (lava_ctx_t*)kmalloc(sizeof(lava_ctx_t));
    if (!c) return NULL;
    c->t = 0;
    lava_init_pal(c);
    return c;
}

void lava_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    lava_ctx_t* c = (lava_ctx_t*)win->reserved;
    if (!c) return;
    int t = c->t;
    for (int y = 0; y < LAVA_ROWS; y++) {
        for (int x = 0; x < LAVA_COLS; x++) {
            int dx = x - LAVA_COLS / 2, dy = y - LAVA_ROWS / 2;
            int dist = (int)fb_isqrt((uint32_t)(dx * dx + dy * dy));
            int v = lava_sin(x * 5 + t)                 // horizontal wave
                  + lava_sin(y * 5 - t)                 // vertical wave, opposite drift
                  + lava_sin((x + y) * 3 + t)           // diagonal wave
                  + lava_sin(dist * 6 + t * 2);         // radial ripple from the centre
            int idx = ((v + 512) >> 2) & (LAVA_PAL_N - 1);   // -508..508 -> cyclic palette index
            fb_fill_rect(cx + x * LAVA_CELL, cy + y * LAVA_CELL, LAVA_CELL, LAVA_CELL, c->pal[idx]);
        }
    }
    font_draw_string_trans(cx + 8, cy + 6, "Nyx Lava", fb_rgb(255, 240, 210));
}

// Animate: advance the plasma time and always repaint.
int lava_win_tick(window_t* win) {
    lava_ctx_t* c = (lava_ctx_t*)win->reserved;
    if (!c) return 0;
    c->t += 2;
    return 1;
}
