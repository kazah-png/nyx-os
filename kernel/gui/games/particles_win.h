#ifndef NYX_PARTICLES_WIN_H
#define NYX_PARTICLES_WIN_H
#include "../core/compositor.h"

// Nyx Particles - an integer particle-fountain and P4 rendering-performance-testing
// demo. Each particle gets sub-pixel fixed-point physics (gravity + floor/wall
// bounce, then it respawns at the nozzle when it settles); a benchmark HUD reports
// the per-frame simulation time and derived FPS, and +/- change the live particle
// count so the frame-time rises and falls with the load. All-integer (the kernel is
// built -mno-sse, no floats).
#define PART_WIN_W 480
#define PART_WIN_H 360

void  particles_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   particles_win_tick(window_t* win);
void  particles_win_key(window_t* win, int key);
void* particles_create_ctx(void);

#endif
