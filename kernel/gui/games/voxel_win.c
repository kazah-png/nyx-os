// ============================================================
// voxel_win.c - a static isometric voxel scene (v6.4.38)
// ============================================================
// A first scoped step toward the Minecraft-class voxel-sandbox NORTH STAR. This
// slice renders a STATIC isometric heightmap of shaded cubes over a Nyx night
// sky, in an in-kernel window (like the games). Cubes are drawn with three shaded
// faces via per-column vertical spans, so no triangle/polygon primitive is needed
// (the framebuffer only offers fb_fill_rect / fb_fill_vgrad). Later increments add
// a first-person camera + keyboard/mouse input and block place/break; for now it
// is a non-interactive preview that proves voxel rendering in a window.
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "voxel_win.h"

// One isometric cube. (ox,oy) is the apex (top vertex of the top diamond); w is
// the diamond's half-width, and the 2:1 iso ratio makes its vertical half-height
// t = w/2; h is the cube's vertical side height. Three face colours (lit top,
// mid-shaded left, dark right) give the classic voxel look.
static void iso_cube(int ox, int oy, int w, int h,
                     uint32_t top, uint32_t left, uint32_t right) {
    int t = w / 2;
    for (int dx = -w; dx <= w; dx++) {                 // top diamond
        int ax = dx < 0 ? -dx : dx;
        int span = t - (ax * t) / w;                   // half-height at this column
        fb_fill_rect(ox + dx, oy + t - span, 1, 2 * span + 1, top);
    }
    for (int dx = -w; dx <= 0; dx++) {                 // left face (parallelogram)
        int edge = oy + t + ((dx + w) * t) / w;        // its top edge y at this column
        fb_fill_rect(ox + dx, edge, 1, h, left);
    }
    for (int dx = 0; dx <= w; dx++) {                  // right face (parallelogram)
        int edge = oy + 2 * t - (dx * t) / w;
        fb_fill_rect(ox + dx, edge, 1, h, right);
    }
}

// A small filled disc (the moon) — brute force, radius is tiny.
static void disc(int cx0, int cy0, int r, uint32_t col) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                fb_fill_rect(cx0 + dx, cy0 + dy, 1, 1, col);
}

void voxel_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win; (void)cw; (void)ch;

    // Nyx night sky: a deep-purple vertical gradient with a scatter of stars and
    // the moon (the brand's night-goddess identity).
    fb_fill_vgrad(cx, cy, VOXEL_W, VOXEL_H, fb_rgb(24, 18, 44), fb_rgb(52, 40, 84));
    static const int stars[][2] = {
        {40,30},{90,60},{150,24},{210,50},{300,28},{360,66},{420,36},
        {70,110},{260,90},{400,120},{130,80},{330,140},{20,70},{190,120},
    };
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); i++)
        fb_fill_rect(cx + stars[i][0], cy + stars[i][1], 2, 2, fb_rgb(220, 220, 245));
    disc(cx + 412, cy + 54, 18, fb_rgb(212, 202, 240));

    // A curated 7x7 heightmap — a gentle hill — drawn back-to-front (row by row,
    // each stack bottom-up) so nearer cubes correctly overdraw farther ones.
    static const int H = 7;
    static const int hmap[7][7] = {
        {0,0,0,0,0,0,0},
        {0,0,1,1,1,0,0},
        {0,1,1,2,1,1,0},
        {0,1,2,3,2,1,0},
        {0,1,1,2,1,1,0},
        {0,0,1,1,1,0,0},
        {0,0,0,0,0,0,0},
    };
    const int w = 18, t = 9, h = 18;
    const int origin_x = cx + VOXEL_W / 2;
    const int origin_y = cy + 92;

    for (int gy = 0; gy < H; gy++) {
        for (int gx = 0; gx < H; gx++) {
            int top_h = hmap[gy][gx];
            for (int gz = 0; gz <= top_h; gz++) {
                int ox = origin_x + (gx - gy) * w;
                int oy = origin_y + (gx + gy) * t - gz * h;
                int is_top = (gz == top_h);
                uint32_t tcol, lcol, rcol;
                if (is_top && top_h == 3) {            // peak -> stone
                    tcol = fb_rgb(152,152,162); lcol = fb_rgb(112,112,122); rcol = fb_rgb(86,86,96);
                } else if (is_top) {                   // surface -> grass
                    tcol = fb_rgb(96,172,74);   lcol = fb_rgb(120,92,58);   rcol = fb_rgb(94,70,44);
                } else {                               // below the surface -> dirt
                    tcol = fb_rgb(134,101,62);  lcol = fb_rgb(112,84,52);   rcol = fb_rgb(88,66,40);
                }
                iso_cube(ox, oy, w, h, tcol, lcol, rcol);
            }
        }
    }

    font_draw_string_trans(cx + 10, cy + 10, "Nyx Voxels", fb_rgb(236, 230, 250));
    font_draw_string_trans(cx + 10, cy + VOXEL_H - 22,
                           "voxel sandbox - preview (static scene)", fb_rgb(182, 176, 208));
}
