// ============================================================
// matrix_win.c - cmatrix-style green "code rain" window (v6.4.57)
// ============================================================
// The sibling of Nyx Fire (v6.4.55) in the user-backlog /fire //matrix //lava
// effect family. Each column has a falling "head" row and a fading green tail of
// glyphs behind it; columns run at independent speeds and restart above the top
// once their trail has fallen off the bottom, and glyphs flicker as they fall.
// Also a P4 visual perf demo — the whole grid is cleared and redrawn each frame.
// Entirely integer (the kernel is built -mno-sse, so no floating point).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "matrix_win.h"
#include "../../drivers/video/font.h"

#define MATRIX_TAIL 16       // lit trail length (glyphs) behind each falling head

typedef struct {
    int      y[MATRIX_COLS];    // head row of each column (negative = staggered start above the top)
    short    spd[MATRIX_COLS];  // ticks between advances (bigger = slower)
    short    cnt[MATRIX_COLS];  // tick counter toward the next advance
    unsigned char glyph[MATRIX_ROWS][MATRIX_COLS];  // the character shown in each cell
    uint32_t rng;
} matrix_ctx_t;

static uint32_t mx_rand(matrix_ctx_t* c) {
    uint32_t x = c->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->rng = x ? x : 0x9e3779b9u;
    return c->rng;
}

// A random printable ASCII glyph (the font has no katakana, so this is our rain).
static unsigned char mx_glyph(matrix_ctx_t* c) { return (unsigned char)(33 + (mx_rand(c) % 94)); }

void* matrix_create_ctx(void) {
    matrix_ctx_t* c = (matrix_ctx_t*)kmalloc(sizeof(matrix_ctx_t));
    if (!c) return NULL;
    c->rng = 0x51ed270bu;
    for (int x = 0; x < MATRIX_COLS; x++) {
        c->y[x]   = -(int)(mx_rand(c) % (MATRIX_ROWS * 2));   // staggered starts above the window
        c->spd[x] = (short)(1 + mx_rand(c) % 4);              // 1..4 ticks per step (varied speeds)
        c->cnt[x] = 0;
    }
    for (int r = 0; r < MATRIX_ROWS; r++)
        for (int x = 0; x < MATRIX_COLS; x++)
            c->glyph[r][x] = mx_glyph(c);
    return c;
}

static void matrix_step(matrix_ctx_t* c) {
    for (int x = 0; x < MATRIX_COLS; x++) {
        if (++c->cnt[x] < c->spd[x]) continue;
        c->cnt[x] = 0;
        c->y[x]++;
        if (c->y[x] >= 0 && c->y[x] < MATRIX_ROWS)            // fresh glyph at the new head cell
            c->glyph[c->y[x]][x] = mx_glyph(c);
        if (c->y[x] - MATRIX_TAIL > MATRIX_ROWS) {            // whole trail off the bottom -> restart
            c->y[x]   = -(int)(mx_rand(c) % MATRIX_ROWS);
            c->spd[x] = (short)(1 + mx_rand(c) % 4);
        }
        if ((mx_rand(c) & 7) == 0) {                          // occasional flicker in the column
            int rr = (int)(mx_rand(c) % MATRIX_ROWS);
            c->glyph[rr][x] = mx_glyph(c);
        }
    }
}

void matrix_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    matrix_ctx_t* c = (matrix_ctx_t*)win->reserved;
    if (!c) return;
    fb_fill_rect(cx, cy, MATRIX_WIN_W, MATRIX_WIN_H, fb_rgb(0, 0, 0));   // black backdrop
    for (int x = 0; x < MATRIX_COLS; x++) {
        int head = c->y[x];
        for (int d = 0; d < MATRIX_TAIL; d++) {
            int r = head - d;
            if (r < 0 || r >= MATRIX_ROWS) continue;
            uint32_t col;
            if (d == 0) {
                col = fb_rgb(205, 255, 205);                  // bright, near-white head
            } else {
                int g = 255 - d * 14; if (g < 40) g = 40;     // green tail fading with distance
                col = fb_rgb(0, (uint8_t)g, 0);
            }
            font_draw_char(cx + x * 8, cy + r * 16, c->glyph[r][x], col, fb_rgb(0, 0, 0));
        }
    }
    font_draw_string_trans(cx + 8, cy + 6, "Nyx Matrix", fb_rgb(120, 255, 120));
}

// Animate: advance every column one game tick and always repaint.
int matrix_win_tick(window_t* win) {
    matrix_ctx_t* c = (matrix_ctx_t*)win->reserved;
    if (!c) return 0;
    matrix_step(c);
    return 1;
}
