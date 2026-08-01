#include "libc.h"

/* A sample package for the in-OS `xbm` manager. `xbm install hello` reads the recipe
 * next to this file, compiles this source with the in-OS `cc`, and installs the binary
 * to /mnt/bin/hello. (`hello` is also a base system binary at /hello.elf, which wins the
 * bare-name search, so run this installed one as `/mnt/bin/hello`; the `greet` package
 * has no such clash and shows off the run-by-bare-name path.) */
int main(void) {
    printf("hello from a package installed by xbm, built in-OS with cc\n");
    return 7;   /* crt0 turns this into exit(7) */
}
