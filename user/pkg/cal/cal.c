#include "libc.h"

/* cal — print a month (or year) calendar, Sunday-first, in the style of the classic
 * `cal`. With no arguments it prints the current month (from the kernel RTC); `cal <year>`
 * prints all twelve months; `cal <month> <year>` prints one month (month 1-12).
 *
 *   cal              current month
 *   cal 9 2026       September 2026
 *   cal 2026         the whole year
 *
 * Pure argv -> stdout; built INSIDE the OS by `cc` — installs via `xbm install cal`.
 */

static const char* MON[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};

static int is_leap(int y) { return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)); }
static int month_days(int m, int y) {
    static const int d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

/* Days since 1970-01-01 for y-m-d (Howard Hinnant's days_from_civil). */
static long days_from_civil(int y, int m, int d) {
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
static int weekday(int y, int m, int d) {   /* 0 = Sunday */
    long days = days_from_civil(y, m, d);
    return (int)(((days % 7) + 4 + 7) % 7);  /* 1970-01-01 was Thursday(4) */
}

/* Center `s` over a 20-column week row and print it + a newline. */
static void print_centered(const char* s) {
    int n = (int)strlen(s), pad = (20 - n) / 2;
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s\n", s);
}

static void print_month(int m, int y) {
    char title[32];
    snprintf(title, sizeof title, "%s %d", MON[m - 1], y);
    print_centered(title);
    printf("Su Mo Tu We Th Fr Sa\n");
    int w = weekday(y, m, 1);            /* weekday of the 1st */
    int dim = month_days(m, y);
    int day = 1 - w;                     /* value in column 0 of row 0 (<=0 = blank) */
    while (day <= dim) {
        for (int c = 0; c < 7; c++) {
            if (day >= 1 && day <= dim) printf("%2d", day);
            else                        printf("  ");
            if (c < 6) putchar(' ');
            day++;
        }
        putchar('\n');
    }
}

int main(int argc, char** argv) {
    int m, y;
    if (argc >= 3) {                     /* cal <month> <year> */
        m = atoi(argv[1]); y = atoi(argv[2]);
        if (m < 1 || m > 12) { fprintf(stderr, "cal: %s: invalid month (1-12)\n", argv[1]); return 1; }
        print_month(m, y);
    } else if (argc == 2) {              /* cal <year> */
        y = atoi(argv[1]);
        if (y < 1 || y > 9999) { fprintf(stderr, "cal: %s: invalid year\n", argv[1]); return 1; }
        char yh[8]; snprintf(yh, sizeof yh, "%d", y);
        print_centered(yh); putchar('\n');
        for (m = 1; m <= 12; m++) { print_month(m, y); putchar('\n'); }
    } else {                             /* current month from the kernel RTC */
        nyx_tm now;
        time(&now);
        print_month(now.mon, now.year);
    }
    return 0;
}
