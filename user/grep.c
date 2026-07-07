#include "libc.h"

/* grep — print input lines that contain PATTERN (a literal substring match).
 * Usage: grep PATTERN [file...].  With no file it reads stdin, so it slots into a
 * pipeline (`ls / | grep elf`). Exit status 0 if any line matched, 1 if none. When
 * more than one file is given, matches are prefixed with "file:". */

static int matched_any;

static void emit(const char* name, int show_name, const char* line) {
    if (show_name) { write(1, name, strlen(name)); write(1, ":", 1); }
    write(1, line, strlen(line));
    write(1, "\n", 1);
    matched_any = 1;
}

/* Scan a stream line by line, printing lines that contain `pat`. */
static void grep_fd(int fd, const char* pat, const char* name, int show_name) {
    char buf[512], line[512];
    int li = 0;
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || li >= (int)sizeof(line) - 1) {
                line[li] = '\0';
                if (strstr(line, pat)) emit(name, show_name, line);
                li = 0;
                if (c != '\n') line[li++] = c;   /* start of a too-long line's tail */
            } else {
                line[li++] = c;
            }
        }
    }
    if (li > 0) {                                /* last line without a trailing newline */
        line[li] = '\0';
        if (strstr(line, pat)) emit(name, show_name, line);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: grep PATTERN [file...]\n"); return 2; }
    const char* pat = argv[1];

    if (argc == 2) {                             /* no files: filter stdin */
        grep_fd(0, pat, "", 0);
    } else {
        int show_name = (argc > 3);
        for (int i = 2; i < argc; i++) {
            long fd = open(argv[i], O_RDONLY, 0);
            if (fd < 0) { printf("grep: %s: not found\n", argv[i]); continue; }
            grep_fd((int)fd, pat, argv[i], show_name);
            close(fd);
        }
    }
    return matched_any ? 0 : 1;
}
