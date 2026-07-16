#ifndef MINESWEEPER_WIN_H
#define MINESWEEPER_WIN_H

#include "kernel.h"
#include "compositor.h"

// Classic "beginner" board: 9x9 with 10 mines.
#define MS_COLS    9
#define MS_ROWS    9
#define MS_MINES   10
#define MS_CELL    22          // pixel size of one board cell
#define MS_MARGIN  10          // window inner margin
#define MS_HEADER  30          // top panel (mine counter / face / timer)
#define MS_GAP     6           // gap between header and board

#define MS_WIN_W   (MS_COLS * MS_CELL + 2 * MS_MARGIN)
#define MS_WIN_H   (MS_MARGIN + MS_HEADER + MS_GAP + MS_ROWS * MS_CELL + MS_MARGIN)

// Per-cell state.
#define MS_HIDDEN   0
#define MS_REVEALED 1
#define MS_FLAGGED  2

typedef struct {
    uint8_t mine[MS_ROWS][MS_COLS];    // 1 = this cell hides a mine
    uint8_t adj[MS_ROWS][MS_COLS];     // count of adjacent mines (0..8)
    uint8_t state[MS_ROWS][MS_COLS];   // MS_HIDDEN / MS_REVEALED / MS_FLAGGED
    int first_click;                   // 1 until the first reveal — mines are placed
                                       // then, so the opening click is always safe
    int game_over;                     // 0 playing, 1 won, 2 lost
    int flags_placed;
    int revealed_count;                // number of non-mine cells uncovered
    int mine_hit_r, mine_hit_c;        // the mine the player detonated (-1 = none)
    uint64_t rng;                      // splitmix64 state for mine placement
    uint32_t start_tick;               // tick at first reveal (timer origin)
    uint32_t end_tick;                 // tick the game ended (freezes the timer)
} minesweeper_win_t;

minesweeper_win_t* minesweeper_create_ctx(void);
void minesweeper_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void minesweeper_win_click(window_t* win, int mx, int my, int btn);
void minesweeper_win_key(window_t* win, int key);

#endif
