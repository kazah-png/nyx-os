#ifndef NYX_MATRIX_WIN_H
#define NYX_MATRIX_WIN_H
#include "../core/compositor.h"

// Nyx Matrix — cmatrix-style green "code rain" as an animated in-kernel window.
// Requested from the user backlog alongside /fire and /lava. Columns of glyphs
// fall at independent speeds, each with a bright head and a fading green tail; the
// glyphs flicker as they fall. Like Nyx Fire it is also a P4 visual perf demo (it
// clears and re-draws the whole grid every frame). All-integer (kernel -mno-sse).
#define MATRIX_COLS 60                    // columns  (font is 8 px wide)
#define MATRIX_ROWS 22                    // rows     (font is 16 px tall)
#define MATRIX_WIN_W (MATRIX_COLS * 8)    // client width  (480)
#define MATRIX_WIN_H (MATRIX_ROWS * 16)   // client height (352)

void  matrix_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
int   matrix_win_tick(window_t* win);
void* matrix_create_ctx(void);

#endif
