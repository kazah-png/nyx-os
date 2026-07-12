#include "kernel.h"
#include "bootsplash.h"
#include "font.h"
#include "nyx_logo.h"

/* NyxOS purple theme — matches the login screen (login.c) and the moon tint. */
#define ACCENT       fb_rgb(150, 110, 240)
#define LOGO_C       fb_rgb(168, 138, 245)   /* moon, identical to login.c   */
#define TITLE_C      fb_rgb(200, 180, 245)
#define SUB_C        fb_rgb(140, 120, 180)
#define TEXT_C       fb_rgb(205, 195, 225)
#define BAR_BG_C     fb_rgb(40, 34, 56)
#define BAR_FG_C     ACCENT
#define BAR_BDR      fb_rgb(66, 56, 88)
#define PCT_C        fb_rgb(190, 165, 255)
#define STAR_DIM     fb_rgb(60, 52, 82)
#define STAR_BRIGHT  fb_rgb(175, 158, 205)
#define SPLASH_BG    fb_rgb(24, 18, 40)      /* flat dark purple (erase/fill) */

#define BAR_W        500
#define BAR_H        14

#define SPIN_RADIUS  14
#define SPIN_DOT_SZ  5

#define NUM_STARS    60
#define TOTAL_MS     5000

// The logo is the shared NyxOS moon (nyx_logo.h) — the same brand mark used by
// the login screen and nyxfetch, so the whole boot flow shows one consistent
// logo instead of the old "NIGHTFALL" crescent.

// Pre-computed positions for 8 dots on a circle
static const int spin_dx[8] = {14, 10, 0, -10, -14, -10, 0, 10};
static const int spin_dy[8] = {0, 10, 14, 10, 0, -10, -14, -10};

static int logo_n, logo_top;
static int bar_x, bar_y, status_y;
static int spin_cx, spin_cy;

static struct { int x, y; int bright; } stars[NUM_STARS];
static int star_frame = 0;

static uint32_t start_ms = 0;

static uint32_t current_ms(void) {
    if (tick_count)
        return tick_count;
    return 0;
}

static void delay_ms(uint32_t ms) {
    uint32_t goal = current_ms() + ms;
    uint32_t t;
    while ((t = current_ms()) < goal) {
        for (volatile int i = 0; i < 20000; i++);
        if (t == 0) break;
    }
    if (current_ms() == 0) {
        for (volatile long i = 0; i < ms * 10000; i++);
    }
}

static void draw_spinner(int phase) {
    for (int i = 0; i < 8; i++) {
        int ri = (i + phase) % 8;
        int d = (ri < 3) ? 0 : (ri < 5) ? 1 : 2;
        uint32_t c = (d == 0) ? ACCENT : (d == 1) ? SUB_C : STAR_DIM;
        int sz = (d == 0) ? SPIN_DOT_SZ : SPIN_DOT_SZ - 1;
        int x = spin_cx + spin_dx[i] - sz / 2;
        int y = spin_cy + spin_dy[i] - sz / 2;
        fb_fill_rect(x, y, sz, sz, c);
    }
}

static void erase_spinner(void) {
    uint32_t bg = SPLASH_BG;
    int r = SPIN_RADIUS + 4;
    fb_fill_rect(spin_cx - r, spin_cy - r, r * 2, r * 2, bg);
}

static void draw_stars(void) {
    for (int i = 0; i < NUM_STARS; i++) {
        uint32_t c = stars[i].bright ? STAR_BRIGHT : STAR_DIM;
        fb_fill_rect(stars[i].x, stars[i].y, 2, 2, c);
    }
}

static void twinkle_stars(void) {
    star_frame++;
    for (int i = 0; i < 5; i++) {
        int idx = (star_frame * 7 + i * 13) % NUM_STARS;
        stars[idx].bright = !stars[idx].bright;
        uint32_t c = stars[idx].bright ? STAR_BRIGHT : STAR_DIM;
        fb_fill_rect(stars[idx].x, stars[idx].y, 2, 2, c);
    }
}

void bootsplash_init(void) {
    uint32_t fw = fb_get_width();
    uint32_t fh = fb_get_height();
    start_ms = 0;

    for (uint32_t y = 0; y < fh; y++) {
        uint32_t t = y * 255 / fh;
        uint8_t r = 16 + t * 22 / 255;
        uint8_t g = 12 + t * 12 / 255;
        uint8_t b = 30 + t * 26 / 255;
        if (r > 38) r = 38;
        if (g > 24) g = 24;
        if (b > 62) b = 62;
        fb_fill_rect(0, y, fw, 1, fb_rgb(r, g, b));
    }

    logo_n = NYX_LOGO_ROWS;

    int logo_h_px = logo_n * FONT_HEIGHT;
    logo_top = fh / 3 - logo_h_px / 2 - 20;

    uint32_t bg_col = SPLASH_BG;
    for (int i = 0; i < logo_n; i++) {
        int lw = NYX_LOGO_COLS * FONT_WIDTH;
        int lx = (fw - lw) / 2;
        font_draw_string(lx, logo_top + i * FONT_HEIGHT, NYX_LOGO[i], LOGO_C, bg_col);
    }

    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x = 20 + (i * 37 + 13) % (fw - 40);
        stars[i].y = 20 + (i * 53 + 7) % (int)(fh * 0.6);
        stars[i].bright = (i % 3) == 0;
    }
    draw_stars();

    bar_x = (fw - BAR_W) / 2;
    bar_y = fh * 3 / 4;
    status_y = bar_y + BAR_H + 22;

    fb_fill_rect(bar_x, bar_y, BAR_W, BAR_H, BAR_BG_C);
    fb_fill_rect(bar_x - 1, bar_y - 1, BAR_W + 2, 1, BAR_BDR);
    fb_fill_rect(bar_x - 1, bar_y + BAR_H, BAR_W + 2, 1, BAR_BDR);
    fb_fill_rect(bar_x - 1, bar_y, 1, BAR_H, BAR_BDR);
    fb_fill_rect(bar_x + BAR_W, bar_y, 1, BAR_H, BAR_BDR);

    spin_cx = bar_x + BAR_W + 30;
    spin_cy = bar_y + BAR_H / 2;

    font_draw_string((fw - 12 * FONT_WIDTH) / 2, status_y, "Starting...", TEXT_C, bg_col);

    start_ms = current_ms();

    for (int f = 0; f < 30; f++) {
        erase_spinner();
        draw_spinner(f);
        twinkle_stars();
        delay_ms(50);
    }
}

void bootsplash_update(int step, int total, const char* status) {
    uint32_t fw = fb_get_width();

    int fill_w = (step * BAR_W) / total;
    if (fill_w > 0)
        fb_fill_rect(bar_x, bar_y, fill_w, BAR_H, BAR_FG_C);

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (step * 100) / total);
    int px = bar_x + BAR_W + 12;
    int py = bar_y + (BAR_H - FONT_HEIGHT) / 2;
    uint32_t bg = SPLASH_BG;
    fb_fill_rect(px - 2, py - 2, 5 * FONT_WIDTH + 4, FONT_HEIGHT + 4, bg);
    font_draw_string(px, py, pct, PCT_C, bg);

    fb_fill_rect(0, status_y, fw, FONT_HEIGHT + 4, bg);
    if (status) {
        int sw = strlen(status) * FONT_WIDTH;
        int sx = (fw - sw) / 2;
        if (sx < 0) sx = 0;
        font_draw_string(sx, status_y, status, TEXT_C, bg);
    }

    erase_spinner();
    draw_spinner(step);
    twinkle_stars();

    uint32_t now = current_ms();
    if (now > 0 && start_ms > 0) {
        uint32_t elapsed = now - start_ms;
        uint32_t desired = (step * TOTAL_MS) / total;
        if (desired > elapsed) {
            uint32_t remain = desired - elapsed;
            if (remain > 200) remain = 200;
            delay_ms(remain);
        }
    } else {
        delay_ms(100);
    }
}

void bootsplash_clear(void) {
    for (int f = 0; f < 4; f++) {
        uint32_t step = (f + 1) * 64;
        uint8_t v = (step > 255) ? 255 : step;
        fb_fill_rect(0, 0, fb_get_width(), fb_get_height(), fb_rgb(v, v, v));
        for (volatile int i = 0; i < 5000000; i++);
    }
}
