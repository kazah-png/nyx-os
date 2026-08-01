/* hello_cc.c — a tiny self-host proof fixture.
 *
 * This program is compiled IN-OS by `tcc_self.elf` — a tcc that was itself built
 * by the in-OS tcc (see the tcc-compiles-tcc arc). If it prints its line and exits
 * 42, then a compiler produced by NyxOS's own compiler has, in turn, compiled and
 * produced a working new program: the self-host loop demonstrated end to end.
 *
 * It uses only the NyxOS libc (shipped at /usr/src/nyx/libc.h), so `tcc_self.elf`
 * compiles it with `-I/usr/src/nyx` exactly the way the `cc` builtin drives tcc.
 */
#include "libc.h"

int main(void) {
    int a = 6, b = 7;
    printf("hello from a program built in-OS by the self-built tcc: %d*%d=%d\n", a, b, a * b);
    return a * b;   /* crt0 turns this into exit(42) */
}
