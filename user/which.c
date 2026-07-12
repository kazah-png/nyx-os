#include "libc.h"

/* which NAME... — print the full path each NAME would resolve to via $PATH, exactly
 * like the shell's command resolver: a name with '/' is checked as-is, otherwise each
 * ':'-separated $PATH directory is tried ("<dir>/<name>" then "<dir>/<name>.elf") and
 * the first hit is printed. Exit status is 1 if any name was not found. */

static int file_exists(const char* p) {
    long fd = open(p, O_RDONLY, 0);
    if (fd < 0) return 0;
    close((int)fd);
    return 1;
}

/* Resolve one name to `out` (empty string if not found). */
static void resolve(const char* name, char* out, int outsz) {
    out[0] = '\0';
    if (strchr(name, '/')) {                     /* explicit path */
        if (file_exists(name)) strncpy(out, name, outsz - 1), out[outsz - 1] = '\0';
        return;
    }
    const char* path = getenv("PATH");
    if (!path || !*path) return;
    for (const char* p = path; *p; ) {
        char dir[96]; int di = 0;
        while (*p && *p != ':' && di < (int)sizeof(dir) - 1) dir[di++] = *p++;
        dir[di] = '\0';
        while (*p == ':') p++;
        if (di == 0) continue;
        const char* sep = (dir[di - 1] == '/') ? "" : "/";
        char cand[160];
        snprintf(cand, sizeof(cand), "%s%s%s", dir, sep, name);
        if (file_exists(cand)) { strncpy(out, cand, outsz - 1); out[outsz - 1] = '\0'; return; }
        snprintf(cand, sizeof(cand), "%s%s%s.elf", dir, sep, name);
        if (file_exists(cand)) { strncpy(out, cand, outsz - 1); out[outsz - 1] = '\0'; return; }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: which NAME...\n"); return 1; }
    int status = 0;
    char out[160];
    for (int a = 1; a < argc; a++) {
        resolve(argv[a], out, sizeof(out));
        if (out[0]) printf("%s\n", out);
        else { printf("%s: not found\n", argv[a]); status = 1; }
    }
    return status;
}
