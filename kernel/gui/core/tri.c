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

int32_t tri_bary_interp(int32_t w0, int32_t w1, int32_t w2, int32_t area,
                        int32_t a0, int32_t a1, int32_t a2) {
    if (area == 0) return 0;
    // 64-bit accumulate: weights and attributes are each up to ~fb-size, their product can
    // exceed 32 bits. Dividing by the signed area handles either winding (weights share the
    // area's sign inside the triangle), so the result is in the attribute's own units.
    int64_t num = (int64_t)w0 * a0 + (int64_t)w1 * a1 + (int64_t)w2 * a2;
    return (int32_t)(num / area);
}

void tri_fill_flat_z(tri_putpx_t put, void* ctx, int32_t* zbuf, int fbw,
                     int minx, int miny, int maxx, int maxy,
                     int x0, int y0, int z0, int x1, int y1, int z1,
                     int x2, int y2, int z2, uint32_t color) {
    if (!put || !zbuf) return;
    int32_t area = tri_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;                       // degenerate: no area to fill
    int bx0 = imin3(x0, x1, x2), bx1 = imax3(x0, x1, x2);
    int by0 = imin3(y0, y1, y2), by1 = imax3(y0, y1, y2);
    if (bx0 < minx) bx0 = minx;
    if (by0 < miny) by0 = miny;
    if (bx1 > maxx) bx1 = maxx;
    if (by1 > maxy) by1 = maxy;
    for (int py = by0; py <= by1; py++) {
        for (int px = bx0; px <= bx1; px++) {
            int32_t w0 = tri_edge(x1, y1, x2, y2, px, py);
            int32_t w1 = tri_edge(x2, y2, x0, y0, px, py);
            int32_t w2 = tri_edge(x0, y0, x1, y1, px, py);
            int neg = (w0 < 0) || (w1 < 0) || (w2 < 0);
            int pos = (w0 > 0) || (w1 > 0) || (w2 > 0);
            if (neg && pos) continue;            // outside the triangle
            int32_t z = tri_bary_interp(w0, w1, w2, area, z0, z1, z2);
            int32_t* zc = &zbuf[py * fbw + px];
            if (z < *zc) { *zc = z; put(ctx, px, py, color); }   // nearer wins, update depth
        }
    }
}

static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

uint32_t tri_pack_rgb(int r, int g, int b) {
    return 0xFF000000u | ((uint32_t)clamp8(r) << 16) | ((uint32_t)clamp8(g) << 8) | (uint32_t)clamp8(b);
}

void tri_fill_gouraud(tri_putpx_t put, void* ctx, int minx, int miny, int maxx, int maxy,
                      int x0, int y0, int x1, int y1, int x2, int y2,
                      const uint8_t* c0, const uint8_t* c1, const uint8_t* c2) {
    if (!put || !c0 || !c1 || !c2) return;
    int32_t area = tri_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    int bx0 = imin3(x0, x1, x2), bx1 = imax3(x0, x1, x2);
    int by0 = imin3(y0, y1, y2), by1 = imax3(y0, y1, y2);
    if (bx0 < minx) bx0 = minx;
    if (by0 < miny) by0 = miny;
    if (bx1 > maxx) bx1 = maxx;
    if (by1 > maxy) by1 = maxy;
    for (int py = by0; py <= by1; py++) {
        for (int px = bx0; px <= bx1; px++) {
            int32_t w0 = tri_edge(x1, y1, x2, y2, px, py);
            int32_t w1 = tri_edge(x2, y2, x0, y0, px, py);
            int32_t w2 = tri_edge(x0, y0, x1, y1, px, py);
            int neg = (w0 < 0) || (w1 < 0) || (w2 < 0);
            int pos = (w0 > 0) || (w1 > 0) || (w2 > 0);
            if (neg && pos) continue;
            int r = tri_bary_interp(w0, w1, w2, area, c0[0], c1[0], c2[0]);
            int g = tri_bary_interp(w0, w1, w2, area, c0[1], c1[1], c2[1]);
            int b = tri_bary_interp(w0, w1, w2, area, c0[2], c1[2], c2[2]);
            put(ctx, px, py, tri_pack_rgb(r, g, b));
        }
    }
}

int32_t tri_persp_interp(int32_t we0, int32_t we1, int32_t we2,
                         int32_t wd0, int32_t wd1, int32_t wd2,
                         int32_t a0, int32_t a1, int32_t a2) {
    // Multiply through by wd0*wd1*wd2 so every term is integer: each vertex's contribution is
    // scaled by the product of the OTHER two depths, which is exactly weighting a/w by 1/w.
    int64_t p12 = (int64_t)wd1 * wd2, p02 = (int64_t)wd0 * wd2, p01 = (int64_t)wd0 * wd1;
    int64_t den = (int64_t)we0 * p12 + (int64_t)we1 * p02 + (int64_t)we2 * p01;
    if (den == 0) return 0;
    int64_t num = (int64_t)we0 * a0 * p12 + (int64_t)we1 * a1 * p02 + (int64_t)we2 * a2 * p01;
    return (int32_t)(num / den);
}

void tri_fill_tex(tri_putpx_t put, void* ctx, int minx, int miny, int maxx, int maxy,
                  int x0, int y0, int w0d, int u0, int v0,
                  int x1, int y1, int w1d, int u1, int v1,
                  int x2, int y2, int w2d, int u2, int v2,
                  tri_tex_t tex, void* texctx) {
    if (!put || !tex) return;
    int32_t area = tri_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    int bx0 = imin3(x0, x1, x2), bx1 = imax3(x0, x1, x2);
    int by0 = imin3(y0, y1, y2), by1 = imax3(y0, y1, y2);
    if (bx0 < minx) bx0 = minx;
    if (by0 < miny) by0 = miny;
    if (bx1 > maxx) bx1 = maxx;
    if (by1 > maxy) by1 = maxy;
    for (int py = by0; py <= by1; py++) {
        for (int px = bx0; px <= bx1; px++) {
            int32_t we0 = tri_edge(x1, y1, x2, y2, px, py);
            int32_t we1 = tri_edge(x2, y2, x0, y0, px, py);
            int32_t we2 = tri_edge(x0, y0, x1, y1, px, py);
            int neg = (we0 < 0) || (we1 < 0) || (we2 < 0);
            int pos = (we0 > 0) || (we1 > 0) || (we2 > 0);
            if (neg && pos) continue;
            int u = tri_persp_interp(we0, we1, we2, w0d, w1d, w2d, u0, u1, u2);
            int v = tri_persp_interp(we0, we1, we2, w0d, w1d, w2d, v0, v1, v2);
            put(ctx, px, py, tex(texctx, u, v));
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

// KAT for barycentric-Z interpolation + the z-buffer occlusion test. Records the final
// colour + depth written per pixel so exact occlusion outcomes can be asserted.
typedef struct { uint32_t col[16 * 16]; int32_t z[16 * 16]; } triz_kat_t;
static void triz_kat_put(void* c, int x, int y, uint32_t color) {
    triz_kat_t* k = (triz_kat_t*)c;
    if (x >= 0 && x < 16 && y >= 0 && y < 16) k->col[y * 16 + x] = color;
}

int triz_selftest(void) {
    // Barycentric interpolation: all weight on one vertex yields that vertex's attribute;
    // equal weights yield the average (centroid); either winding; area==0 guarded.
    if (tri_bary_interp(36, 0, 0, 36, 100, 200, 300) != 100) return 1;
    if (tri_bary_interp(0, 36, 0, 36, 100, 200, 300) != 200) return 2;
    if (tri_bary_interp(0, 0, 36, 36, 100, 200, 300) != 300) return 3;
    if (tri_bary_interp(12, 12, 12, 36, 100, 200, 300) != 200) return 4;   // centroid = 600/3
    if (tri_bary_interp(-36, 0, 0, -36, 100, 200, 300) != 100) return 5;   // reversed winding
    if (tri_bary_interp(0, 0, 0, 0, 1, 2, 3) != 0) return 6;               // degenerate guard

    // Z-buffer occlusion: a far triangle A, then a near triangle B over its lower-left
    // corner, then a far triangle C over B — B must win its pixels, C must not overwrite it.
    static triz_kat_t k;
    for (int i = 0; i < 256; i++) { k.col[i] = 0; k.z[i] = 0x7FFFFFFF; }   // clear to "far"
    tri_fill_flat_z(triz_kat_put, &k, k.z, 16, 0, 0, 15, 15, 0,0,100, 8,0,100, 0,8,100, 0xAAu); // A far
    tri_fill_flat_z(triz_kat_put, &k, k.z, 16, 0, 0, 15, 15, 0,0,10,  4,0,10,  0,4,10,  0xBBu); // B near
    tri_fill_flat_z(triz_kat_put, &k, k.z, 16, 0, 0, 15, 15, 0,0,200, 4,0,200, 0,4,200, 0xCCu); // C far
    if (k.col[0 * 16 + 0] != 0xBB) return 7;    // (0,0): B (z=10) beat A(100) and C(200)
    if (k.z[0 * 16 + 0] != 10)     return 8;    // nearest depth recorded
    if (k.col[1 * 16 + 6] != 0xAA) return 9;    // (6,1): inside A only (7<=8), outside B/C (7>4)
    if (k.z[1 * 16 + 6] != 100)    return 10;
    return 0;
}

typedef struct { uint32_t col[16 * 16]; } trigou_kat_t;
static void trigou_kat_put(void* c, int x, int y, uint32_t color) {
    trigou_kat_t* k = (trigou_kat_t*)c;
    if (x >= 0 && x < 16 && y >= 0 && y < 16) k->col[y * 16 + x] = color;
}

int trigou_selftest(void) {
    // RGB pack + clamp.
    if (tri_pack_rgb(255, 0, 0)   != 0xFFFF0000u) return 1;
    if (tri_pack_rgb(0, 255, 0)   != 0xFF00FF00u) return 2;
    if (tri_pack_rgb(0, 0, 255)   != 0xFF0000FFu) return 3;
    if (tri_pack_rgb(300, -5, 128) != 0xFFFF0080u) return 4;   // clamp hi (300->255) + lo (-5->0)

    // Gouraud: each vertex keeps its own colour; the centroid is the average of the three.
    static trigou_kat_t k;
    for (int i = 0; i < 256; i++) k.col[i] = 0;
    static const uint8_t red[3] = {255, 0, 0}, grn[3] = {0, 255, 0}, blu[3] = {0, 0, 255};
    tri_fill_gouraud(trigou_kat_put, &k, 0, 0, 15, 15, 0, 0, 9, 0, 0, 9, red, grn, blu);
    if (k.col[0 * 16 + 0] != 0xFFFF0000u) return 5;   // (0,0) vertex -> red
    if (k.col[0 * 16 + 9] != 0xFF00FF00u) return 6;   // (9,0) vertex -> green
    if (k.col[9 * 16 + 0] != 0xFF0000FFu) return 7;   // (0,9) vertex -> blue
    if (k.col[3 * 16 + 3] != 0xFF555555u) return 8;   // centroid (3,3) -> avg 85,85,85 = 0x55
    return 0;
}

int tritex_selftest(void) {
    // Vertex recovery: all edge-weight on one vertex yields that vertex's attribute.
    if (tri_persp_interp(50, 0, 0, 2, 3, 4, 111, 222, 333) != 111) return 1;
    if (tri_persp_interp(0, 50, 0, 2, 3, 4, 111, 222, 333) != 222) return 2;
    if (tri_persp_interp(0, 0, 50, 2, 3, 4, 111, 222, 333) != 333) return 3;
    // Edge midpoint (we0==we1, we2==0) with UNEQUAL depth: perspective pulls toward the near
    // vertex. v0 near (wd0=1,a0=0), v1 far (wd1=3,a1=100) -> (a0*wd1+a1*wd0)/(wd1+wd0)=25,
    // where the affine midpoint would be 50. (Independent of v2's depth/attr since we2=0.)
    if (tri_persp_interp(10, 10, 0, 1, 3, 7, 0, 100, 555) != 25) return 4;
    // Equal depths degrade to the affine average (50).
    if (tri_persp_interp(10, 10, 0, 2, 2, 7, 0, 100, 555) != 50) return 5;
    // Degenerate denominator guard.
    if (tri_persp_interp(0, 0, 0, 1, 1, 1, 5, 5, 5) != 0) return 6;
    return 0;
}



