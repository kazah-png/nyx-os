#include "libc.h"

/* grep — print input lines that contain PATTERN (a literal substring match).
 * Usage: grep [-i] [-n] [-v] [-c] PATTERN [file...].  With no file it reads stdin,
 * so it slots into a pipeline (`ls / | grep elf`). Flags (combinable, e.g. -in):
 *   -i  ignore case      -n  prefix each line with its 1-based number
 *   -v  invert: print the lines that do NOT match
 *   -c  print only a COUNT of selected lines (per file), not the lines
 * Exit status 0 if any line was selected, 1 if none. When more than one file is
 * given, output is prefixed with "file:". */

static int matched_any;
static int opt_i, opt_n, opt_v, opt_c;
static const char* opt_pat;

/* Write a non-negative int to stdout (no snprintf dependency for the -n prefix). */
static void put_uint(int v) {
    char t[16]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) write(1, &t[--n], 1);
}

/* Substring test: plain strstr, or a case-folding strcasestr when -i is set. ASCII. */
static int ci_match(const char* hay, const char* needle) {
    return (opt_i ? strcasestr(hay, needle) : strstr(hay, needle)) != 0;
}

static void emit(const char* name, int show_name, int lineno, const char* line) {
    if (show_name) { write(1, name, strlen(name)); write(1, ":", 1); }
    if (opt_n) { put_uint(lineno); write(1, ":", 1); }
    write(1, line, strlen(line));
    write(1, "\n", 1);
    matched_any = 1;
}

/* A line is selected when its match state differs from -v. With -c we only count
 * selected lines; otherwise each is printed. Sets matched_any for the exit status. */
static int select_line(const char* name, int show_name, int lineno, const char* line) {
    if (ci_match(line, opt_pat) == opt_v) return 0;   /* not selected */
    matched_any = 1;
    if (!opt_c) emit(name, show_name, lineno, line);
    return 1;
}

/* Scan a stream line by line; returns the count of selected lines (for -c). */
static int grep_fd(int fd, const char* pat, const char* name, int show_name) {
    opt_pat = pat;
    char buf[512], line[512];
    int li = 0, lineno = 0, cnt = 0;
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || li >= (int)sizeof(line) - 1) {
                line[li] = '\0';
                lineno++;
                cnt += select_line(name, show_name, lineno, line);
                li = 0;
                if (c != '\n') line[li++] = c;   /* start of a too-long line's tail */
            } else {
                line[li++] = c;
            }
        }
    }
    if (li > 0) {                                /* last line without a trailing newline */
        line[li] = '\0';
        lineno++;
        cnt += select_line(name, show_name, lineno, line);
    }
    return cnt;
}

int main(int argc, char** argv) {
    int ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'i') opt_i = 1;
            else if (*f == 'n') opt_n = 1;
            else if (*f == 'v') opt_v = 1;
            else if (*f == 'c') opt_c = 1;
            else { printf("grep: invalid option -%c\n", *f); return 2; }
        }
    if (ai >= argc) { printf("usage: grep [-invc] PATTERN [file...]\n"); return 2; }
    const char* pat = argv[ai++];

    if (ai >= argc) {                            /* no files: filter stdin */
        int c = grep_fd(0, pat, "", 0);
        if (opt_c) { put_uint(c); write(1, "\n", 1); }
    } else {
        int show_name = (argc - ai > 1);
        for (int i = ai; i < argc; i++) {
            long fd = open(argv[i], O_RDONLY, 0);
            if (fd < 0) { printf("grep: %s: not found\n", argv[i]); continue; }
            int c = grep_fd((int)fd, pat, argv[i], show_name);
            if (opt_c) {                         /* -c: one count line per file (GNU prints 0 too) */
                if (show_name) { write(1, argv[i], strlen(argv[i])); write(1, ":", 1); }
                put_uint(c); write(1, "\n", 1);
            }
            close(fd);
        }
    }
    return matched_any ? 0 : 1;
}
