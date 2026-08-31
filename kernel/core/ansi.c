// ============================================================
// ansi.c - ANSI/CSI escape-sequence helpers
// ============================================================
// The terminal's escape-sequence parser reads CSI numeric parameters (the N in ESC[Nm,
// the row/col in ESC[R;CH) one decimal digit at a time straight from a program's output.
// That output is untrusted — any program, or `cat` of any file, can drive it — so a run
// of digits like "ESC[99999999999999m" would, with the naive `p = p*10 + digit`, overflow
// the signed int holding the parameter. Signed overflow is undefined behavior, and even
// where a later clamp saves the write, a negative/garbage parameter is fragile. This
// helper folds each digit with a hard saturation at CSI_PARAM_MAX, so the value stays a
// small non-negative int no matter how many digits arrive. Correctness is pinned by
// ansi_csi_selftest() (the CI `ansicsi` KAT). Pure logic — no state, no allocation.
#include "kernel.h"
#include "ansi.h"

int csi_param_accum(int cur, int digit) {
    if (cur < 0) cur = 0;                          // defensive: parameters are non-negative
    if (digit < 0 || digit > 9) return cur;        // ignore a non-digit (parser shouldn't pass one)
    if (cur > (CSI_PARAM_MAX - digit) / 10)        // cur*10 + digit would exceed the cap...
        return CSI_PARAM_MAX;                      // ...so saturate instead of overflowing
    return cur * 10 + digit;
}

// ---- Known-answer test --------------------------------------------------------------
int ansi_csi_selftest(void) {
    // Normal accumulation of legitimate parameters.
    int v = 0;
    v = csi_param_accum(v, 1); v = csi_param_accum(v, 2); v = csi_param_accum(v, 3);
    if (v != 123) return 1;                        // "123"
    if (csi_param_accum(0, 0) != 0) return 2;      // "0"
    { int a = csi_param_accum(0, 8); a = csi_param_accum(a, 0); if (a != 80) return 3; }  // "80"

    // Saturation: a long digit run never overflows, never goes negative, caps at MAX.
    { int a = 0; for (int i = 0; i < 40; i++) a = csi_param_accum(a, 9);
      if (a != CSI_PARAM_MAX) return 4;
      if (a < 0) return 5; }

    // Exact boundary: 6553 then '5' lands on the cap; further digits stay pinned.
    { int a = 6553; a = csi_param_accum(a, 5); if (a != CSI_PARAM_MAX) return 6;
      a = csi_param_accum(a, 0); if (a != CSI_PARAM_MAX) return 7; }

    // Just under the guard still accumulates exactly: 6552*10 + 9 = 65529, under the cap.
    if (csi_param_accum(6552, 9) != 65529) return 8;

    // A stray non-digit is a no-op (defensive contract).
    if (csi_param_accum(12, 99) != 12) return 9;
    if (csi_param_accum(12, -1) != 12) return 10;
    return 0;
}
