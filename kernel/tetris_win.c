// ============================================================
// tetris_win.c - Tetris, an in-kernel GUI game window (v5.9.43)
// ============================================================
// The well is a TET_COLS x TET_ROWS grid; the compositor's ~30fps game-tick (on_tick)
// applies gravity every drop_ticks frames (fewer as the level rises). Left/Right move,
// Up/W rotates (with a small wall kick), Down/S soft-drops one row, Space hard-drops,
// R restarts. Locking a piece clears any full lines and scores; a spawn that doesn't
// fit ends the game. Same in-kernel pattern as Pong/Snake, reusing on_tick.
#include "kernel.h"
#include "compositor.h"
#include "tetris_win.h"
#include "font.h"

// 7 tetrominoes x 4 rotations, as 4x4 bitmasks. Cell (r,c) is filled when
// shape & (0x8000 >> (r*4 + c)) — the nibble is the row, its MSB the leftmost column.
static const uint16_t TET_SHAPES[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 }, // I
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 }, // J
    { 0x2E00, 0x4460, 0x0E80, 0xC440 }, // L
    { 0x6600, 0x6600, 0x6600, 0x6600 }, // O
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 }, // S
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 }, // T
    { 0xC600, 0x2640, 0x0C60, 0x4C80 }, // Z
};

static uint32_t piece_color(int t) {
    switch (t) {
        case 0:  return fb_rgb( 80, 210, 230); // I cyan
        case 1:  return fb_rgb( 70, 110, 230); // J blue
        case 2:  return fb_rgb(235, 150,  50); // L orange
        case 3:  return fb_rgb(230, 205,  60); // O yellow
        case 4:  return fb_rgb( 90, 200, 100); // S green
        case 5:  return fb_rgb(180, 100, 220); // T purple
        default: return fb_rgb(230,  80,  80); // Z red
    }
}

typedef struct {
    uint8_t board[TET_ROWS][TET_COLS];   // 0 empty, else piece-type + 1
    int cur, rot, px, py;                // current piece, rotation, 4x4-box top-left (board coords)
    int next;
    int score, lines, level;
    int game_over;
    int drop_ctr;
    uint64_t rng;
} tetris_ctx_t;

static uint32_t tet_rnd(tetris_ctx_t* s, uint32_t n) {
    s->rng += 0x9E3779B97F4A7C15ULL;                    // splitmix64
    uint64_t z = s->rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (uint32_t)(z % n);
}

static int cell_filled(int type, int rot, int r, int c) {
    return (TET_SHAPES[type][rot] & (0x8000 >> (r * 4 + c))) != 0;
}

// Does piece (type,rot) fit at board top-left (px,py) — inside the walls/floor and
// not overlapping a locked cell? Cells above the top (by < 0) are allowed.
static int fits(tetris_ctx_t* s, int type, int rot, int px, int py) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!cell_filled(type, rot, r, c)) continue;
            int bx = px + c, by = py + r;
            if (bx < 0 || bx >= TET_COLS || by >= TET_ROWS) return 0;
            if (by >= 0 && s->board[by][bx]) return 0;
        }
    return 1;
}

static void spawn(tetris_ctx_t* s) {
    s->cur = s->next;
    s->next = (int)tet_rnd(s, 7);
    s->rot = 0;
    s->px = 3; s->py = 0;
    if (!fits(s, s->cur, s->rot, s->px, s->py)) s->game_over = 1;
}

static void tetris_reset(tetris_ctx_t* s) {
    for (int r = 0; r < TET_ROWS; r++)
        for (int c = 0; c < TET_COLS; c++) s->board[r][c] = 0;
    s->score = 0; s->lines = 0; s->level = 1;
    s->game_over = 0; s->drop_ctr = 0;
    s->next = (int)tet_rnd(s, 7);
    spawn(s);
}

void* tetris_create_ctx(void) {
    tetris_ctx_t* s = (tetris_ctx_t*)kmalloc(sizeof(tetris_ctx_t));
    if (!s) return NULL;
    s->rng = (uint64_t)get_ticks() ^ 0x2545F4914F6CDD1DULL;
    tetris_reset(s);
    return s;
}

static void lock_and_next(tetris_ctx_t* s) {
    for (int r = 0; r < 4; r++)                          // lock the piece into the well
        for (int c = 0; c < 4; c++)
            if (cell_filled(s->cur, s->rot, r, c)) {
                int by = s->py + r, bx = s->px + c;
                if (by >= 0 && by < TET_ROWS && bx >= 0 && bx < TET_COLS)
                    s->board[by][bx] = (uint8_t)(s->cur + 1);
            }

    int cleared = 0;                                     // clear full lines, bottom-up
    for (int r = TET_ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < TET_COLS; c++) if (!s->board[r][c]) { full = 0; break; }
        if (full) {
            for (int rr = r; rr > 0; rr--)
                for (int c = 0; c < TET_COLS; c++) s->board[rr][c] = s->board[rr-1][c];
            for (int c = 0; c < TET_COLS; c++) s->board[0][c] = 0;
            cleared++;
            r++;                                         // re-check this row after the shift
        }
    }
    if (cleared) {
        static const int pts[5] = { 0, 40, 100, 300, 1200 };   // classic line scoring
        s->lines += cleared;
        s->score += pts[cleared] * s->level;
        s->level = 1 + s->lines / 10;
    }
    spawn(s);
}

static void step_down(tetris_ctx_t* s) {
    if (fits(s, s->cur, s->rot, s->px, s->py + 1)) s->py++;
    else lock_and_next(s);
}

int tetris_win_tick(window_t* win) {
    tetris_ctx_t* s = (tetris_ctx_t*)win->reserved;
    if (!s || s->game_over) return 1;
    int drop_ticks = 24 - (s->level - 1) * 2;            // faster as the level rises
    if (drop_ticks < 4) drop_ticks = 4;
    if (++s->drop_ctr >= drop_ticks) { s->drop_ctr = 0; step_down(s); }
    return 1;
}

void tetris_win_key(window_t* win, int key) {
    tetris_ctx_t* s = (tetris_ctx_t*)win->reserved;
    if (!s) return;
    if (key == 'r' || key == 'R') { tetris_reset(s); return; }
    if (s->game_over) return;

    if (key == KEY_LEFT || key == 'a' || key == 'A') {
        if (fits(s, s->cur, s->rot, s->px - 1, s->py)) s->px--;
    } else if (key == KEY_RIGHT || key == 'd' || key == 'D') {
        if (fits(s, s->cur, s->rot, s->px + 1, s->py)) s->px++;
    } else if (key == KEY_DOWN || key == 's' || key == 'S') {
        step_down(s); s->drop_ctr = 0;
    } else if (key == KEY_UP || key == 'w' || key == 'W') {
        int nr = (s->rot + 1) & 3;                       // rotate CW, basic wall kick
        static const int kicks[5] = { 0, -1, 1, -2, 2 };
        for (int k = 0; k < 5; k++)
            if (fits(s, s->cur, nr, s->px + kicks[k], s->py)) { s->rot = nr; s->px += kicks[k]; break; }
    } else if (key == ' ') {
        while (fits(s, s->cur, s->rot, s->px, s->py + 1)) s->py++;   // hard drop
        lock_and_next(s);
    }
}

static void draw_cell(int x, int y, uint32_t col) {
    fb_fill_rect(x + 1, y + 1, TET_CELL - 2, TET_CELL - 2, col);
    fb_fill_rect(x + 1, y + 1, TET_CELL - 2, 2, fb_rgb(255, 255, 255)); // top bevel
}

void tetris_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    tetris_ctx_t* s = (tetris_ctx_t*)win->reserved;
    if (!s) return;

    fb_fill_rect(cx, cy, TET_W, TET_H, fb_rgb(24, 26, 36));
    int bx = cx + TET_MARGIN, by = cy + TET_MARGIN;

    fb_fill_rect(bx, by, TET_BOARD_W, TET_BOARD_H, fb_rgb(14, 15, 22));      // well
    for (int c = 1; c < TET_COLS; c++)
        fb_fill_rect(bx + c*TET_CELL, by, 1, TET_BOARD_H, fb_rgb(30, 32, 44));
    for (int r = 1; r < TET_ROWS; r++)
        fb_fill_rect(bx, by + r*TET_CELL, TET_BOARD_W, 1, fb_rgb(30, 32, 44));

    for (int r = 0; r < TET_ROWS; r++)                                       // locked cells
        for (int c = 0; c < TET_COLS; c++)
            if (s->board[r][c])
                draw_cell(bx + c*TET_CELL, by + r*TET_CELL, piece_color(s->board[r][c] - 1));

    if (!s->game_over)                                                       // falling piece
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (cell_filled(s->cur, s->rot, r, c)) {
                    int gy = s->py + r, gx = s->px + c;
                    if (gy >= 0) draw_cell(bx + gx*TET_CELL, by + gy*TET_CELL, piece_color(s->cur));
                }

    int panel_x = bx + TET_BOARD_W + 10;                                     // side panel
    uint32_t bg = fb_rgb(24, 26, 36), lbl = fb_rgb(200, 205, 220);
    font_draw_string(panel_x, by, "NEXT", lbl, bg);
    int nbx = panel_x, nby = by + 20;
    fb_fill_rect(nbx, nby, TET_CELL*4, TET_CELL*4, fb_rgb(14, 15, 22));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cell_filled(s->next, 0, r, c))
                draw_cell(nbx + c*TET_CELL, nby + r*TET_CELL, piece_color(s->next));

    char buf[16];
    int ty = nby + TET_CELL*4 + 16;
    font_draw_string(panel_x, ty, "SCORE", lbl, bg);
    snprintf(buf, sizeof(buf), "%d", s->score);
    font_draw_string(panel_x, ty + 18, buf, fb_rgb(240, 240, 120), bg);
    font_draw_string(panel_x, ty + 44, "LINES", lbl, bg);
    snprintf(buf, sizeof(buf), "%d", s->lines);
    font_draw_string(panel_x, ty + 62, buf, fb_rgb(160, 230, 160), bg);
    font_draw_string(panel_x, ty + 88, "LEVEL", lbl, bg);
    snprintf(buf, sizeof(buf), "%d", s->level);
    font_draw_string(panel_x, ty + 106, buf, fb_rgb(200, 200, 240), bg);

    if (s->game_over) {
        const char* m = "GAME OVER";
        const char* m2 = "press R";
        font_draw_string(bx + (TET_BOARD_W - (int)strlen(m)*FONT_WIDTH)/2,
                         by + TET_BOARD_H/2 - 18, m, fb_rgb(240, 110, 110), fb_rgb(14, 15, 22));
        font_draw_string(bx + (TET_BOARD_W - (int)strlen(m2)*FONT_WIDTH)/2,
                         by + TET_BOARD_H/2 + 2, m2, fb_rgb(230, 200, 200), fb_rgb(14, 15, 22));
    }
}
