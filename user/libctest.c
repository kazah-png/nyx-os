#include "libc.h"

/* NyxOS toolchain self-test (Phase 2 — in-OS compiler groundwork).
 * Exercises the libc primitives that a C compiler (and most nontrivial
 * programs) need. Prints one PASS/FAIL line per function to stdout, which the
 * kernel routes to the serial console at boot. This file grows as the libc
 * grows toward hosting the compiler. */
static int cmp_int(const void* a, const void* b) {
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int ok = 1;

    /* realloc: grow a buffer; old contents preserved, grown region writable. */
    char* p = (char*)malloc(8);
    for (int i = 0; i < 8; i++) p[i] = (char)('A' + i);
    p = (char*)realloc(p, 64);
    int r_ok = (p != 0);
    for (int i = 0; i < 8; i++) if (p[i] != (char)('A' + i)) r_ok = 0;
    if (p) p[63] = 'Z';
    printf("LIBCTEST: realloc %s\n", r_ok ? "PASS" : "FAIL");
    if (!r_ok) ok = 0;
    free(p);

    /* calloc: zero-filled allocation. */
    int* z = (int*)calloc(16, sizeof(int));
    int c_ok = (z != 0);
    if (z) for (int i = 0; i < 16; i++) if (z[i] != 0) c_ok = 0;
    printf("LIBCTEST: calloc %s\n", c_ok ? "PASS" : "FAIL");
    if (!c_ok) ok = 0;
    free(z);

    /* memmove: overlapping forward shift (a plain memcpy would corrupt this). */
    char buf[16];
    for (int i = 0; i < 16; i++) buf[i] = (char)('0' + i);
    memmove(buf + 2, buf, 10);
    int m_ok = 1;
    for (int i = 0; i < 10; i++) if (buf[i + 2] != (char)('0' + i)) m_ok = 0;
    printf("LIBCTEST: memmove %s\n", m_ok ? "PASS" : "FAIL");
    if (!m_ok) ok = 0;

    /* File I/O wrappers (open/write/stat/lseek/read) on the persistent /mnt mount:
     * write a file, stat it (size + regular-file mode), then reopen, seek past the
     * first 7 bytes and read the tail back, comparing it to the original. */
    {
        const char* path = "/mnt/libctest_io.txt";
        const char* msg  = "NyxOS toolchain file I/O test\n";
        long mlen = (long)strlen(msg);
        int io_ok = 1;
        int fd = (int)open(path, O_CREAT | O_TRUNC, 0644);   /* NyxOS: bit0=O_CREAT, no access mode */
        if (fd < 0) io_ok = 0;
        else { if (write(fd, msg, mlen) != mlen) io_ok = 0; close(fd); }

        struct stat st;
        if (io_ok && (stat(path, &st) != 0 || st.st_size != (unsigned)mlen || !S_ISREG(st.st_mode))) io_ok = 0;

        fd = (int)open(path, O_RDONLY, 0);
        if (fd < 0) io_ok = 0;
        else {
            char rb[64];
            if (lseek(fd, 7, SEEK_SET) != 7) io_ok = 0;
            long n = read(fd, rb, (long)sizeof(rb) - 1);
            if (n < 0) { io_ok = 0; n = 0; }
            rb[n] = '\0';
            if (io_ok && (n != mlen - 7 || memcmp(rb, msg + 7, (unsigned long)n) != 0)) io_ok = 0;
            close(fd);
        }
        printf("LIBCTEST: fileio(open/write/stat/lseek/read) %s\n", io_ok ? "PASS" : "FAIL");
        if (!io_ok) ok = 0;
    }

    /* ctype (character classification/conversion) — a lexer's staples. */
    {
        int ct_ok = isalpha('A') && isalpha('z') && !isalpha('7') &&
                    isdigit('0') && isdigit('9') && !isdigit('x') &&
                    isspace(' ') && isspace('\n') && !isspace('a') &&
                    isxdigit('f') && isxdigit('C') && !isxdigit('g') &&
                    isalnum('q') && isalnum('5') && !isalnum('#') &&
                    ispunct('#') && !ispunct('a') &&
                    toupper('a') == 'A' && tolower('Z') == 'z' && toupper('7') == '7';
        printf("LIBCTEST: ctype %s\n", ct_ok ? "PASS" : "FAIL");
        if (!ct_ok) ok = 0;
    }

    /* strtol / strtoul — string to integer, with sign/base/prefix handling. */
    {
        char* end = 0;
        long a = strtol("  -42xyz", &end, 10);           /* signed decimal, stops at 'x' */
        int s_ok = (a == -42) && end && (*end == 'x');
        s_ok = s_ok && (strtoul("0x1A", 0, 0) == 26);    /* base 0 auto-detects hex */
        s_ok = s_ok && (strtol("777", 0, 8) == 511);     /* explicit octal */
        printf("LIBCTEST: strtol/strtoul %s\n", s_ok ? "PASS" : "FAIL");
        if (!s_ok) ok = 0;
    }

    /* string/stdlib extras: memchr, strrchr, strncat, strdup, qsort. */
    {
        int se_ok = 1;
        const char* hay = "abc/def/ghi";
        if (strrchr(hay, '/') != hay + 7) se_ok = 0;        /* LAST '/' */
        if (memchr(hay, 'd', 11) != hay + 4) se_ok = 0;     /* first 'd' */
        if (memchr(hay, 'z', 11) != 0) se_ok = 0;           /* absent */

        char nb[16]; nb[0] = '\0';
        strncat(nb, "foo", 16);
        strncat(nb, "bar", 2);                              /* bounded: only "ba" */
        if (strcmp(nb, "fooba") != 0) se_ok = 0;

        char* dup = strdup("duplicate me");
        if (!dup || strcmp(dup, "duplicate me") != 0) se_ok = 0;
        free(dup);

        int arr[8] = { 5, 2, 9, 1, 7, 3, 8, 4 };
        qsort(arr, 8, sizeof(int), cmp_int);
        for (int i = 0; i < 7; i++) if (arr[i] > arr[i + 1]) se_ok = 0;
        if (arr[0] != 1 || arr[7] != 9) se_ok = 0;

        printf("LIBCTEST: str/stdlib(memchr/strrchr/strncat/strdup/qsort) %s\n", se_ok ? "PASS" : "FAIL");
        if (!se_ok) ok = 0;
    }

    printf("LIBCTEST: %s\n", ok ? "ALL PASS" : "SOME FAIL");
    return ok ? 0 : 1;
}
