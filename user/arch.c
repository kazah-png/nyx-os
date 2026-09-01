#include "libc.h"

/* arch — print machine hardware name (equivalent to `uname -m`).
 * Queries CPUID at runtime instead of hard-coding: leaf 0x80000001 EDX[29]
 * is the Long Mode bit — set iff the CPU is 64-bit capable. Falls back to
 * "x86_64" for the NyxOS target if CPUID is unavailable. */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    unsigned int eax, ebx, ecx, edx;
    unsigned int max_ext = 0;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    max_ext = eax;
    if (max_ext >= 0x80000001) {
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001));
        if (edx & (1u << 29)) {
            printf("x86_64\n");
            return 0;
        }
        printf("i686\n");
        return 0;
    }

    /* No extended CPUID — still x86, report 32-bit. */
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    (void)eax; (void)ebx; (void)ecx; (void)edx;
    printf("x86_64\n");
    return 0;
}
