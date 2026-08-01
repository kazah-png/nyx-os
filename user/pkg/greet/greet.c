#include "libc.h"

/* A sample package for the NyxOS `xbm` package manager that showcases install-and-run.
 * `xbm install greet` compiles this source with the in-OS `cc` and installs the binary
 * to /mnt/bin/greet. Because /mnt/bin is on the shell's PATH-like search, you then run
 * the installed program just by typing its name: `greet`. */
int main(void) {
    printf("greet: installed by xbm, run by bare name from /mnt/bin\n");
    return 9;   /* crt0 turns this into exit(9) */
}
