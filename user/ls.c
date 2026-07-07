#include "libc.h"

/* ls — list a directory (default "/"). Enumeration lives in the kernel: one
 * getdents() call does the whole vfs_open/readdir/close cycle and fills an array
 * of nyx_dirent_t. Directories are shown with a trailing '/'.
 *
 * The buffer is malloc()'d and then memset() to zero BEFORE getdents(): with the
 * v5.8.9 lazy-sbrk heap a fresh malloc only faults in the page holding the block
 * header, and the kernel's copy_to_user cannot fault the rest in — so we touch
 * every page here to make the whole buffer resident first. */

#define MAX_ENTS 128

int main(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : "/";

    nyx_dirent_t* ents = (nyx_dirent_t*)malloc(sizeof(nyx_dirent_t) * MAX_ENTS);
    if (!ents) { printf("ls: out of memory\n"); return 1; }
    memset(ents, 0, sizeof(nyx_dirent_t) * MAX_ENTS);   /* make every page resident */

    long n = getdents(path, ents, MAX_ENTS);
    if (n < 0) { printf("ls: cannot access '%s'\n", path); free(ents); return 1; }

    for (long i = 0; i < n; i++) {
        if (ents[i].type == 1) printf("%s/\n", ents[i].name);   /* directory */
        else                   printf("%s\n",  ents[i].name);
    }
    free(ents);
    return 0;
}
