#include "libc.h"

/* whoami — print the current user, read from the $USER environment variable (seeded
 * by the shell and inherited through execve). Falls back to "nyx" if unset. */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const char* u = getenv("USER");
    printf("%s\n", (u && *u) ? u : "nyx");
    return 0;
}
