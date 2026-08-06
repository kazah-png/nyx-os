#ifndef NYX_BLOBS_WIN_H
#define NYX_BLOBS_WIN_H
#include "../core/compositor.h"

// Nyx Blobs - a metaballs P4 rendering-performance-testing demo. N soft "blobs" drift
// and bounce around the client; each frame evaluates a scalar field (a sum of inverse-
// square falloffs from every blob) at every 2x2 cell and maps it through a purple->lilac
// ramp, so the blobs glow and MERGE where their fields overlap. A benchmark HUD reports
// the field samples per frame, the measured frame time and the derived rate in Mpx/s;
// +/- change the blob count so the per-sample cost (N divisions) - and the frame time -
// scale with the load. All-integer (the kernel is built -mno-sse, no floats). The
// organic-field sibling of Nyx Fractal (escape-time) and Nyx Fill (flat overdraw).
#define BLOBS_WIN_W 480
#define BLOBS_WIN_H 360

void  blobs_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   blobs_win_tick(window_t* win);
void  blobs_win_key(window_t* win, int key);
void* blobs_create_ctx(void);

#endif
