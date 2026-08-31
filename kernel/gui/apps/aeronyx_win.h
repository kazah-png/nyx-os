#ifndef NYX_AERONYX_WIN_H
#define NYX_AERONYX_WIN_H
#include "../core/compositor.h"

// Aeronyx — an ANIMATED nyxfetch. The NyxOS moon renders as a rotating, shaded 3D sphere
// drawn in ASCII (a donut.c-style relief: normal-dot-light luminance indexes a character
// ramp), spinning beside live system stats (version, uptime, memory). Inspired by
// areofyl/fetch. All fixed-point — the kernel is built -mno-sse, so there are no floats;
// the sphere point + its normal rotate with Q12 sin/cos and project with perspective.
#define AERONYX_WIN_W 560
#define AERONYX_WIN_H 380

void  aeronyx_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   aeronyx_win_tick(window_t* win);
void* aeronyx_create_ctx(void);

#endif
