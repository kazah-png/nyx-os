#ifndef NYX_LIFE_WIN_H
#define NYX_LIFE_WIN_H
#include "../core/compositor.h"

// Nyx Life - Conway's Game of Life as a P4 rendering / compute performance testbed.
// A bounded grid of cells steps by the classic B3/S23 rule; the benchmark HUD reports
// the live-cell count, the generation counter, the per-frame step time and the derived
// generations/second. + / - change how many generations advance per frame (the perf
// knob - a memory-bound grid stencil, the compute-bound sibling of Nyx Fill's fill-rate
// loop and Nyx Fractal's per-pixel math). 'r' reseeds a fresh random soup, 'p' pauses,
// 'c' clears. Two-tone purple render (fresh births glow brighter than survivors).
// All-integer (the kernel is built -mno-sse, no floats).
#define LIFE_WIN_W 480
#define LIFE_WIN_H 360

void  life_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   life_win_tick(window_t* win);
void  life_win_key(window_t* win, int key);
void* life_create_ctx(void);

#endif
