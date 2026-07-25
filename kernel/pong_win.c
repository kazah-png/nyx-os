// ============================================================
// pong_win.c - Pong, an in-kernel GUI game window (v5.9.39)
// ============================================================
// The court is the window client (PONG_W x PONG_H). The compositor's ~30fps game-tick
// (window on_tick) calls pong_win_tick, which advances the ball, bounces it off the
// walls and paddles, moves the AI paddle, and scores. The player's paddle follows the
// mouse (on_mousemove) — classic paddle control — and arrows / W-S nudge it too.
#include "kernel.h"
#include "compositor.h"
#include "pong_win.h"

#define PW  8       // paddle width
#define PH  54      // paddle height
#define BS  8       // ball size
#define LPX 12                    // left  (player) paddle x
#define RPX (PONG_W - 12 - PW)    // right (AI)     paddle x

typedef struct {
    int ball_x, ball_y, vx, vy;
    int lpaddle_y, rpaddle_y;      // top-left y of each paddle
    int lscore, rscore;
} pong_ctx_t;

static void reset_ball(pong_ctx_t* p, int dir) {
    p->ball_x = PONG_W / 2 - BS / 2;
    p->ball_y = PONG_H / 2 - BS / 2;
    p->vx = dir * 5;
    p->vy = 3;
}

void* pong_create_ctx(void) {
    pong_ctx_t* p = (pong_ctx_t*)kmalloc(sizeof(pong_ctx_t));
    if (!p) return NULL;
    p->lpaddle_y = PONG_H / 2 - PH / 2;
    p->rpaddle_y = PONG_H / 2 - PH / 2;
    p->lscore = 0;
    p->rscore = 0;
    reset_ball(p, 1);
    return p;
}

// Bounce off a paddle: reverse vx and add "spin" from where on the paddle it hit.
static void paddle_bounce(pong_ctx_t* p, int paddle_y) {
    p->vx = -p->vx;
    int rel = (p->ball_y + BS / 2) - (paddle_y + PH / 2);   // -PH/2 .. +PH/2
    p->vy = rel / 6;
    if (p->vy == 0) p->vy = (rel < 0) ? -1 : 1;
}

void pong_win_tick(window_t* win) {
    pong_ctx_t* p = (pong_ctx_t*)win->reserved;
    if (!p) return;

    p->ball_x += p->vx;
    p->ball_y += p->vy;

    // top / bottom walls
    if (p->ball_y < 0)              { p->ball_y = 0;              p->vy = -p->vy; }
    if (p->ball_y + BS > PONG_H)    { p->ball_y = PONG_H - BS;    p->vy = -p->vy; }

    // player paddle (left), only while moving left toward it
    if (p->vx < 0 && p->ball_x <= LPX + PW && p->ball_x + BS >= LPX &&
        p->ball_y + BS >= p->lpaddle_y && p->ball_y <= p->lpaddle_y + PH) {
        p->ball_x = LPX + PW;
        paddle_bounce(p, p->lpaddle_y);
    }
    // AI paddle (right), only while moving right toward it
    if (p->vx > 0 && p->ball_x + BS >= RPX && p->ball_x <= RPX + PW &&
        p->ball_y + BS >= p->rpaddle_y && p->ball_y <= p->rpaddle_y + PH) {
        p->ball_x = RPX - BS;
        paddle_bounce(p, p->rpaddle_y);
    }

    // scoring (ball leaves left/right edge)
    if (p->ball_x + BS < 0)  { p->rscore++; reset_ball(p, -1); }
    if (p->ball_x > PONG_W)  { p->lscore++; reset_ball(p,  1); }

    // AI: chase the ball, capped so a human can win
    int target = p->ball_y + BS / 2 - PH / 2;
    int speed = 4;
    if (p->rpaddle_y < target - 2)      p->rpaddle_y += speed;
    else if (p->rpaddle_y > target + 2) p->rpaddle_y -= speed;
    if (p->rpaddle_y < 0)               p->rpaddle_y = 0;
    if (p->rpaddle_y + PH > PONG_H)     p->rpaddle_y = PONG_H - PH;
}

void pong_win_mousemove(window_t* win, int mx, int my, int btns) {
    (void)mx; (void)btns;
    pong_ctx_t* p = (pong_ctx_t*)win->reserved;
    if (!p) return;
    int y = my - (win->y + TITLE_H) - PH / 2;      // centre the paddle on the cursor
    if (y < 0)               y = 0;
    if (y + PH > PONG_H)     y = PONG_H - PH;
    p->lpaddle_y = y;
}

void pong_win_key(window_t* win, int key) {
    pong_ctx_t* p = (pong_ctx_t*)win->reserved;
    if (!p) return;
    int step = 26;
    if (key == KEY_UP   || key == 'w' || key == 'W') p->lpaddle_y -= step;
    if (key == KEY_DOWN || key == 's' || key == 'S') p->lpaddle_y += step;
    if (p->lpaddle_y < 0)            p->lpaddle_y = 0;
    if (p->lpaddle_y + PH > PONG_H)  p->lpaddle_y = PONG_H - PH;
}

void pong_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    pong_ctx_t* p = (pong_ctx_t*)win->reserved;
    if (!p) return;

    fb_fill_rect(cx, cy, PONG_W, PONG_H, fb_rgb(10, 10, 15));                 // court
    for (int y = 0; y < PONG_H; y += 24)                                     // centre net
        fb_fill_rect(cx + PONG_W / 2 - 1, cy + y, 2, 12, fb_rgb(55, 55, 68));

    fb_fill_rect(cx + LPX, cy + p->lpaddle_y, PW, PH, fb_rgb(120, 200, 255)); // player
    fb_fill_rect(cx + RPX, cy + p->rpaddle_y, PW, PH, fb_rgb(255, 140, 120)); // AI
    fb_fill_rect(cx + p->ball_x, cy + p->ball_y, BS, BS, fb_rgb(255, 255, 255));

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", p->lscore);
    font_draw_string(cx + PONG_W / 2 - 40, cy + 8, buf, fb_rgb(200, 210, 230), fb_rgb(10, 10, 15));
    snprintf(buf, sizeof(buf), "%d", p->rscore);
    font_draw_string(cx + PONG_W / 2 + 32, cy + 8, buf, fb_rgb(230, 200, 200), fb_rgb(10, 10, 15));
}
