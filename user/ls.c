#include "libc.h"

/* ls — list a directory (default "/"). Enumeration lives in the kernel: one
 * getdents() call does the whole vfs_open/readdir/close cycle and fills an array
 * of nyx_dirent_t. Flags (combinable, e.g. -la):
 *   -a  show entries whose name starts with '.' (hidden by default)
 *   -l  long format: a type char (d/-) + byte size + name (one stat() per entry)
 * Directories are shown with a trailing '/'; without -l, dirs are bright blue and
 * .elf files bright green.
 *
 * The buffer is malloc()'d and then memset() to zero BEFORE getdents(): with the
 * v5.8.9 lazy-sbrk heap a fresh malloc only faults in the page holding the block
 * header, and the kernel's copy_to_user cannot fault the rest in — so we touch
 * every page here to make the whole buffer resident first. */

#define MAX_ENTS 128

int main(int argc, char** argv) {
    int show_all = 0, long_fmt = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'a') show_all = 1;
            else if (*f == 'l') long_fmt = 1;
            else { printf("ls: invalid option -%c\n", *f); return 2; }
        }
    const char* path = (ai < argc) ? argv[ai] : "/";

    nyx_dirent_t* ents = (nyx_dirent_t*)malloc(sizeof(nyx_dirent_t) * MAX_ENTS);
    if (!ents) { printf("ls: out of memory\n"); return 1; }
    memset(ents, 0, sizeof(nyx_dirent_t) * MAX_ENTS);   /* make every page resident */

    long n = getdents(path, ents, MAX_ENTS);
    if (n < 0) { printf("ls: cannot access '%s'\n", path); free(ents); return 1; }

    for (long i = 0; i < n; i++) {
        const char* nm = ents[i].name;
        if (!show_all && nm[0] == '.') continue;
        int isd = (ents[i].type == 1);

        if (long_fmt) {
            unsigned int sz = 0;
            if (!isd) {
                char ep[320];
                if (path[0] == '/' && path[1] == '\0') snprintf(ep, sizeof(ep), "/%s", nm);
                else                                   snprintf(ep, sizeof(ep), "%s/%s", path, nm);
                struct stat st;
                if (stat(ep, &st) == 0) sz = st.st_size;
            }
            printf("%c %8u  %s\n", isd ? 'd' : '-', sz, nm);
            continue;
        }

        if (isd) {                                   /* directory -> bright blue */
            printf("\x1b[1;34m%s/\x1b[0m\n", nm);
        } else {
            /* executables (.elf) -> bright green, everything else default */
            int L = 0; while (nm[L]) L++;
            int is_elf = (L >= 4 && nm[L-4] == '.' && nm[L-3] == 'e' && nm[L-2] == 'l' && nm[L-1] == 'f');
            if (is_elf) printf("\x1b[1;32m%s\x1b[0m\n", nm);
            else        printf("%s\n", nm);
        }
    }
    free(ents);
    return 0;
}
