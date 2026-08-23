#include "tri.h"

int32_t tri_edge(int ax, int ay, int bx, int by, int px, int py) {
    return (int32_t)((bx - ax) * (py - ay) - (by - ay) * (px - ax));
}

static int imin3(int a, int b, int c) { int m = a; if (b < m) m = b; if (c < m) m = c; return m; }
static int imax3(int a, int b, int c) { int m = a; if (b > m) m = b; if (c > m) m = c; return m; }

void tri_fill_flat(tri_putpx_t put, void* ctx, int minx, int miny, int maxx, int maxy,
                   int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    if (!put) return;
    // Bounding box of the triangle, clamped to the caller's clip rect.
    int bx0 = imin3(x0, x1, x2), bx1 = imax3(x0, x1, x2);
    int by0 = imin3(y0, y1, y2), by1 = imax3(y0, y1, y2);
    if (bx0 < minx) bx0 = minx;
    if (by0 < miny) by0 = miny;
    if (bx1 > maxx) bx1 = maxx;
    if (by1 > maxy) by1 = maxy;
    // A point is inside when it is on the same side of all three directed edges. Computing
    // all three edge functions and requiring they never straddle zero handles both windings
    // and treats on-edge (==0) as inside. Incremental stepping is a later optimisation.
    for (int py = by0; py <= by1; py++) {
        for (int px = bx0; px <= bx1; px++) {
            int32_t w0 = tri_edge(x1, y1, x2, y2, px, py);
            int32_t w1 = tri_edge(x2, y2, x0, y0, px, py);
            int32_t w2 = tri_edge(x0, y0, x1, y1, px, py);
            int neg = (w0 < 0) || (w1 < 0) || (w2 < 0);
            int pos = (w0 > 0) || (w1 > 0) || (w2 > 0);
            if (!(neg && pos)) put(ctx, px, py, color);   // all same sign (or on an edge)
        }
    }
}

// KAT scratch: a coverage counter + a 16x16 hit grid so exact pixels can be asserted.
typedef struct { int count; uint8_t hit[16 * 16]; } tri_kat_t;
static void tri_kat_put(void* c, int x, int y, uint32_t color) {
    (void)color;
    tri_kat_t* k = (tri_kat_t*)c;
    k->count++;
    if (x >= 0 && x < 16 && y >= 0 && y < 16) k->hit[y * 16 + x] = 1;
}

int tri_selftest(void) {
    // Edge-function sign: the directed edge (0,0)->(4,0); a point above/below/on it.
    if (tri_edge(0, 0, 4, 0, 2,  1) <= 0) return 1;   // one side positive
    if (tri_edge(0, 0, 4, 0, 2, -1) >= 0) return 2;   // the other side negative
    if (tri_edge(0, 0, 4, 0, 2,  0) != 0) return 3;   // exactly on the line -> 0

    static tri_kat_t k;
    // Right triangle (0,0),(4,0),(0,4): integer points with x>=0, y>=0, x+y<=4 -> 15 of them.
    k.count = 0; for (int i = 0; i < 256; i++) k.hit[i] = 0;
    tri_fill_flat(tri_kat_put, &k, 0, 0, 15, 15, 0, 0, 4, 0, 0, 4, 0xFFFFFFu);
    if (k.count != 15) return 4;
    if (!k.hit[0 * 16 + 0] || !k.hit[0 * 16 + 4] || !k.hit[4 * 16 + 0]) return 5;  // all 3 vertices
    if (!k.hit[1 * 16 + 1]) return 6;                 // (1,1) is inside
    if ( k.hit[3 * 16 + 3]) return 7;                 // (3,3): 3+3=6 > 4 -> outside

    // Reversed winding must give identical coverage.
    k.count = 0; for (int i = 0; i < 256; i++) k.hit[i] = 0;
    tri_fill_flat(tri_kat_put, &k, 0, 0, 15, 15, 0, 0, 0, 4, 4, 0, 0xFFFFFFu);
    if (k.count != 15) return 8;

    // Clip rect [0..2]x[0..2] keeps only the 3x3 block (all satisfy x+y<=4) -> 9 pixels.
    k.count = 0; for (int i = 0; i < 256; i++) k.hit[i] = 0;
    tri_fill_flat(tri_kat_put, &k, 0, 0, 2, 2, 0, 0, 4, 0, 0, 4, 0xFFFFFFu);
    if (k.count != 9) return 9;

    // Degenerate (collinear) triangle has zero area; a point off the line is outside, so the
    // 1x1 clip at (5,5) — well off the line through (0,0),(2,2),(4,4) — covers nothing.
    k.count = 0;
    tri_fill_flat(tri_kat_put, &k, 5, 5, 5, 5, 0, 0, 2, 2, 4, 4, 0xFFFFFFu);
    if (k.count != 0) return 10;

    return 0;
}
