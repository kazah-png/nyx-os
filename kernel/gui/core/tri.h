#ifndef NYX_TRI_H
#define NYX_TRI_H
#include "../../core/kernel.h"

// Software triangle rasterizer (SM64 video backend / Nyx Voxels 3D-render study). The
// framebuffer had no triangle primitive — only fb_fill_rect / fb_fill_vgrad — so filled
// triangles were impossible. This fills them with the integer edge-function method, so it
// runs in the -mno-sse kernel with NO floats. Pixels are emitted through a callback, which
// keeps the fill core unit-testable off-screen (the KAT counts coverage on a grid; the
// compositor plots straight to the framebuffer via fb_put_pixel).
//
// P0b rung 1 (v6.4.343): flat-color fill, either winding. Later rungs add a z-buffer,
// barycentric (Gouraud) colour, perspective-correct texturing, and a 4x4 matrix pipeline.

typedef void (*tri_putpx_t)(void* ctx, int x, int y, uint32_t color);

// 2D edge function: twice the signed area of triangle (A,B,P). Its sign tells which side of
// the directed edge A->B point P lies on (0 = exactly on the line). This is the inside test.
int32_t tri_edge(int ax, int ay, int bx, int by, int px, int py);

// Fill the flat-colour triangle (x0,y0)-(x1,y1)-(x2,y2), clipped to the inclusive rect
// [minx..maxx]x[miny..maxy], emitting each covered pixel through put(ctx,x,y,color). Works
// for either winding (CW or CCW). A degenerate (zero-area) triangle covers nothing.
void tri_fill_flat(tri_putpx_t put, void* ctx, int minx, int miny, int maxx, int maxy,
                   int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);

// Barycentric interpolation of a per-vertex attribute at a pixel: given the three edge
// weights (w0,w1,w2 from tri_edge) and the triangle's total signed area, return
// (w0*a0 + w1*a1 + w2*a2) / area. Works for either winding; area==0 returns 0. Pure — the
// building block for z / colour / uv interpolation. (v6.4.344)
int32_t tri_bary_interp(int32_t w0, int32_t w1, int32_t w2, int32_t area,
                        int32_t a0, int32_t a1, int32_t a2);

// Z-buffered flat fill: like tri_fill_flat but interpolates a per-vertex depth (z0,z1,z2)
// across the triangle and writes a pixel ONLY where it is nearer (smaller z) than zbuf's
// current value, updating zbuf. `zbuf` is a caller-owned int32 array of `fbw`*height entries
// indexed [y*fbw + x]; the caller clears it to a far value (e.g. INT32_MAX) before a frame.
// This gives correct PER-PIXEL occlusion (two triangles can interpenetrate). (v6.4.344)
void tri_fill_flat_z(tri_putpx_t put, void* ctx, int32_t* zbuf, int fbw,
                     int minx, int miny, int maxx, int maxy,
                     int x0, int y0, int z0, int x1, int y1, int z1,
                     int x2, int y2, int z2, uint32_t color);

// Pack clamped R,G,B (each clamped to [0,255]) into the framebuffer's 32bpp word, matching
// fb_rgb: 0xFF<<24 | r<<16 | g<<8 | b. (v6.4.345)
uint32_t tri_pack_rgb(int r, int g, int b);

// Gouraud fill: interpolate a per-vertex RGB colour (each c* is a 3-byte {r,g,b}) smoothly
// across the triangle with tri_bary_interp per channel, packing + emitting each pixel. Same
// clip + either-winding rules as tri_fill_flat; degenerate covers nothing. (v6.4.345)
void tri_fill_gouraud(tri_putpx_t put, void* ctx, int minx, int miny, int maxx, int maxy,
                      int x0, int y0, int x1, int y1, int x2, int y2,
                      const uint8_t* c0, const uint8_t* c1, const uint8_t* c2);

int tri_selftest(void);      // KAT for the flat rasterizer core
int triz_selftest(void);     // KAT for barycentric-Z interpolation + the z-buffer test
int trigou_selftest(void);   // KAT for RGB packing + Gouraud vertex-colour interpolation

#endif
