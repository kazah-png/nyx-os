#include "libc.h"
#include "syscall.h"

/* epoch — Unix timestamp converter (UTC). A small developer utility that turns a
 * `time_t` into a readable UTC date and back, built on the shared libc's calendar
 * routines (gmtime/strftime for seconds->date, timegm for date->seconds).
 *
 *   epoch                          print the current Unix time (seconds since 1970-01-01 UTC)
 *   epoch <seconds>                show the UTC date/time those seconds represent
 *   epoch <Y> <M> <D>              convert a UTC date (at 00:00:00) to Unix time
 *   epoch <Y> <M> <D> <h> <m> <s>  convert a full UTC date+time to Unix time
 *
 * Pure argv -> stdout; built INSIDE the OS by `cc` — installs via `xbm install epoch`.
 */

static int parse_long(const char* s, long* out) {
    char* end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return 0;    /* no digits, or trailing garbage */
    *out = v;
    return 1;
}

static void show_epoch(time_t t) {
    char buf[64];
    struct tm r;
    gmtime_r(&t, &r);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC (%A)", &r);
    printf("%s\n", buf);
}

int main(int argc, char** argv) {
    if (argc == 1) {                               /* current Unix time */
        struct timeval tv;
        if (gettimeofday(&tv, (void*)0) != 0) { printf("epoch: cannot read the clock\n"); return 1; }
        printf("%ld\n", tv.tv_sec);
        return 0;
    }

    if (argc == 2) {                               /* seconds -> UTC date */
        long n;
        if (!parse_long(argv[1], &n)) { printf("epoch: not a number: %s\n", argv[1]); return 1; }
        show_epoch((time_t)n);
        return 0;
    }

    if (argc == 4 || argc == 7) {                  /* UTC date -> seconds */
        long y, mo, d, h = 0, mi = 0, s = 0;
        int ok = parse_long(argv[1], &y) && parse_long(argv[2], &mo) && parse_long(argv[3], &d);
        if (argc == 7)
            ok = ok && parse_long(argv[4], &h) && parse_long(argv[5], &mi) && parse_long(argv[6], &s);
        if (!ok) { printf("epoch: date fields must be numbers\n"); return 1; }
        struct tm t;
        memset(&t, 0, sizeof t);
        t.tm_year = (int)y - 1900;
        t.tm_mon  = (int)mo - 1;
        t.tm_mday = (int)d;
        t.tm_hour = (int)h;
        t.tm_min  = (int)mi;
        t.tm_sec  = (int)s;
        printf("%ld\n", (long)timegm(&t));
        return 0;
    }

    printf("Usage: epoch                          (current Unix time)\n");
    printf("       epoch <seconds>                (Unix time -> UTC date)\n");
    printf("       epoch <Y> <M> <D>              (UTC date -> Unix time)\n");
    printf("       epoch <Y> <M> <D> <h> <m> <s>\n");
    return 1;
}
