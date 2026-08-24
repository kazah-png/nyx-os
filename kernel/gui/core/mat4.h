#ifndef NYX_MAT4_H
#define NYX_MAT4_H
#include "../../core/kernel.h"

// Fixed-point 4x4 matrix + vec4 math for the software 3D pipeline (SM64 video backend / Nyx
// Voxels perf study). Everything is Q16.16 (FP_ONE = 65536) and integer-only, since the
// kernel is built -mno-sse (no floats). Row-major storage: m[row*4 + col]. Vectors are
// int32[4] {x,y,z,w} in Q16. This is P0b rung 5: transform → project feeding the rasterizer.

#define FP_SHIFT 16
#define FP_ONE   (1 << FP_SHIFT)

typedef struct { int32_t m[16]; } mat4;

// Q16 fixed-point multiply: (a*b) with the 2^16 scale divided back out, via a 64-bit product.
static inline int32_t fpmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> FP_SHIFT); }

int  msin(int a);   // Q16 sine, phase 0..255 -> -65536..65536 (parabolic; exact at 0/64/128/192)
int  mcos(int a);

mat4 mat4_identity(void);
mat4 mat4_mul(const mat4* a, const mat4* b);                       // returns a*b
void mat4_mul_vec4(const mat4* m, const int32_t* v, int32_t* out); // out = m*v (Q16 4-vectors)
mat4 mat4_translate(int32_t tx, int32_t ty, int32_t tz);           // Q16 offsets
mat4 mat4_rotate_x(int a);                                         // angle a in 0..255
mat4 mat4_rotate_y(int a);

int mat4_selftest(void);   // KAT for the matrix/vector math

#endif
