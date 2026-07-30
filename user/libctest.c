#include "libc.h"

/* NyxOS toolchain self-test (Phase 2 — in-OS compiler groundwork).
 * Exercises the libc primitives that a C compiler (and most nontrivial
 * programs) need. Prints one PASS/FAIL line per function to stdout, which the
 * kernel routes to the serial console at boot. This file grows as the libc
 * grows toward hosting the compiler. */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int ok = 1;

    /* realloc: grow a buffer; old contents preserved, grown region writable. */
    char* p = (char*)malloc(8);
    for (int i = 0; i < 8; i++) p[i] = (char)('A' + i);
    p = (char*)realloc(p, 64);
    int r_ok = (p != 0);
    for (int i = 0; i < 8; i++) if (p[i] != (char)('A' + i)) r_ok = 0;
    if (p) p[63] = 'Z';
    printf("LIBCTEST: realloc %s\n", r_ok ? "PASS" : "FAIL");
    if (!r_ok) ok = 0;
    free(p);

    /* calloc: zero-filled allocation. */
    int* z = (int*)calloc(16, sizeof(int));
    int c_ok = (z != 0);
    if (z) for (int i = 0; i < 16; i++) if (z[i] != 0) c_ok = 0;
    printf("LIBCTEST: calloc %s\n", c_ok ? "PASS" : "FAIL");
    if (!c_ok) ok = 0;
    free(z);

    /* memmove: overlapping forward shift (a plain memcpy would corrupt this). */
    char buf[16];
    for (int i = 0; i < 16; i++) buf[i] = (char)('0' + i);
    memmove(buf + 2, buf, 10);
    int m_ok = 1;
    for (int i = 0; i < 10; i++) if (buf[i + 2] != (char)('0' + i)) m_ok = 0;
    printf("LIBCTEST: memmove %s\n", m_ok ? "PASS" : "FAIL");
    if (!m_ok) ok = 0;

    printf("LIBCTEST: %s\n", ok ? "ALL PASS" : "SOME FAIL");
    return ok ? 0 : 1;
}
