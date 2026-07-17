#include "libc.h"

/* stacktest — exercise v5.8.85 demand-grown user stacks + the guard page.
 *
 * Each frame parks a ~4 KB touched buffer on the stack, so N frames of recursion use
 * ~N*4 KB of stack. The old kernel committed a fixed 64 KB and #PF'd (→ SIGSEGV) past
 * it; now only a few pages are committed at exec and vm_handle_fault grows the rest on
 * demand up to a 512 KB ceiling, with a guard page below it.
 *
 *   stacktest         -> ~192 KB: was fatal at 64 KB, now SURVIVES (lazy growth)
 *   stacktest <frames>-> pass e.g. 200 (~800 KB) to blow past the 512 KB ceiling and
 *                        hit the guard page: the kernel prints "[stack overflow]" and
 *                        the process dies with SIGSEGV (status 139) — caught cleanly,
 *                        not silent corruption.
 *
 * The recursion is deliberately NOT tail-recursive (the buffer is read AFTER the
 * recursive call) so -Os cannot fold it into a loop that wouldn't grow the stack.
 */
static long recurse(int depth, int frames) {
    volatile char buf[4000];
    buf[0]    = (char)depth;
    buf[3999] = (char)(depth ^ 0x5a);
    if (depth < frames) {
        long r = recurse(depth + 1, frames);
        return r + buf[0] + buf[3999];      /* use buf AFTER the call: no tail-call */
    }
    return (long)buf[0] + buf[3999];
}

int main(int argc, char** argv) {
    int frames = 48;                        /* ~192 KB — well past the old 64 KB stack */
    if (argc > 1) frames = atoi(argv[1]);
    if (frames < 1) frames = 1;
    printf("stacktest: recursing %d frames (~%d KB of stack; old fixed limit was 64 KB)...\n",
           frames, frames * 4);
    long s = recurse(0, frames);
    printf("stacktest: SURVIVED — used ~%d KB of demand-grown stack (checksum %ld)\n",
           frames * 4, s);
    return 0;
}
