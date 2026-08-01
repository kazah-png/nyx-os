// ============================================================
// voxel_win.c - an interactive isometric voxel scene (v6.4.39)
// ============================================================
// A step toward the Minecraft-class voxel-sandbox NORTH STAR. v6.4.38 rendered a
// STATIC scene; this makes it INTERACTIVE: a mutable heightmap in a per-window
// ctx, with left-click = place a cube on the picked cell and right-click = break
// the top one. Cubes are drawn with three shaded faces via per-column vertical
// spans, so no triangle/polygon primitive is needed (the framebuffer only offers
// fb_fill_rect / fb_fill_vgrad). A later increment adds a first-person camera.
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "voxel_win.h"

// Iso projection constants (2:1 isometric): cube half-width VX_W, so the top
// diamond's vertical half-height is VX_T = VX_W/2; VX_HH is the cube side height.
// The scene origin (apex of cell (0,0,0)'s top diamond) sits at client-relative
// (VX_OX, VX_OY).
#define VX_W   18
#define VX_T   9
#define VX_HH  18
#define VX_OX  (VOXEL_W / 2)
#define VX_OY  92
#define VX_MAXH 6        // max stack height (keeps tall stacks inside the window)

typedef struct {
    int hmap[VX_N][VX_N];
    int dirty;           // set on edit; voxel_win_tick returns 1 once to repaint
} voxel_ctx_t;

void* voxel_create_ctx(void) {
    voxel_ctx_t* c = (voxel_ctx_t*)kmalloc(sizeof(voxel_ctx_t));
    if (!c) return NULL;
    // A gentle starting hill.
    static const int seed[VX_N][VX_N] = {
        {0,0,0,0,0,0,0},
        {0,0,1,1,1,0,0},
        {0,1,1,2,1,1,0},
        {0,1,2,3,2,1,0},
        {0,1,1,2,1,1,0},
        {0,0,1,1,1,0,0},
        {0,0,0,0,0,0,0},
    };
    for (int y = 0; y < VX_N; y++)
        for (int x = 0; x < VX_N; x++)
            c->hmap[y][x] = seed[y][x];
    c->dirty = 0;
    return c;
}

// One isometric cube. (ox,oy) = apex (top vertex of the top diamond); three face
// colours (lit top, mid-left, dark-right) give the classic voxel look.
static void iso_cube(int ox, int oy, uint32_t top, uint32_t left, uint32_t right) {
    for (int dx = -VX_W; dx <= VX_W; dx++) {                 // top diamond
        int ax = dx < 0 ? -dx : dx;
        int span = VX_T - (ax * VX_T) / VX_W;
        fb_fill_rect(ox + dx, oy + VX_T - span, 1, 2 * span + 1, top);
    }
    for (int dx = -VX_W; dx <= 0; dx++) {                    // left face
        int edge = oy + VX_T + ((dx + VX_W) * VX_T) / VX_W;
        fb_fill_rect(ox + dx, edge, 1, VX_HH, left);
    }
    for (int dx = 0; dx <= VX_W; dx++) {                     // right face
        int edge = oy + 2 * VX_T - (dx * VX_T) / VX_W;
        fb_fill_rect(ox + dx, edge, 1, VX_HH, right);
    }
}

static void disc(int cx0, int cy0, int r, uint32_t col) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                fb_fill_rect(cx0 + dx, cy0 + dy, 1, 1, col);
}

void voxel_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    voxel_ctx_t* c = (voxel_ctx_t*)win->reserved;
    if (!c) return;

    // Nyx night sky: a deep-purple gradient with stars and the moon.
    fb_fill_vgrad(cx, cy, VOXEL_W, VOXEL_H, fb_rgb(24, 18, 44), fb_rgb(52, 40, 84));
    static const int stars[][2] = {
        {40,30},{90,60},{150,24},{210,50},{300,28},{360,66},{420,36},
        {70,110},{260,90},{400,120},{130,80},{330,140},{20,70},{190,120},
    };
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); i++)
        fb_fill_rect(cx + stars[i][0], cy + stars[i][1], 2, 2, fb_rgb(220, 220, 245));
    disc(cx + 412, cy + 54, 18, fb_rgb(212, 202, 240));

    // Terrain from the (mutable) heightmap, drawn back-to-front (row by row, each
    // stack bottom-up) so nearer cubes correctly overdraw farther ones.
    const int origin_x = cx + VX_OX;
    const int origin_y = cy + VX_OY;
    for (int gy = 0; gy < VX_N; gy++) {
        for (int gx = 0; gx < VX_N; gx++) {
            int top_h = c->hmap[gy][gx];
            for (int gz = 0; gz <= top_h; gz++) {
                int ox = origin_x + (gx - gy) * VX_W;
                int oy = origin_y + (gx + gy) * VX_T - gz * VX_HH;
                int is_top = (gz == top_h);
                uint32_t tcol, lcol, rcol;
                if (is_top && top_h >= 3) {           // high ground -> stone
                    tcol = fb_rgb(152,152,162); lcol = fb_rgb(112,112,122); rcol = fb_rgb(86,86,96);
                } else if (is_top) {                  // surface -> grass
                    tcol = fb_rgb(96,172,74);   lcol = fb_rgb(120,92,58);   rcol = fb_rgb(94,70,44);
                } else {                              // below the surface -> dirt
                    tcol = fb_rgb(134,101,62);  lcol = fb_rgb(112,84,52);   rcol = fb_rgb(88,66,40);
                }
                iso_cube(ox, oy, tcol, lcol, rcol);
            }
        }
    }

    font_draw_string_trans(cx + 10, cy + 10, "Nyx Voxels", fb_rgb(236, 230, 250));
    font_draw_string_trans(cx + 10, cy + VOXEL_H - 22,
                           "left-click place  -  right-click break", fb_rgb(182, 176, 208));
}

// Map a client click to the grid cell whose TOP face is under it, testing cells
// front-to-back (nearest first) so a click on a tall stack picks the stack, not a
// cell behind it. Returns 1 and sets *pgx/*pgy on a hit.
static int voxel_pick(voxel_ctx_t* c, int ox0, int oy0, int mx, int my, int* pgx, int* pgy) {
    for (int gy = VX_N - 1; gy >= 0; gy--)
        for (int gx = VX_N - 1; gx >= 0; gx--) {
            int gz = c->hmap[gy][gx];
            int ox = ox0 + (gx - gy) * VX_W;
            int oy = oy0 + (gx + gy) * VX_T - gz * VX_HH;      // apex of the top cube
            int dcx = mx - ox, dcy = my - (oy + VX_T);         // vs top-diamond centre
            if (dcx < 0) dcx = -dcx;
            if (dcy < 0) dcy = -dcy;
            if (dcx * VX_T + dcy * VX_W <= VX_W * VX_T) {       // inside the diamond
                *pgx = gx; *pgy = gy; return 1;
            }
        }
    return 0;
}

void voxel_win_click(window_t* win, int mx, int my, int btn) {
    voxel_ctx_t* c = (voxel_ctx_t*)win->reserved;
    if (!c) return;
    int ox0 = WIN_CLIENT_X(win) + VX_OX;
    int oy0 = WIN_CLIENT_Y(win) + VX_OY;
    int gx, gy;
    if (!voxel_pick(c, ox0, oy0, mx, my, &gx, &gy)) return;
    if (btn == 2) {                                  // right-click -> break
        if (c->hmap[gy][gx] > 0) c->hmap[gy][gx]--;
    } else {                                         // left-click -> place
        if (c->hmap[gy][gx] < VX_MAXH) c->hmap[gy][gx]++;
    }
    c->dirty = 1;
}

// Repaint once after an edit (the right-click path does not force a redraw, and
// the scene is otherwise static, so a tiny dirty-gated tick covers both buttons).
int voxel_win_tick(window_t* win) {
    voxel_ctx_t* c = (voxel_ctx_t*)win->reserved;
    if (!c || !c->dirty) return 0;
    c->dirty = 0;
    return 1;
}
