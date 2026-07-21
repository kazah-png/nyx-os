#include "theme.h"
#include "kernel.h"
#include "compositor.h"
#include "minesweeper_win.h"
#include "font.h"

// A tiny GUI Minesweeper. Left-click uncovers a cell, right-click plants/removes a
// flag, and left-clicking an already-uncovered number "chords" (uncovers its
// neighbours once the right number of flags surround it). Mines are placed lazily
// on the first click so the opening move can never lose. The face button in the
// header starts a new game (so does 'r' / 'n').

static inline uint64_t ms_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// splitmix64 — cheap, good-enough randomness for shuffling mines around the board.
static uint32_t ms_rng_next(minesweeper_win_t* m) {
    m->rng += 0x9E3779B97F4A7C15ULL;
    uint64_t z = m->rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (uint32_t)z;
}

static void ms_reset(minesweeper_win_t* m) {
    memset_asm(m->mine, 0, sizeof(m->mine));
    memset_asm(m->adj, 0, sizeof(m->adj));
    memset_asm(m->state, 0, sizeof(m->state));   // all MS_HIDDEN
    m->first_click = 1;
    m->game_over = 0;
    m->flags_placed = 0;
    m->revealed_count = 0;
    m->mine_hit_r = -1;
    m->mine_hit_c = -1;
    m->start_tick = 0;
    m->end_tick = 0;
    // Re-stir the RNG so each new game differs (RDTSC low bits + the 1kHz tick).
    m->rng ^= ms_rdtsc() ^ ((uint64_t)get_ticks() << 21);
}

minesweeper_win_t* minesweeper_create_ctx(void) {
    minesweeper_win_t* m = (minesweeper_win_t*)kmalloc(sizeof(minesweeper_win_t));
    if (!m) return NULL;
    memset_asm(m, 0, sizeof(minesweeper_win_t));
    m->rng = ms_rdtsc() ^ 0x243F6A8885A308D3ULL;
    ms_reset(m);
    return m;
}

// Scatter MS_MINES mines, keeping the first-clicked cell AND its 8 neighbours
// clear so the opening reveal always flood-fills a comfortable area.
static void ms_place_mines(minesweeper_win_t* m, int sr, int sc) {
    int placed = 0;
    while (placed < MS_MINES) {
        int idx = ms_rng_next(m) % (MS_ROWS * MS_COLS);
        int r = idx / MS_COLS, c = idx % MS_COLS;
        if (m->mine[r][c]) continue;
        if (r >= sr - 1 && r <= sr + 1 && c >= sc - 1 && c <= sc + 1) continue;
        m->mine[r][c] = 1;
        placed++;
    }
    for (int r = 0; r < MS_ROWS; r++) {
        for (int c = 0; c < MS_COLS; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (!dr && !dc) continue;
                    int nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= MS_ROWS || nc < 0 || nc >= MS_COLS) continue;
                    if (m->mine[nr][nc]) n++;
                }
            m->adj[r][c] = (uint8_t)n;
        }
    }
}

// Flood-fill uncovering from an empty (adj==0) cell. Iterative with an explicit
// stack; each cell is revealed exactly at enqueue time, so the stack never needs
// more than one slot per board cell.
static void ms_flood(minesweeper_win_t* m, int r, int c) {
    int sr[MS_ROWS * MS_COLS], sc[MS_ROWS * MS_COLS], sp = 0;
    m->state[r][c] = MS_REVEALED;
    m->revealed_count++;
    sr[sp] = r; sc[sp] = c; sp++;
    while (sp > 0) {
        sp--;
        int cr = sr[sp], cc = sc[sp];
        if (m->adj[cr][cc] != 0) continue;      // numbered border: don't expand past it
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                if (!dr && !dc) continue;
                int nr = cr + dr, nc = cc + dc;
                if (nr < 0 || nr >= MS_ROWS || nc < 0 || nc >= MS_COLS) continue;
                if (m->state[nr][nc] == MS_HIDDEN) {   // skips flagged + already-revealed
                    m->state[nr][nc] = MS_REVEALED;
                    m->revealed_count++;
                    sr[sp] = nr; sc[sp] = nc; sp++;
                }
            }
    }
}

static void ms_lose(minesweeper_win_t* m, int r, int c) {
    m->state[r][c] = MS_REVEALED;
    m->mine_hit_r = r;
    m->mine_hit_c = c;
    m->game_over = 2;
    m->end_tick = get_ticks();
    // Uncover every mine that the player did not correctly flag.
    for (int i = 0; i < MS_ROWS; i++)
        for (int j = 0; j < MS_COLS; j++)
            if (m->mine[i][j] && m->state[i][j] != MS_FLAGGED)
                m->state[i][j] = MS_REVEALED;
}

static void ms_check_win(minesweeper_win_t* m) {
    if (m->revealed_count == MS_ROWS * MS_COLS - MS_MINES) {
        m->game_over = 1;
        m->end_tick = get_ticks();
        // Auto-flag the remaining mines for a tidy finished board.
        m->flags_placed = MS_MINES;
        for (int i = 0; i < MS_ROWS; i++)
            for (int j = 0; j < MS_COLS; j++)
                if (m->mine[i][j]) m->state[i][j] = MS_FLAGGED;
    }
}

// Left-click action on (r,c): uncover a hidden cell, or "chord" a revealed number.
static void ms_reveal(minesweeper_win_t* m, int r, int c) {
    if (m->game_over) return;
    if (r < 0 || r >= MS_ROWS || c < 0 || c >= MS_COLS) return;

    uint8_t st = m->state[r][c];
    if (st == MS_FLAGGED) return;

    if (st == MS_REVEALED) {
        // Chord: if the number of flags around a satisfied number matches it,
        // uncover the remaining hidden neighbours in one click.
        if (m->first_click) return;
        int a = m->adj[r][c];
        if (a == 0) return;
        int flags = 0;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                if (!dr && !dc) continue;
                int nr = r + dr, nc = c + dc;
                if (nr < 0 || nr >= MS_ROWS || nc < 0 || nc >= MS_COLS) continue;
                if (m->state[nr][nc] == MS_FLAGGED) flags++;
            }
        if (flags == a) {
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (!dr && !dc) continue;
                    int nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= MS_ROWS || nc < 0 || nc >= MS_COLS) continue;
                    if (m->state[nr][nc] == MS_HIDDEN) ms_reveal(m, nr, nc);
                }
        }
        return;
    }

    // Hidden cell.
    if (m->first_click) {
        ms_place_mines(m, r, c);
        m->first_click = 0;
        m->start_tick = get_ticks();
    }
    if (m->mine[r][c]) { ms_lose(m, r, c); return; }
    ms_flood(m, r, c);
    ms_check_win(m);
}

static void ms_toggle_flag(minesweeper_win_t* m, int r, int c) {
    if (m->game_over) return;
    if (r < 0 || r >= MS_ROWS || c < 0 || c >= MS_COLS) return;
    if (m->state[r][c] == MS_REVEALED) return;
    if (m->state[r][c] == MS_HIDDEN) {
        m->state[r][c] = MS_FLAGGED;
        m->flags_placed++;
    } else {
        m->state[r][c] = MS_HIDDEN;
        m->flags_placed--;
    }
}

// ---- drawing helpers -------------------------------------------------------

static uint32_t ms_num_color(int n) {
    switch (n) {
        case 1: return fb_rgb(40, 80, 230);     // blue
        case 2: return fb_rgb(30, 140, 40);     // green
        case 3: return fb_rgb(220, 40, 40);     // red
        case 4: return fb_rgb(20, 20, 150);     // dark blue
        case 5: return fb_rgb(140, 20, 20);     // maroon
        case 6: return fb_rgb(20, 150, 150);    // teal
        case 7: return fb_rgb(20, 20, 20);      // black
        default: return fb_rgb(90, 90, 90);     // 8 = grey
    }
}

static void ms_draw_flag(int x, int y) {
    int px = x + MS_CELL / 2 + 3;          // pole x
    for (int i = 0; i < 7; i++) {          // red triangular cloth, base on the pole
        int w = 7 - i;
        fb_fill_rect(px - w, y + 5 + i, w, 1, fb_rgb(210, 40, 40));
    }
    fb_fill_rect(px, y + 5, 2, MS_CELL - 11, fb_rgb(20, 20, 20));      // pole
    fb_fill_rect(x + 5, y + MS_CELL - 6, MS_CELL - 10, 2, fb_rgb(20, 20, 20)); // base
}

static void ms_draw_mine(int x, int y) {
    int mx = x + MS_CELL / 2, my = y + MS_CELL / 2;
    uint32_t k = fb_rgb(15, 15, 15);
    fb_fill_rect(mx - 4, my - 3, 8, 6, k);      // body (plus-shaped blob)
    fb_fill_rect(mx - 3, my - 4, 6, 8, k);
    fb_fill_rect(mx - 6, my - 1, 12, 2, k);     // horizontal spike
    fb_fill_rect(mx - 1, my - 6, 2, 12, k);     // vertical spike
    fb_fill_rect(mx - 2, my - 2, 2, 2, THEME_TEXT); // glint
}

static void ms_draw_cross(int x, int y) {   // "wrong flag" marker on a loss
    uint32_t red = fb_rgb(210, 40, 40);
    for (int i = 0; i < MS_CELL - 8; i++) {
        fb_fill_rect(x + 4 + i, y + 4 + i, 2, 2, red);
        fb_fill_rect(x + MS_CELL - 6 - i, y + 4 + i, 2, 2, red);
    }
}

static void ms_draw_cell(minesweeper_win_t* m, int r, int c, int x, int y) {
    uint8_t st = m->state[r][c];
    if (st == MS_HIDDEN || st == MS_FLAGGED) {
        fb_fill_rect(x, y, MS_CELL, MS_CELL, fb_rgb(120, 126, 140));
        fb_fill_rect(x, y, MS_CELL, 2, fb_rgb(175, 180, 195));                 // top bevel
        fb_fill_rect(x, y, 2, MS_CELL, fb_rgb(175, 180, 195));                 // left bevel
        fb_fill_rect(x, y + MS_CELL - 2, MS_CELL, 2, fb_rgb(70, 74, 84));      // bottom bevel
        fb_fill_rect(x + MS_CELL - 2, y, 2, MS_CELL, fb_rgb(70, 74, 84));      // right bevel
        if (st == MS_FLAGGED) {
            ms_draw_flag(x, y);
            if (m->game_over == 2 && !m->mine[r][c]) ms_draw_cross(x, y);      // flagged a safe cell
        }
        return;
    }

    // Revealed (sunken, flat).
    uint32_t bg = fb_rgb(180, 184, 196);
    if (m->mine[r][c] && r == m->mine_hit_r && c == m->mine_hit_c)
        bg = fb_rgb(220, 60, 60);        // the mine that ended the game
    fb_fill_rect(x, y, MS_CELL, MS_CELL, bg);
    fb_fill_rect(x, y, MS_CELL, 1, fb_rgb(120, 124, 134));
    fb_fill_rect(x, y, 1, MS_CELL, fb_rgb(120, 124, 134));

    if (m->mine[r][c]) {
        ms_draw_mine(x, y);
    } else if (m->adj[r][c] > 0) {
        char d[2] = { (char)('0' + m->adj[r][c]), 0 };
        font_draw_string(x + (MS_CELL - FONT_WIDTH) / 2, y + (MS_CELL - FONT_HEIGHT) / 2,
                         d, ms_num_color(m->adj[r][c]), bg);
    }
}

static void ms_draw_face(int fx, int fy, int fs, int over) {
    uint32_t k = fb_rgb(20, 20, 20);
    // raised grey button
    fb_fill_rect(fx, fy, fs, fs, fb_rgb(120, 126, 140));
    fb_fill_rect(fx, fy, fs, 2, fb_rgb(175, 180, 195));
    fb_fill_rect(fx, fy, 2, fs, fb_rgb(175, 180, 195));
    fb_fill_rect(fx, fy + fs - 2, fs, 2, fb_rgb(70, 74, 84));
    fb_fill_rect(fx + fs - 2, fy, 2, fs, fb_rgb(70, 74, 84));
    // yellow head
    fb_fill_rect(fx + 4, fy + 4, fs - 8, fs - 8, fb_rgb(240, 220, 60));

    if (over == 1) {
        // "cool" win face: sunglasses bar + smile
        fb_fill_rect(fx + 6, fy + 9, fs - 12, 3, k);
        fb_fill_rect(fx + 8, fy + fs - 9, fs - 16, 2, k);
        fb_fill_rect(fx + 7, fy + fs - 11, 2, 2, k);
        fb_fill_rect(fx + fs - 9, fy + fs - 11, 2, 2, k);
    } else if (over == 2) {
        // dead face: X eyes + frown
        fb_fill_rect(fx + 7, fy + 8, 2, 2, k);  fb_fill_rect(fx + 9, fy + 10, 2, 2, k);
        fb_fill_rect(fx + 9, fy + 8, 2, 2, k);  fb_fill_rect(fx + 7, fy + 10, 2, 2, k);
        fb_fill_rect(fx + fs - 11, fy + 8, 2, 2, k); fb_fill_rect(fx + fs - 9, fy + 10, 2, 2, k);
        fb_fill_rect(fx + fs - 9, fy + 8, 2, 2, k);  fb_fill_rect(fx + fs - 11, fy + 10, 2, 2, k);
        fb_fill_rect(fx + 8, fy + fs - 8, fs - 16, 2, k);
        fb_fill_rect(fx + 7, fy + fs - 10, 2, 2, k);
        fb_fill_rect(fx + fs - 9, fy + fs - 10, 2, 2, k);
    } else {
        // playing: dot eyes + smile
        fb_fill_rect(fx + 8, fy + 9, 2, 2, k);
        fb_fill_rect(fx + fs - 10, fy + 9, 2, 2, k);
        fb_fill_rect(fx + 8, fy + fs - 9, fs - 16, 2, k);
        fb_fill_rect(fx + 7, fy + fs - 11, 2, 2, k);
        fb_fill_rect(fx + fs - 9, fy + fs - 11, 2, 2, k);
    }
}

// A 3-digit red-on-black LED-style counter (mines-left / elapsed timer).
static void ms_draw_counter(int x, int y, uint32_t val) {
    if (val > 999) val = 999;
    char buf[8];
    snprintf(buf, sizeof(buf), "%03u", val);
    int w = 3 * FONT_WIDTH + 8;
    fb_fill_rect(x, y, w, MS_HEADER - 14, fb_rgb(20, 20, 20));
    font_draw_string(x + 4, y + (MS_HEADER - 14 - FONT_HEIGHT) / 2, buf,
                     fb_rgb(255, 60, 60), fb_rgb(20, 20, 20));
}

void minesweeper_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    minesweeper_win_t* m = (minesweeper_win_t*)win->reserved;
    if (!m) return;

    fb_fill_rect(cx, cy, cw, ch, fb_rgb(50, 52, 58));

    int hx = cx + MS_MARGIN, hy = cy + MS_MARGIN;
    int board_w = MS_COLS * MS_CELL;

    // Header panel.
    fb_fill_rect(hx, hy, board_w, MS_HEADER, fb_rgb(30, 32, 36));

    int mines_left = MS_MINES - m->flags_placed;
    if (mines_left < 0) mines_left = 0;
    ms_draw_counter(hx + 4, hy + 7, (uint32_t)mines_left);

    uint32_t elapsed;
    if (m->first_click) elapsed = 0;
    else {
        uint32_t endt = m->game_over ? m->end_tick : get_ticks();
        elapsed = (endt - m->start_tick) / 1000;
    }
    int tw = 3 * FONT_WIDTH + 8;
    ms_draw_counter(hx + board_w - tw - 4, hy + 7, elapsed);

    int fs = 26;
    int fx = hx + board_w / 2 - fs / 2;
    int fy = hy + (MS_HEADER - fs) / 2;
    ms_draw_face(fx, fy, fs, m->game_over);

    // Board.
    int bx0 = cx + MS_MARGIN;
    int by0 = cy + MS_MARGIN + MS_HEADER + MS_GAP;
    for (int r = 0; r < MS_ROWS; r++)
        for (int c = 0; c < MS_COLS; c++)
            ms_draw_cell(m, r, c, bx0 + c * MS_CELL, by0 + r * MS_CELL);
}

void minesweeper_win_click(window_t* win, int mx, int my, int btn) {
    minesweeper_win_t* m = (minesweeper_win_t*)win->reserved;
    if (!m) return;

    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);
    int hx = cx + MS_MARGIN, hy = cy + MS_MARGIN;
    int board_w = MS_COLS * MS_CELL;

    // Face button → new game (either mouse button).
    int fs = 26;
    int fx = hx + board_w / 2 - fs / 2;
    int fy = hy + (MS_HEADER - fs) / 2;
    if (mx >= fx && mx < fx + fs && my >= fy && my < fy + fs) {
        ms_reset(m);
        return;
    }

    int bx0 = cx + MS_MARGIN;
    int by0 = cy + MS_MARGIN + MS_HEADER + MS_GAP;
    if (mx < bx0 || my < by0) return;
    int c = (mx - bx0) / MS_CELL;
    int r = (my - by0) / MS_CELL;
    if (r < 0 || r >= MS_ROWS || c < 0 || c >= MS_COLS) return;

    if (btn == 2) ms_toggle_flag(m, r, c);
    else          ms_reveal(m, r, c);
}

void minesweeper_win_key(window_t* win, int key) {
    minesweeper_win_t* m = (minesweeper_win_t*)win->reserved;
    if (!m) return;
    if (key == 'r' || key == 'R' || key == 'n' || key == 'N') ms_reset(m);
}
