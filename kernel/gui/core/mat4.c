#include "mat4.h"

// Parabolic Q16 sine (adapted from rotor_win's Q12 isin): phase 0..255, peak 65536 at 64.
// Exact at the cardinal phases: msin(0)=0, msin(64)=65536, msin(128)=0, msin(192)=-65536.
int msin(int a) {
    a &= 255;
    int s = 1;
    if (a >= 128) { a -= 128; s = -1; }
    return s * (a * (128 - a)) * 16;    // 64*64=4096, *16 = 65536 = 1.0 in Q16
}
int mcos(int a) { return msin(a + 64); }

mat4 mat4_identity(void) {
    mat4 r;
    for (int i = 0; i < 16; i++) r.m[i] = 0;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = FP_ONE;
    return r;
}

mat4 mat4_mul(const mat4* a, const mat4* b) {
    mat4 r;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 4; col++) {
            int64_t s = 0;
            for (int k = 0; k < 4; k++)
                s += (int64_t)a->m[row * 4 + k] * b->m[k * 4 + col];
            r.m[row * 4 + col] = (int32_t)(s >> FP_SHIFT);
        }
    return r;
}

void mat4_mul_vec4(const mat4* m, const int32_t* v, int32_t* out) {
    for (int row = 0; row < 4; row++) {
        int64_t s = 0;
        for (int k = 0; k < 4; k++)
            s += (int64_t)m->m[row * 4 + k] * v[k];
        out[row] = (int32_t)(s >> FP_SHIFT);
    }
}

mat4 mat4_translate(int32_t tx, int32_t ty, int32_t tz) {
    mat4 r = mat4_identity();
    r.m[3] = tx; r.m[7] = ty; r.m[11] = tz;   // translation in the last column (row-major)
    return r;
}

mat4 mat4_rotate_x(int a) {
    int c = mcos(a), s = msin(a);
    mat4 r = mat4_identity();
    r.m[5] = c;  r.m[6] = -s;
    r.m[9] = s;  r.m[10] = c;
    return r;
}

mat4 mat4_rotate_y(int a) {
    int c = mcos(a), s = msin(a);
    mat4 r = mat4_identity();
    r.m[0] = c;  r.m[2] = s;
    r.m[8] = -s; r.m[10] = c;
    return r;
}

int mat4_selftest(void) {
    // msin exact at cardinal phases.
    if (msin(0) != 0 || msin(64) != FP_ONE || msin(128) != 0 || msin(192) != -FP_ONE) return 1;

    // identity * v = v.
    mat4 I = mat4_identity();
    int32_t v[4] = {3 * FP_ONE, -5 * FP_ONE, 7 * FP_ONE, FP_ONE}, o[4];
    mat4_mul_vec4(&I, v, o);
    if (o[0] != 3 * FP_ONE || o[1] != -5 * FP_ONE || o[2] != 7 * FP_ONE || o[3] != FP_ONE) return 2;

    // translate(2,3,4) moves a point by (2,3,4).
    mat4 T = mat4_translate(2 * FP_ONE, 3 * FP_ONE, 4 * FP_ONE);
    mat4_mul_vec4(&T, v, o);
    if (o[0] != 5 * FP_ONE || o[1] != -2 * FP_ONE || o[2] != 11 * FP_ONE || o[3] != FP_ONE) return 3;

    // translate(1,0,0) * translate(2,0,0) == translate(3,0,0).
    mat4 T1 = mat4_translate(FP_ONE, 0, 0), T2 = mat4_translate(2 * FP_ONE, 0, 0);
    mat4 T3 = mat4_mul(&T1, &T2);
    if (T3.m[3] != 3 * FP_ONE || T3.m[0] != FP_ONE || T3.m[15] != FP_ONE) return 4;

    // rotate_y(90deg = phase 64): (1,0,0) -> (0,0,-1).
    mat4 Ry = mat4_rotate_y(64);
    int32_t xaxis[4] = {FP_ONE, 0, 0, FP_ONE};
    mat4_mul_vec4(&Ry, xaxis, o);
    if (o[0] != 0 || o[1] != 0 || o[2] != -FP_ONE || o[3] != FP_ONE) return 5;

    // rotate_x(90deg): (0,1,0) -> (0,0,1).
    mat4 Rx = mat4_rotate_x(64);
    int32_t yaxis[4] = {0, FP_ONE, 0, FP_ONE};
    mat4_mul_vec4(&Rx, yaxis, o);
    if (o[0] != 0 || o[1] != 0 || o[2] != FP_ONE || o[3] != FP_ONE) return 6;

    return 0;
}
