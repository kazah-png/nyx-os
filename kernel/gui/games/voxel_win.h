#ifndef VOXEL_WIN_H
#define VOXEL_WIN_H
#include "../core/compositor.h"

// Nyx Voxels (working title — the final brand name is a user decision, like
// "Selene" was for the browser) — a first scoped step toward the playable
// Minecraft-class voxel-sandbox north star (v6.4.38). This slice renders a STATIC
// isometric voxel scene (a small heightmap of shaded cubes over a Nyx night sky)
// in an in-kernel window; a later increment adds a first-person camera + input.
#define VOXEL_W 480      // window client width
#define VOXEL_H 360      // window client height

void voxel_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);

#endif
