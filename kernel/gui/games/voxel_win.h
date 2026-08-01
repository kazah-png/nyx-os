#ifndef VOXEL_WIN_H
#define VOXEL_WIN_H
#include "../core/compositor.h"

// Nyx Voxels (working title — the final brand name is a user decision, like
// "Selene" was for the browser) — a step toward the playable Minecraft-class
// voxel-sandbox north star. v6.4.38 rendered a static isometric scene; v6.4.39
// made it interactive (place/break with the mouse); v6.4.40 adds BLOCK TYPES +
// a palette (grass/dirt/stone/wood/sand/water — pick with number keys 1-6 or by
// clicking the palette bar; left-click places the selected type, right-click
// breaks). A later increment adds a first-person camera. Grid is VX_N x VX_N.
#define VOXEL_W 480      // window client width
#define VOXEL_H 360      // window client height
#define VX_N    7        // heightmap grid side

void  voxel_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void  voxel_win_click(window_t* win, int mx, int my, int btn);
void  voxel_win_key(window_t* win, int key);
int   voxel_win_tick(window_t* win);
void* voxel_create_ctx(void);

#endif
