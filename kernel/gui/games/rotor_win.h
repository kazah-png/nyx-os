#ifndef NYX_ROTOR_WIN_H
#define NYX_ROTOR_WIN_H
#include "../core/compositor.h"

// Nyx Rotor - a spinning 3D point-lattice and P4 rendering-performance-testing demo.
// An NxNxN lattice of points forming a cube tumbles on two axes; each point is
// rotated (fixed-point sin/cos), perspective-projected and plotted, shaded by depth.
// A benchmark HUD reports the per-frame render time + derived FPS, and +/- change the
// lattice density (N, so the point count = N^3) to scale the load. All-integer (the
// kernel is built -mno-sse, no floats).
#define ROTOR_WIN_W 480
#define ROTOR_WIN_H 360

void  rotor_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   rotor_win_tick(window_t* win);
void  rotor_win_key(window_t* win, int key);
void* rotor_create_ctx(void);

#endif
