#ifndef NYX_MANDEL_WIN_H
#define NYX_MANDEL_WIN_H
#include "../core/compositor.h"

// Nyx Fractal — a fixed-point Mandelbrot renderer as an in-kernel window and a P4
// rendering-performance-testing demo (a sibling to the Nyx Voxels testbed and the
// /fire //matrix //lava effects). It AUTO-ZOOMS toward a fixed interesting point,
// ping-ponging in and out, so the render load rises and falls on its own; a
// benchmark HUD reports the measured per-frame render time, the derived render
// FPS, the current iteration cap, and the zoom factor — i.e. it MEASURES the
// software renderer under a continuously varying load. All-integer (the kernel is
// built -mno-sse, so no floating point): Q8.24 fixed-point complex arithmetic.
#define MANDEL_CELL   4                              // each compute cell is drawn CELL x CELL
#define MANDEL_COLS   120                            // compute-grid columns
#define MANDEL_ROWS   90                             // compute-grid rows
#define MANDEL_WIN_W  (MANDEL_COLS * MANDEL_CELL)    // client width  (480)
#define MANDEL_WIN_H  (MANDEL_ROWS * MANDEL_CELL)    // client height (360)

void  mandel_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   mandel_win_tick(window_t* win);
void* mandel_create_ctx(void);

#endif
