#ifndef NYX_JULIA_WIN_H
#define NYX_JULIA_WIN_H
#include "../core/compositor.h"

// Nyx Julia — a fixed-point Julia-set renderer and P4 rendering-performance-testing
// demo (a sibling to Nyx Fractal). Where the Mandelbrot fixes z0=0 and varies c per
// pixel, the Julia set fixes c and sets z0 = the pixel; here c ORBITS a circle over
// time so the whole set morphs continuously, which makes the render load rise and
// fall on its own (connected sets fill more interior => slower). A benchmark HUD
// reports the per-frame render time, derived render FPS, iteration cap, and the live
// c value. All-integer Q8.24 fixed point (the kernel is built -mno-sse, no floats).
#define JULIA_CELL   4                              // each compute cell is drawn CELL x CELL
#define JULIA_COLS   120                            // compute-grid columns
#define JULIA_ROWS   90                             // compute-grid rows
#define JULIA_WIN_W  (JULIA_COLS * JULIA_CELL)      // client width  (480)
#define JULIA_WIN_H  (JULIA_ROWS * JULIA_CELL)      // client height (360)

void  julia_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   julia_win_tick(window_t* win);
void* julia_create_ctx(void);

#endif
