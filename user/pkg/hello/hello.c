#include "libc.h"

/* A sample package for the in-OS `pkg` manager. `pkg install hello` reads the recipe
 * next to this file, compiles this source with the in-OS `cc`, and installs the binary
 * to /mnt/bin/hello — run it as `/mnt/bin/hello`. */
int main(void) {
    printf("hello from a package installed by pkg, built in-OS with cc\n");
    return 7;   /* crt0 turns this into exit(7) */
}
