#include "libc.h"

/* find — walk a directory tree and print every path, optionally only those whose
 * name contains a substring: `find [DIR] [PATTERN]`. Recursion uses getdents with
 * a per-level static entry pool (static: getdents' copy_to_user cannot fault
 * untouched lazy-heap pages in), capped at a sane depth. */

#define MAX_DEPTH 8
#define MAX_ENTS  64

static nyx_dirent_t ents[MAX_DEPTH][MAX_ENTS];   /* one pool per recursion level */
static const char* pattern;

static void walk(const char* dir, int depth) {
    if (depth >= MAX_DEPTH) return;
    long n = getdents(dir, ents[depth], MAX_ENTS);
    if (n < 0) return;
    for (long i = 0; i < n; i++) {
        char path[192];
        const char* sep = (dir[strlen(dir) - 1] == '/') ? "" : "/";
        snprintf(path, sizeof(path), "%s%s%s", dir, sep, ents[depth][i].name);
        if (!pattern || strstr(ents[depth][i].name, pattern)) {
            write(1, path, strlen(path));
            if (ents[depth][i].type == 1) write(1, "/", 1);
            write(1, "\n", 1);
        }
        if (ents[depth][i].type == 1)             /* directory: recurse */
            walk(path, depth + 1);
    }
}

int main(int argc, char** argv) {
    const char* dir = (argc >= 2) ? argv[1] : ".";
    pattern = (argc >= 3) ? argv[2] : 0;
    walk(dir, 0);
    return 0;
}
