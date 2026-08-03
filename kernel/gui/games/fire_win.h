#ifndef FIRE_WIN_H
#define FIRE_WIN_H
#include "../core/compositor.h"

// Nyx Fire — the classic "doom fire" effect (Fabien Sanglard's algorithm) as an
// in-kernel animated window. Requested from the user backlog alongside /matrix
// and /lava. It is also a P4 visual perf demo: every frame re-propagates and
// re-fills the whole heat grid, so it stresses the software framebuffer fill.
// All-integer (the kernel is built -mno-sse, so no floating point).
#define FIRE_W    120        // heat-grid columns
#define FIRE_H    90         // heat-grid rows
#define FIRE_CELL 4          // each cell is drawn as a CELL x CELL block
#define FIRE_WIN_W (FIRE_W * FIRE_CELL)   // client width  (480)
#define FIRE_WIN_H (FIRE_H * FIRE_CELL)   // client height (360)

void  fire_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   fire_win_tick(window_t* win);
void* fire_create_ctx(void);

#endif
