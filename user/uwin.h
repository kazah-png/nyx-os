#ifndef UWIN_H
#define UWIN_H
/* Client-side drawing helpers for ring-3 window programs (the SYS_WIN_* ABI). A
 * window app renders into its OWN w*h XRGB (0x00RRGGBB) buffer, then win_present()s
 * it. These are the shared primitives every app needs so each one stops hand-rolling
 * raw pixel loops — the first reusable piece for moving GUI apps out of the kernel
 * into ring-3 clients. Pure (no syscalls); every write is clipped to [0,w) x [0,h),
 * so a rectangle partly or wholly off-buffer is trimmed, never written out of bounds. */
#include "libc.h"

/* Fill the whole client buffer with one colour. */
static inline void uwin_fill(unsigned int* buf, int w, int h, unsigned int color) {
    for (int i = 0, n = w * h; i < n; i++) buf[i] = color;
}

/* Fill an axis-aligned rectangle, clipped to the buffer. */
static inline void uwin_fill_rect(unsigned int* buf, int w, int h,
                                  int x, int y, int rw, int rh, unsigned int color) {
    if (rw <= 0 || rh <= 0) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + rw, y1 = y + rh;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            buf[yy * w + xx] = color;
}

/* One horizontal run of `len` pixels starting at (x,y), clipped. Handy for the
 * per-row spans of a gradient background. */
static inline void uwin_hline(unsigned int* buf, int w, int h,
                              int x, int y, int len, unsigned int color) {
    if (y < 0 || y >= h || len <= 0) return;
    int x0 = x < 0 ? 0 : x, x1 = x + len;
    if (x1 > w) x1 = w;
    for (int xx = x0; xx < x1; xx++) buf[y * w + xx] = color;
}

/* Blit one 8x16 glyph bitmap `g` (16 bytes; row 0 = top, MSB = leftmost pixel) at
 * (x,y): only set bits are written, in `fg` (transparent background), clipped. */
static inline void uwin_glyph(unsigned int* buf, int w, int h, int x, int y,
                              const unsigned char* g, unsigned int fg) {
    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= h) continue;
        unsigned int bits = g[row];
        for (int col = 0; col < 8; col++)
            if ((bits >> (7 - col)) & 1u) {
                int px = x + col;
                if (px >= 0 && px < w) buf[py * w + px] = fg;
            }
    }
}

/* Draw a NUL-terminated string at (x,y), fetching each glyph from the ONE kernel
 * font via font_glyph() (8px advance per char). Returns x just past the last glyph.
 * The first client-side text primitive for ring-3 window apps. */
static inline int uwin_text(unsigned int* buf, int w, int h, int x, int y,
                            const char* s, unsigned int fg) {
    unsigned char g[16];
    for (; *s; s++, x += 8)
        if (font_glyph((unsigned char)*s, g) == 0)
            uwin_glyph(buf, w, h, x, y, g, fg);
    return x;
}
#endif
