// ============================================================
// aeronyx_win.c - Aeronyx: an ANIMATED nyxfetch (v0.1)
// ============================================================
// The NyxOS moon as a rotating, shaded 3D SPHERE rendered in ASCII — a donut.c-style
// relief where each surface point's normal-dot-light luminance picks a glyph off a ramp
// (".,-~:;=!*#$@"). It spins on two axes beside live system stats (version, uptime,
// memory). Inspired by areofyl/fetch (a C tool that renders a distro logo as a spinning
// ASCII 3D object). All fixed-point: the kernel is built -mno-sse (no floats), so the
// sphere point + its normal rotate with Q12 sin/cos and project with a perspective divide.
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "aeronyx_win.h"
#include "../../drivers/video/font.h"

#define AX_COLS 32          // ASCII sphere grid, in glyph cells
#define AX_ROWS 20

typedef struct {
    int spin;               // rotation phase around Y (0..255)
    int tip;                // rotation phase around X (0..255)
} aeronyx_ctx_t;

// Fixed-point sine, phase 0..255 -> -4096..4096 (Q12), parabolic (no table, no float).
static int isin(int a) { a &= 255; int s = 1; if (a >= 128) { a -= 128; s = -1; } return s * (a * (128 - a)); }
static int icos(int a) { return isin(a + 64); }

// Luminance ramp, dim -> bright (the donut.c classic).
static const char AX_RAMP[] = ".,-~:;=!*#$@";
#define AX_RAMP_N 12

void* aeronyx_create_ctx(void) {
    aeronyx_ctx_t* c = (aeronyx_ctx_t*)kmalloc(sizeof(aeronyx_ctx_t));
    if (!c) return 0;
    c->spin = 0; c->tip = 36;
    return c;
}

void aeronyx_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    aeronyx_ctx_t* c = (aeronyx_ctx_t*)win->reserved;
    if (!c) return;
    fb_fill_rect(cx, cy, AERONYX_WIN_W, AERONYX_WIN_H, fb_rgb(10, 8, 20));   // night sky

    // --- rasterise the shaded sphere into an ASCII grid with a per-cell z-buffer ---
    static char cell[AX_ROWS][AX_COLS];
    static int  zbuf[AX_ROWS][AX_COLS];
    for (int r = 0; r < AX_ROWS; r++)
        for (int q = 0; q < AX_COLS; q++) { cell[r][q] = 0; zbuf[r][q] = -(1 << 30); }

    const int csp = icos(c->spin), ssp = isin(c->spin);   // spin around Y
    const int ctp = icos(c->tip),  stp = isin(c->tip);    // tip around X
    const int LX = -1638, LY = -2867, LZ = -2458;         // unit-ish light dir (Q12): upper-front-left
    const int CAMZ = 12288, SX = 46, SY = 26;             // perspective camera + grid scale

    for (int u = 0; u < 256; u += 3) {                    // longitude
        const int cu = icos(u), su = isin(u);
        for (int v = 0; v < 256; v += 3) {                // latitude (full sweep covers the whole sphere)
            const int cv = icos(v), sv = isin(v);
            // unit sphere point (Q12, R=4096=1.0); for a sphere the surface NORMAL == the point.
            int x = (cv * cu) >> 12, y = (cv * su) >> 12, z = sv;
            // rotate around Y (spin) then X (tip) — the same tumble as Nyx Rotor.
            int x1 = (x * csp + z * ssp) >> 12;
            int z1 = (-x * ssp + z * csp) >> 12;
            int y2 = (y * ctp - z1 * stp) >> 12;
            int z2 = (y * stp + z1 * ctp) >> 12;
            // luminance = rotated normal . light (the point is the normal); only lit points draw.
            int lum = (x1 * LX + y2 * LY + z2 * LZ) >> 12;   // ~ -4096..4096
            if (lum <= 0) continue;
            int ri = lum * AX_RAMP_N / 4096; if (ri >= AX_RAMP_N) ri = AX_RAMP_N - 1;
            // perspective project to a grid cell (col,row); nearest z wins that cell.
            int denom = z2 + CAMZ; if (denom < 256) denom = 256;
            int col = AX_COLS / 2 + (x1 * SX) / denom;
            int row = AX_ROWS / 2 - (y2 * SY) / denom;
            if (col < 0 || col >= AX_COLS || row < 0 || row >= AX_ROWS) continue;
            if (z2 > zbuf[row][col]) { zbuf[row][col] = z2; cell[row][col] = AX_RAMP[ri]; }
        }
    }

    // draw the grid: brighter glyphs get a brighter lilac, so the shading reads in colour too.
    const int gx = cx + 20, gy = cy + 28;
    for (int r = 0; r < AX_ROWS; r++)
        for (int q = 0; q < AX_COLS; q++) {
            char g = cell[r][q];
            if (!g) continue;
            int b = 120; for (int k = 0; k < AX_RAMP_N; k++) if (AX_RAMP[k] == g) { b = 120 + k * 135 / AX_RAMP_N; break; }
            char s[2] = { g, 0 };
            font_draw_string_trans((uint32_t)(gx + q * FONT_WIDTH), (uint32_t)(gy + r * FONT_HEIGHT), s,
                                   fb_rgb((uint8_t)(b * 200 / 255), (uint8_t)(b * 170 / 255), (uint8_t)b));
        }

    // --- live system stats panel (right side) — the "fetch" half of aeronyx ---
    extern void mem_pool_kb(uint32_t*, uint32_t*, uint32_t*);
    extern uint32_t get_uptime_seconds(void);
    int px = cx + AX_COLS * FONT_WIDTH + 44, py = cy + 40;
    uint32_t used_kb = 0, tot_kb = 0; mem_pool_kb(&used_kb, 0, &tot_kb);
    uint32_t up = get_uptime_seconds();
    char line[64];
    font_draw_string_trans((uint32_t)px, (uint32_t)py, "Aeronyx", fb_rgb(200, 170, 250));
    font_draw_string_trans((uint32_t)px, (uint32_t)(py + FONT_HEIGHT), "nyx@nyxos", fb_rgb(150, 140, 190));
    py += FONT_HEIGHT * 3;
    snprintf(line, sizeof(line), "OS      NyxOS %s", KERNEL_VERSION);
    font_draw_string_trans((uint32_t)px, (uint32_t)py, line, fb_rgb(210, 205, 225)); py += FONT_HEIGHT + 4;
    snprintf(line, sizeof(line), "Kernel  %s", KERNEL_NAME);
    font_draw_string_trans((uint32_t)px, (uint32_t)py, line, fb_rgb(210, 205, 225)); py += FONT_HEIGHT + 4;
    snprintf(line, sizeof(line), "Uptime  %uh %um %us", up / 3600, (up / 60) % 60, up % 60);
    font_draw_string_trans((uint32_t)px, (uint32_t)py, line, fb_rgb(210, 205, 225)); py += FONT_HEIGHT + 4;
    snprintf(line, sizeof(line), "Memory  %u / %u MB", used_kb / 1024, tot_kb / 1024);
    font_draw_string_trans((uint32_t)px, (uint32_t)py, line, fb_rgb(210, 205, 225)); py += FONT_HEIGHT + 4;
    snprintf(line, sizeof(line), "Shell   nyxsh");
    font_draw_string_trans((uint32_t)px, (uint32_t)py, line, fb_rgb(210, 205, 225)); py += FONT_HEIGHT + 4;
    snprintf(line, sizeof(line), "WM      Hemera");
    font_draw_string_trans((uint32_t)px, (uint32_t)py, line, fb_rgb(210, 205, 225));
}

int aeronyx_win_tick(window_t* win) {
    aeronyx_ctx_t* c = (aeronyx_ctx_t*)win->reserved;
    if (!c) return 0;
    c->spin = (c->spin + 2) & 255;   // spin around the vertical
    c->tip  = (c->tip + 1) & 255;    // gentle tip
    return 1;                        // repaint every tick (the spin is the point)
}
