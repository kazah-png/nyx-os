// ============================================================
// particles_win.c - Nyx Particles: an integer particle-fountain perf demo (v6.4.80)
// ============================================================
// A P4 rendering-performance-testing window. `count` particles get sub-pixel
// fixed-point physics (Q.8): gravity pulls them down, they bounce off the floor and
// walls losing energy, and respawn at the bottom-centre nozzle once they settle, so
// the whole thing reads as a purple fountain of sparks. A benchmark HUD reports the
// measured per-frame simulation time and the derived FPS; +/- change the live
// particle count, so the frame-time visibly rises and falls with the load - the
// point of the demo. All-integer (the kernel is built -mno-sse, no floats).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "particles_win.h"
#include "../../drivers/video/font.h"

#define PFX      8                      // sub-pixel fixed-point bits (Q.8)
#define PONE     (1 << PFX)             // 1.0 px in fixed point
#define PART_MAX 4096                   // hard cap on particles (the +/- knob range)
#define PPAL_N   64                     // palette size (dim violet -> bright lilac by speed)

typedef struct { int x, y, vx, vy; } particle_t;   // position + velocity, all in Q.8 px

typedef struct {
    particle_t p[PART_MAX];
    int      count;                     // active particles (the perf knob, +/-)
    uint32_t rng;                       // xorshift PRNG state
    uint32_t pal[PPAL_N];
    uint32_t last_ft_ms;                // last measured per-frame sim time (ms)
    uint32_t last_fps;                  // derived FPS
} particles_ctx_t;

static uint32_t prng(particles_ctx_t* c) {
    uint32_t x = c->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; c->rng = x; return x;
}

// Launch one particle from the nozzle (bottom centre) with a randomized upward
// velocity and a little sideways spread - the fountain source.
static void emit(particles_ctx_t* c, particle_t* q) {
    q->x = (PART_WIN_W / 2) * PONE + (int)(prng(c) % (9u * PONE)) - 4 * PONE;   // +/- ~4 px spread
    q->y = (PART_WIN_H - 8) * PONE;
    q->vx = (int)(prng(c) % (7u * PONE)) - 3 * PONE;                            // -3..+3 px/tick
    q->vy = -(int)(prng(c) % (5u * PONE) + 4u * PONE);                          // up 4..9 px/tick
}

void* particles_create_ctx(void) {
    particles_ctx_t* c = (particles_ctx_t*)kmalloc(sizeof(particles_ctx_t));
    if (!c) return NULL;
    memset_asm(c, 0, sizeof(*c));
    c->rng = 0x1234abcdu;
    c->count = 512;                     // a lively default; +/- scales it
    // Purple Nyx palette: dim violet (slow) -> bright lilac (fast).
    for (int i = 0; i < PPAL_N; i++) {
        int r = 90  + i * 165 / PPAL_N;
        int g = 40  + i * 150 / PPAL_N;
        int b = 140 + i * 115 / PPAL_N;
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        c->pal[i] = fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    for (int i = 0; i < PART_MAX; i++) emit(c, &c->p[i]);
    return c;
}

void particles_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    particles_ctx_t* c = (particles_ctx_t*)win->reserved;
    if (!c) return;
    fb_fill_rect(cx, cy, PART_WIN_W, PART_WIN_H, fb_rgb(10, 8, 18));            // night-violet bg
    fb_fill_rect(cx, cy + PART_WIN_H - 4, PART_WIN_W, 1, fb_rgb(60, 45, 90));   // floor line

    for (int i = 0; i < c->count; i++) {
        int px = cx + (c->p[i].x >> PFX);
        int py = cy + (c->p[i].y >> PFX);
        if (px < cx || px >= cx + PART_WIN_W - 1 || py < cy || py >= cy + PART_WIN_H - 1) continue;
        int vpx = c->p[i].vx >> PFX, vpy = c->p[i].vy >> PFX;
        int idx = vpx * vpx + vpy * vpy;                                        // colour by speed^2
        if (idx >= PPAL_N) idx = PPAL_N - 1;
        fb_fill_rect(px, py, 2, 2, c->pal[idx]);
    }

    // Benchmark HUD (top-right): a live read of the sim under the current load.
    int hw = 150, hh = 92;
    int hx = cx + (int)PART_WIN_W - hw - 8, hy = cy + 8;
    fb_fill_rect(hx, hy, hw, hh, fb_rgb(22, 16, 32));
    fb_fill_rect(hx, hy, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy + hh - 1, hw, 1, fb_rgb(120, 90, 170));
    fb_fill_rect(hx, hy, 1, hh, fb_rgb(120, 90, 170));
    fb_fill_rect(hx + hw - 1, hy, 1, hh, fb_rgb(120, 90, 170));

    char line[48];
    font_draw_string_trans(hx + 8, hy + 6, "Nyx Particles", fb_rgb(230, 210, 250));
    snprintf(line, sizeof(line), "count %d", c->count);
    font_draw_string_trans(hx + 8, hy + 22, line, fb_rgb(205, 198, 224));
    if (c->last_ft_ms) snprintf(line, sizeof(line), "frame %u ms", c->last_ft_ms);
    else               snprintf(line, sizeof(line), "frame <1 ms");
    font_draw_string_trans(hx + 8, hy + 38, line, fb_rgb(205, 198, 224));
    snprintf(line, sizeof(line), "sim FPS %u", c->last_fps);
    font_draw_string_trans(hx + 8, hy + 54, line, fb_rgb(182, 176, 208));
    font_draw_string_trans(hx + 8, hy + 70, "+/- count  r reset", fb_rgb(150, 144, 178));
}

// Step the fountain one frame: gravity, integrate, bounce off floor/walls with energy
// loss, respawn settled particles. Timed with get_ticks() (the PIT is 1 kHz, so a tick
// is 1 ms); the measured cost scales with `count`, which is what the HUD reports.
int particles_win_tick(window_t* win) {
    particles_ctx_t* c = (particles_ctx_t*)win->reserved;
    if (!c) return 0;
    const int G = 48;                                    // gravity, Q.8 per tick
    const int floor = (PART_WIN_H - 6) * PONE;
    const int wall  = (PART_WIN_W - 1) * PONE;
    uint32_t t0 = get_ticks();
    for (int i = 0; i < c->count; i++) {
        particle_t* q = &c->p[i];
        q->vy += G;
        q->x  += q->vx;
        q->y  += q->vy;
        if (q->x < 0)         { q->x = 0;    q->vx = -q->vx * 3 / 4; }
        else if (q->x > wall) { q->x = wall; q->vx = -q->vx * 3 / 4; }
        if (q->y >= floor) {
            q->y  = floor;
            q->vy = -q->vy * 11 / 16;                    // bounce, lose energy
            q->vx = q->vx * 7 / 8;
            if (q->vy > -PONE) emit(c, q);               // settled -> relaunch from the nozzle
        }
    }
    uint32_t el = get_ticks() - t0;
    c->last_ft_ms = el;
    c->last_fps   = el ? (1000u / el) : 999u;
    return 1;
}

void particles_win_key(window_t* win, int key) {
    particles_ctx_t* c = (particles_ctx_t*)win->reserved;
    if (!c) return;
    if (key == '+' || key == '=') { c->count += 256; if (c->count > PART_MAX) c->count = PART_MAX; }
    else if (key == '-' || key == '_') { c->count -= 256; if (c->count < 64) c->count = 64; }
    else if (key == 'r' || key == 'R') { for (int i = 0; i < PART_MAX; i++) emit(c, &c->p[i]); }
}
