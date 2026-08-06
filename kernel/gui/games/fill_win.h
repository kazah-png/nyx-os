#ifndef NYX_FILL_WIN_H
#define NYX_FILL_WIN_H
#include "../core/compositor.h"

// Nyx Fill - a 2D fill-rate / overdraw P4 rendering-performance-testing demo. Each
// frame paints the whole client area `passes` times over with a scrolling
// purple->lilac gradient (intentional overdraw); a benchmark HUD reports the pixels
// drawn per frame, the measured frame time, and the derived fill rate in Mpx/s. +/-
// change the overdraw factor so the pixel throughput - and the frame time - scale
// with the load. All-integer (the kernel is built -mno-sse, no floats). The pure
// 2D-fill sibling of Nyx Rotor (3D points) and Nyx Fractal (compute).
#define FILL_WIN_W 480
#define FILL_WIN_H 360

void  fill_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   fill_win_tick(window_t* win);
void  fill_win_key(window_t* win, int key);
void* fill_create_ctx(void);

#endif
