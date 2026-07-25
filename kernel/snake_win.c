// ============================================================
// snake_win.c - Snake, an in-kernel GUI game window (v5.9.42)
// ============================================================
// The client area is a score strip (SNAKE_HDR) over a SNAKE_COLS x SNAKE_ROWS grid.
// The compositor's ~30fps game-tick (window on_tick) calls snake_win_tick, which steps
// the snake once every STEP_TICKS frames (so it moves at a playable pace, not 30/s).
// Arrows / WASD steer, food grows the snake + scores, a wall or self hit ends the game
// (R restarts). Direction changes are queued and applied at the step, and a 180-degree
// reversal is rejected so a fast double-tap can't fold the snake onto itself.
#include "kernel.h"
#include "compositor.h"
#include "snake_win.h"
#include "font.h"

#define SNAKE_MAX  (SNAKE_COLS * SNAKE_ROWS)   // 352 — snake can fill the whole board
#define STEP_TICKS 5                           // move every 5 ticks (~6 steps/sec at 30fps)

typedef struct {
    uint8_t sx[SNAKE_MAX], sy[SNAKE_MAX];   // body cells in grid coords; [0] is the head
    int len;
    int dx, dy;         // current heading
    int ndx, ndy;       // queued heading (applied at the next step)
    int food_x, food_y;
    int score;
    int game_over;
    int tick_ctr;
    uint64_t rng;       // splitmix64 state for food placement
} snake_ctx_t;

static uint32_t snake_rnd(snake_ctx_t* s, uint32_t n) {
    s->rng += 0x9E3779B97F4A7C15ULL;                    // splitmix64
    uint64_t z = s->rng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (uint32_t)(z % n);
}

static int on_snake(snake_ctx_t* s, int x, int y) {
    for (int i = 0; i < s->len; i++)
        if (s->sx[i] == x && s->sy[i] == y) return 1;
    return 0;
}

static void place_food(snake_ctx_t* s) {
    for (int t = 0; t < 200; t++) {                     // random free cell (board is small)
        int x = (int)snake_rnd(s, SNAKE_COLS), y = (int)snake_rnd(s, SNAKE_ROWS);
        if (!on_snake(s, x, y)) { s->food_x = x; s->food_y = y; return; }
    }
    for (int y = 0; y < SNAKE_ROWS; y++)                // fallback: first free cell
        for (int x = 0; x < SNAKE_COLS; x++)
            if (!on_snake(s, x, y)) { s->food_x = x; s->food_y = y; return; }
}

static void snake_reset(snake_ctx_t* s) {
    s->len = 3;
    int hx = SNAKE_COLS / 2, hy = SNAKE_ROWS / 2;
    s->sx[0] = hx;     s->sy[0] = hy;                   // head, facing right
    s->sx[1] = hx - 1; s->sy[1] = hy;
    s->sx[2] = hx - 2; s->sy[2] = hy;
    s->dx = 1; s->dy = 0;
    s->ndx = 1; s->ndy = 0;
    s->score = 0;
    s->game_over = 0;
    s->tick_ctr = 0;
    place_food(s);
}

void* snake_create_ctx(void) {
    snake_ctx_t* s = (snake_ctx_t*)kmalloc(sizeof(snake_ctx_t));
    if (!s) return NULL;
    s->rng = (uint64_t)get_ticks() ^ 0xD1B54A32D192ED03ULL;
    snake_reset(s);
    return s;
}

void snake_win_tick(window_t* win) {
    snake_ctx_t* s = (snake_ctx_t*)win->reserved;
    if (!s || s->game_over) return;
    if (++s->tick_ctr < STEP_TICKS) return;             // pace the snake below the frame rate
    s->tick_ctr = 0;

    s->dx = s->ndx; s->dy = s->ndy;                     // commit the queued heading
    int nx = s->sx[0] + s->dx;
    int ny = s->sy[0] + s->dy;

    if (nx < 0 || nx >= SNAKE_COLS || ny < 0 || ny >= SNAKE_ROWS) { s->game_over = 1; return; }

    // Self-collision. The tail cell is exempt UNLESS we're about to grow (then it stays).
    int grow = (nx == s->food_x && ny == s->food_y);
    int check = s->len - (grow ? 0 : 1);
    for (int i = 0; i < check; i++)
        if (s->sx[i] == nx && s->sy[i] == ny) { s->game_over = 1; return; }

    if (grow && s->len < SNAKE_MAX) s->len++;
    for (int i = s->len - 1; i > 0; i--) { s->sx[i] = s->sx[i-1]; s->sy[i] = s->sy[i-1]; }
    s->sx[0] = (uint8_t)nx; s->sy[0] = (uint8_t)ny;

    if (grow) { s->score++; place_food(s); }
}

void snake_win_key(window_t* win, int key) {
    snake_ctx_t* s = (snake_ctx_t*)win->reserved;
    if (!s) return;
    if (key == 'r' || key == 'R') { snake_reset(s); return; }
    if (s->game_over) return;

    int ndx = s->ndx, ndy = s->ndy;
    if      (key == KEY_UP    || key == 'w' || key == 'W') { ndx = 0;  ndy = -1; }
    else if (key == KEY_DOWN  || key == 's' || key == 'S') { ndx = 0;  ndy = 1;  }
    else if (key == KEY_LEFT  || key == 'a' || key == 'A') { ndx = -1; ndy = 0;  }
    else if (key == KEY_RIGHT || key == 'd' || key == 'D') { ndx = 1;  ndy = 0;  }
    else return;
    if (ndx == -s->dx && ndy == -s->dy) return;         // no instant 180 into the neck
    s->ndx = ndx; s->ndy = ndy;
}

void snake_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    snake_ctx_t* s = (snake_ctx_t*)win->reserved;
    if (!s) return;

    fb_fill_rect(cx, cy, SNAKE_W, SNAKE_HDR, fb_rgb(28, 32, 44));           // score strip
    int by = cy + SNAKE_HDR;
    fb_fill_rect(cx, by, SNAKE_W, SNAKE_ROWS * SNAKE_CELL, fb_rgb(16, 20, 16)); // board

    for (int gx = 1; gx < SNAKE_COLS; gx++)                                 // subtle grid
        fb_fill_rect(cx + gx*SNAKE_CELL, by, 1, SNAKE_ROWS*SNAKE_CELL, fb_rgb(22, 28, 22));
    for (int gy = 1; gy < SNAKE_ROWS; gy++)
        fb_fill_rect(cx, by + gy*SNAKE_CELL, SNAKE_W, 1, fb_rgb(22, 28, 22));

    fb_fill_rect(cx + s->food_x*SNAKE_CELL + 3, by + s->food_y*SNAKE_CELL + 3,
                 SNAKE_CELL - 6, SNAKE_CELL - 6, fb_rgb(235, 80, 70));      // food

    for (int i = 0; i < s->len; i++) {                                     // snake
        uint32_t col = (i == 0) ? fb_rgb(140, 240, 130) : fb_rgb(70, 190, 90);
        fb_fill_rect(cx + s->sx[i]*SNAKE_CELL + 1, by + s->sy[i]*SNAKE_CELL + 1,
                     SNAKE_CELL - 2, SNAKE_CELL - 2, col);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %d", s->score);
    font_draw_string(cx + 6, cy + 4, buf, fb_rgb(220, 225, 235), fb_rgb(28, 32, 44));

    if (s->game_over) {
        const char* msg = "GAME OVER - press R";
        int tw = (int)strlen(msg) * FONT_WIDTH;
        int mx = cx + (SNAKE_W - tw) / 2;
        int my = by + (SNAKE_ROWS*SNAKE_CELL) / 2 - FONT_HEIGHT / 2;
        fb_fill_rect(mx - 6, my - 4, tw + 12, FONT_HEIGHT + 8, fb_rgb(20, 20, 25));
        font_draw_string(mx, my, msg, fb_rgb(240, 120, 110), fb_rgb(20, 20, 25));
    }
}
