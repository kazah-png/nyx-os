// ============================================================
// voxel_win.c - an isometric voxel 3D-render perf-testing sandbox (v6.4.52)
// ============================================================
// The concrete driver of the P4 performance-testing north star (reframed by the
// user 2026-08-02: this is NOT a game, it is a study/testbed for the software 3D
// renderer). v6.4.38 rendered a static scene; v6.4.39 made it interactive
// (place/break); v6.4.40 added BLOCK TYPES + a palette; v6.4.41 procedural
// worldgen (G); v6.4.42 a rotatable iso camera (R); v6.4.52 adds a BENCHMARK HUD
// (key B): it times the software renderer's raw throughput (per-frame render time,
// derived FPS, cube fill count) so the sandbox measures fill rate / voxel
// throughput, not just draws a scene. Cubes are drawn with three shaded faces via
// per-column vertical spans, so no triangle primitive is needed (the framebuffer
// only offers fb_fill_rect / fb_fill_vgrad).
#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "voxel_win.h"
#include "../../drivers/video/font.h"

// Iso projection constants (2:1 isometric): cube half-width VX_W, top-diamond
// vertical half-height VX_T = VX_W/2, cube side height VX_HH. Scene origin (apex
// of cell (0,0,0)'s top diamond) is client-relative (VX_OX, VX_OY).
#define VX_W    18
#define VX_T    9
#define VX_HH   18
#define VX_OX   (VOXEL_W / 2)
#define VX_OY   96
#define VX_MAXH 7        // max blocks per column (keeps tall stacks in the window)

// Benchmark mode (the 3D-render PERFORMANCE-TESTING purpose, P4): when on, each
// visible frame re-renders the whole terrain VOXEL_BENCH_REPS times and times it
// with get_ticks() (the PIT runs at 1 kHz, so a tick == 1 ms). Repeating the pass
// lifts the measurement above the 1 ms clock resolution and above the compositor's
// ~30 fps redraw cap, so the HUD reports the software renderer's raw throughput
// (per-frame render time + derived FPS + the cube fill count).
#define VOXEL_BENCH_REPS 40

// Palette bar geometry (client-relative), one swatch per block type.
#define PAL_X   10
#define PAL_Y   30
#define PAL_SW  22       // swatch size
#define PAL_GAP 4

typedef struct { uint32_t top, left, right; } block_t;
#define NTYPES 6
static block_t g_types[NTYPES];
static int g_types_ready = 0;
static const char* TYPE_INITIAL = "GDSWSC";   // Grass Dirt Stone Wood Sand Cyan(water)

// Build the block palette once (fb_rgb is a runtime function, so this can't be a
// static const initialiser). top = lit face, left/right = shaded sides.
static void voxel_init_types(void) {
    if (g_types_ready) return;
    g_types[0] = (block_t){ fb_rgb(96,172,74),   fb_rgb(120,92,58),   fb_rgb(94,70,44) };    // grass
    g_types[1] = (block_t){ fb_rgb(134,101,62),  fb_rgb(112,84,52),   fb_rgb(88,66,40) };    // dirt
    g_types[2] = (block_t){ fb_rgb(152,152,162), fb_rgb(112,112,122), fb_rgb(86,86,96) };    // stone
    g_types[3] = (block_t){ fb_rgb(150,110,60),  fb_rgb(120,85,48),   fb_rgb(95,66,38) };    // wood
    g_types[4] = (block_t){ fb_rgb(225,215,150), fb_rgb(205,195,130), fb_rgb(175,165,105) }; // sand
    g_types[5] = (block_t){ fb_rgb(120,200,225), fb_rgb(95,170,200),  fb_rgb(70,140,170) };  // water
    g_types_ready = 1;
}

typedef struct {
    signed char col[VX_N][VX_N][VX_MAXH];   // block type at each level (valid for z < height)
    unsigned char height[VX_N][VX_N];        // number of stacked blocks (>= 1)
    int sel;                                 // selected palette type
    int dirty;                               // set on edit; on_tick returns 1 once to repaint
    unsigned seed;                           // worldgen seed (advanced by the 'g' key)
    int view;                                // isometric camera orientation, 0..3 (R rotates)
    int bench;                               // benchmark mode (perf HUD) toggle — key B
    uint32_t last_fps;                       // last measured render throughput (frames/s)
    uint32_t last_ft_us;                     // last measured per-frame render time (microseconds)
    int last_cubes;                          // cubes rendered per frame (fill load)
} voxel_ctx_t;

// Map a SCREEN grid position (the iso layout is fixed) to the DATA cell it shows,
// applying the current camera orientation — so `R` orbits the island 90 degrees
// by rotating which cell fills each screen slot (the painter order is unchanged).
static void rot_cell(int view, int gx, int gy, int* sx, int* sy) {
    switch (view & 3) {
        default:
        case 0: *sx = gx;              *sy = gy;              break;
        case 1: *sx = gy;              *sy = VX_N - 1 - gx;   break;
        case 2: *sx = VX_N - 1 - gx;   *sy = VX_N - 1 - gy;   break;
        case 3: *sx = VX_N - 1 - gy;   *sy = gx;              break;
    }
}

void* voxel_create_ctx(void) {
    voxel_ctx_t* c = (voxel_ctx_t*)kmalloc(sizeof(voxel_ctx_t));
    if (!c) return NULL;
    // A gentle starting hill (top index -> height = index + 1 blocks).
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
        for (int x = 0; x < VX_N; x++) {
            int H = seed[y][x] + 1;
            c->height[y][x] = (unsigned char)H;
            for (int z = 0; z < H; z++)
                c->col[y][x][z] = (z == H - 1) ? 0 : 1;   // surface grass, below dirt
        }
    c->sel = 0;
    c->dirty = 0;
    c->seed = 0x2f6e6479u;
    c->view = 0;
    c->bench = 0;
    c->last_fps = 0;
    c->last_ft_us = 0;
    c->last_cubes = 0;
    return c;
}

// A tiny integer hash (xorshift-ish) — the kernel is built -mno-sse, so worldgen
// stays entirely in integer math (no float terrain noise).
static unsigned vx_hash(unsigned a) {
    a ^= a << 13; a ^= a >> 17; a ^= a << 5;
    return a ? a : 0x9e3779b9u;
}

// Fill the world procedurally: per-cell random heights, box-blurred into rolling
// hills, then a surface material by elevation band — water in the low pools, sand
// on the shores, grass on the plains, stone on the peaks, dirt underneath.
static void voxel_worldgen(voxel_ctx_t* c, unsigned seed) {
    int raw[VX_N][VX_N];
    for (int y = 0; y < VX_N; y++)
        for (int x = 0; x < VX_N; x++)
            raw[y][x] = 1 + (int)(vx_hash(seed ^ (y * 73856093u) ^ (x * 19349663u)) % VX_MAXH);
    for (int y = 0; y < VX_N; y++)
        for (int x = 0; x < VX_N; x++) {
            int s = raw[y][x] * 2, n = 2;      // weight the centre so hills stay varied
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int yy = y + dy, xx = x + dx;
                    if (yy >= 0 && yy < VX_N && xx >= 0 && xx < VX_N) { s += raw[yy][xx]; n++; }
                }
            int H = s / n;
            if (H < 1) H = 1;
            if (H > VX_MAXH) H = VX_MAXH;
            c->height[y][x] = (unsigned char)H;
            for (int z = 0; z < H; z++) {
                int ty;
                if (z < H - 1)      ty = 1;    // dirt underground
                else if (H <= 1)    ty = 5;    // water pool
                else if (H == 2)    ty = 4;    // sandy shore
                else if (H >= 5)    ty = 2;    // stone peak
                else                ty = 0;    // grass plain
                c->col[y][x][z] = (signed char)ty;
            }
        }
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
    voxel_init_types();

    // Nyx night sky: a deep-purple gradient with stars and the moon.
    fb_fill_vgrad(cx, cy, VOXEL_W, VOXEL_H, fb_rgb(24, 18, 44), fb_rgb(52, 40, 84));
    static const int stars[][2] = {
        {60,150},{150,140},{300,150},{360,120},{420,150},
        {260,150},{400,140},{330,150},{200,140},{440,120},
    };
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); i++)
        fb_fill_rect(cx + stars[i][0], cy + stars[i][1], 2, 2, fb_rgb(220, 220, 245));
    disc(cx + 250, cy + 40, 14, fb_rgb(212, 202, 240));   // moon (clear of the top-right perf HUD)

    // Terrain, back-to-front (row by row, each column bottom-up). In benchmark mode
    // the whole terrain pass is rendered VOXEL_BENCH_REPS times and timed so the HUD
    // can report the renderer's raw throughput; otherwise it renders exactly once.
    const int origin_x = cx + VX_OX;
    const int origin_y = cy + VX_OY;
    int cubes = 0;
    int reps = c->bench ? VOXEL_BENCH_REPS : 1;
    uint32_t t0 = get_ticks();
    for (int rep = 0; rep < reps; rep++) {
        cubes = 0;
        for (int gy = 0; gy < VX_N; gy++) {
            for (int gx = 0; gx < VX_N; gx++) {
                int sx, sy; rot_cell(c->view, gx, gy, &sx, &sy);   // orientation-mapped data cell
                int H = c->height[sy][sx];
                for (int gz = 0; gz < H; gz++) {
                    int ty = c->col[sy][sx][gz];
                    if (ty < 0 || ty >= NTYPES) ty = 1;
                    int ox = origin_x + (gx - gy) * VX_W;
                    int oy = origin_y + (gx + gy) * VX_T - gz * VX_HH;
                    iso_cube(ox, oy, g_types[ty].top, g_types[ty].left, g_types[ty].right);
                    cubes++;
                }
            }
        }
    }
    uint32_t elapsed = get_ticks() - t0;
    c->last_cubes = cubes;
    if (c->bench) {
        if (elapsed == 0) elapsed = 1;                          // 1 ms clock-resolution floor
        c->last_fps   = (uint32_t)reps * 1000u / elapsed;       // frames the renderer can push
        c->last_ft_us = elapsed * 1000u / (uint32_t)reps;       // per-frame render time (us)
    }

    // Title + palette bar (drawn last, over the sky).
    font_draw_string_trans(cx + 10, cy + 10, "Nyx Voxels", fb_rgb(236, 230, 250));
    for (int i = 0; i < NTYPES; i++) {
        int sx = cx + PAL_X + i * (PAL_SW + PAL_GAP);
        int sy = cy + PAL_Y;
        fb_fill_rect(sx, sy, PAL_SW, PAL_SW, g_types[i].top);
        fb_fill_rect(sx, sy, PAL_SW, 1, g_types[i].left);        // slight top edge
        char ini[2] = { TYPE_INITIAL[i], '\0' };
        font_draw_string_trans(sx + (PAL_SW - FONT_WIDTH) / 2, sy + (PAL_SW - FONT_HEIGHT) / 2,
                               ini, fb_rgb(20, 20, 24));
        if (i == c->sel) {                                       // selected: bright frame
            uint32_t hl = fb_rgb(255, 240, 180);
            fb_fill_rect(sx - 2, sy - 2, PAL_SW + 4, 2, hl);
            fb_fill_rect(sx - 2, sy + PAL_SW, PAL_SW + 4, 2, hl);
            fb_fill_rect(sx - 2, sy - 2, 2, PAL_SW + 4, hl);
            fb_fill_rect(sx + PAL_SW, sy - 2, 2, PAL_SW + 4, hl);
        }
    }
    // Perf HUD (top-right) — the 3D-render performance-testing readout (P4).
    {
        int hx = cx + VOXEL_W - 152, hy = cy + 8;
        int hh = c->bench ? 84 : 52;
        char line[40];
        fb_fill_rect(hx - 8, hy - 4, 152, hh, fb_rgb(18, 14, 32));   // panel backdrop
        fb_fill_rect(hx - 8, hy - 4, 152, 1,  fb_rgb(70, 58, 104));  // top edge
        font_draw_string_trans(hx, hy, c->bench ? "BENCH  ON" : "BENCH  off",
                               c->bench ? fb_rgb(150, 240, 160) : fb_rgb(184, 178, 210));
        snprintf(line, sizeof(line), "cubes %d", c->last_cubes);
        font_draw_string_trans(hx, hy + 16, line, fb_rgb(222, 216, 242));
        snprintf(line, sizeof(line), "world %dx%d  v%d", VX_N, VX_N, c->view);
        font_draw_string_trans(hx, hy + 32, line, fb_rgb(222, 216, 242));
        if (c->bench) {
            snprintf(line, sizeof(line), "frame %u.%02u ms",
                     c->last_ft_us / 1000u, (c->last_ft_us % 1000u) / 10u);
            font_draw_string_trans(hx, hy + 52, line, fb_rgb(255, 232, 150));
            snprintf(line, sizeof(line), "render FPS %u", c->last_fps);
            font_draw_string_trans(hx, hy + 68, line, fb_rgb(255, 232, 150));
        }
    }
    font_draw_string_trans(cx + 10, cy + VOXEL_H - 22,
                           "L place  R break   1-6 pick  G gen  R rot  B bench", fb_rgb(182, 176, 208));
}

// Map a client click to the grid cell whose TOP face is under it (front-to-back,
// hit-testing each cell's top diamond at its current stacked height).
static int voxel_pick(voxel_ctx_t* c, int ox0, int oy0, int mx, int my, int* pgx, int* pgy) {
    for (int gy = VX_N - 1; gy >= 0; gy--)
        for (int gx = VX_N - 1; gx >= 0; gx--) {
            int sx, sy; rot_cell(c->view, gx, gy, &sx, &sy);   // screen slot -> data cell
            int top = c->height[sy][sx] - 1;                   // top block level
            int ox = ox0 + (gx - gy) * VX_W;
            int oy = oy0 + (gx + gy) * VX_T - top * VX_HH;
            int dcx = mx - ox, dcy = my - (oy + VX_T);
            if (dcx < 0) dcx = -dcx;
            if (dcy < 0) dcy = -dcy;
            if (dcx * VX_T + dcy * VX_W <= VX_W * VX_T) {
                *pgx = sx; *pgy = sy; return 1;                 // return the data cell to edit
            }
        }
    return 0;
}

void voxel_win_click(window_t* win, int mx, int my, int btn) {
    voxel_ctx_t* c = (voxel_ctx_t*)win->reserved;
    if (!c) return;
    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);

    // Palette bar first: a click on a swatch selects that block type.
    if (my >= cy + PAL_Y && my < cy + PAL_Y + PAL_SW) {
        int rel = mx - (cx + PAL_X);
        if (rel >= 0) {
            int i = rel / (PAL_SW + PAL_GAP);
            if (i >= 0 && i < NTYPES && rel - i * (PAL_SW + PAL_GAP) < PAL_SW) {
                c->sel = i; c->dirty = 1; return;
            }
        }
    }

    // Otherwise edit the terrain.
    int gx, gy;
    if (!voxel_pick(c, cx + VX_OX, cy + VX_OY, mx, my, &gx, &gy)) return;
    if (btn == 2) {                                  // right-click -> break the top block
        if (c->height[gy][gx] > 1) c->height[gy][gx]--;
    } else {                                         // left-click -> place the selected type
        int H = c->height[gy][gx];
        if (H < VX_MAXH) { c->col[gy][gx][H] = (signed char)c->sel; c->height[gy][gx] = (unsigned char)(H + 1); }
    }
    c->dirty = 1;
}

void voxel_win_key(window_t* win, int key) {
    voxel_ctx_t* c = (voxel_ctx_t*)win->reserved;
    if (!c) return;
    if (key >= '1' && key <= '0' + NTYPES) { c->sel = key - '1'; c->dirty = 1; }
    else if (key == 'g' || key == 'G') {           // regenerate a fresh procedural world
        c->seed = vx_hash(c->seed + 0x6d2b79f5u);
        voxel_worldgen(c, c->seed);
        c->dirty = 1;
    }
    else if (key == 'r' || key == 'R') {           // orbit the isometric camera 90 degrees
        c->view = (c->view + 1) & 3;
        c->dirty = 1;
    }
    else if (key == 'b' || key == 'B') {           // toggle the benchmark HUD (perf testbed)
        c->bench = !c->bench;
        c->dirty = 1;
    }
}

// Repaint once after an edit (the right-click path does not force a redraw, and
// the scene is otherwise static, so a dirty-gated tick covers clicks and keys).
int voxel_win_tick(window_t* win) {
    voxel_ctx_t* c = (voxel_ctx_t*)win->reserved;
    if (!c) return 0;
    if (c->bench) return 1;        // benchmark: repaint every tick for a live throughput read
    if (!c->dirty) return 0;
    c->dirty = 0;
    return 1;
}
