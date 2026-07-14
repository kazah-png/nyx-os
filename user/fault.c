#include "libc.h"

/* A deliberately-crashing program. It dereferences a NULL pointer, which raises a
 * ring-3 page fault. Used to verify the kernel now KILLS just this process
 * (delivering the default SIGSEGV action, waitpid status 128+11=139) instead of
 * panicking the whole system — the shell and everything else keep running. The
 * pointer is volatile so the store isn't optimized away. */
int main(void) {
    printf("fault: about to write through a NULL pointer (expect SIGSEGV)...\n");
    volatile int* p = (volatile int*)0;
    *p = 0x42;                         /* ring-3 #PF: write to the unmapped zero page */
    printf("fault: STILL ALIVE — this line must never be reached\n");
    return 0;
}
