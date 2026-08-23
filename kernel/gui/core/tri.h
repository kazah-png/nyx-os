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

int tri_selftest(void);   // KAT for the rasterizer core

#endif
