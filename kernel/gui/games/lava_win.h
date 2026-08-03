#ifndef NYX_LAVA_WIN_H
#define NYX_LAVA_WIN_H
#include "../core/compositor.h"

// Nyx Lava — an animated plasma / "lava lamp" effect, the third of the user-backlog
// /fire //matrix //lava effect family (after Nyx Fire v6.4.55 and Nyx Matrix
// v6.4.57). A classic demoscene plasma: overlapping phase-shifted sine waves mapped
// through a warm cyclic palette, morphing over time. Also a P4 visual perf demo
// (it re-fills the whole grid every frame). All-integer — the kernel is built
// -mno-sse, so it uses a parabolic integer sine, no floating point.
#define LAVA_COLS 120                    // plasma grid columns
#define LAVA_ROWS 90                     // plasma grid rows
#define LAVA_CELL 4                      // each cell is drawn as a CELL x CELL block
#define LAVA_WIN_W (LAVA_COLS * LAVA_CELL)   // client width  (480)
#define LAVA_WIN_H (LAVA_ROWS * LAVA_CELL)   // client height (360)

void  lava_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   lava_win_tick(window_t* win);
void* lava_create_ctx(void);

#endif
