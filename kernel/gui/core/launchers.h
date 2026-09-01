#ifndef NYX_GUI_LAUNCHERS_H
#define NYX_GUI_LAUNCHERS_H
// Desktop window launchers — the public entry points that open a top-level window (a game,
// an app, or a P4 render-perf demo). Every one is implemented in gui/core/compositor.c.
//
// These were split out of the core/kernel.h god-header as one cohesive module boundary (an
// incremental per-subsystem header split — see the architecture-modularity thread). kernel.h
// re-includes this header, so all existing includers keep seeing the prototypes unchanged;
// new callers that only open windows can include just this focused header instead of the
// whole kernel.h. Prototypes only (void / const char*), so this header has no dependencies.

void launch_doom_windowed(void);        // run DOOM inside a compositor window
void launch_pong(void);                 // open a Pong game window
void launch_snake(void);                // open a Snake game window
void launch_tetris(void);               // open a Tetris game window
void launch_minesweeper(void);          // open the Minesweeper window
void launch_games_folder(void);         // open the "Games" desktop folder
void launch_selene(void);               // open Selene, the web browser
void launch_voxel(void);                // open Nyx Voxels, the voxel-scene window
void launch_fire(void);                 // open Nyx Fire, the animated doom-fire window
void launch_matrix(void);               // open Nyx Matrix, the green code-rain window
void launch_lava(void);                 // open Nyx Lava, the animated plasma window
void launch_nyxflex(void);              // open Nyx Flex, the 4-quadrant matrix+lava+terminal+DOOM showcase
void launch_mandel(void);               // open Nyx Fractal, the Mandelbrot perf-demo window
void launch_julia(void);                // open Nyx Julia, the Julia-set perf-demo window
void launch_particles(void);            // open Nyx Particles, the particle-fountain perf-demo window
void launch_rotor(void);                // open Nyx Rotor, the spinning-3D-lattice perf-demo window
void launch_fill(void);                 // open Nyx Fill, the 2D fill-rate/overdraw perf-demo window
void launch_life(void);                 // open Nyx Life, the Game-of-Life compute/render perf-demo window
void launch_blobs(void);                // open Nyx Blobs, the metaballs render-perf-demo window
void launch_imageview(const char* path);// open the Image Viewer, optionally on a file
void launch_fileman(const char* path);  // open the File Manager, optionally at a directory

#endif
