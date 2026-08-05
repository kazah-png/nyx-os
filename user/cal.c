#include "libc.h"

/* cal — print a month calendar. No args: the current month (from the RTC via
 * time(), like date). "cal <month> <year>": that specific month. Day-of-week for
 * the 1st comes from Sakamoto's algorithm (pure integer, no external table), and
 * weeks run Sunday-first like traditional cal(1). */

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/* Sakamoto's method: 0=Sunday .. 6=Saturday for a Gregorian date (month 1..12). */
static int day_of_week(int y, int m, int d) {
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int main(int argc, char** argv) {
    static const char* mname[13] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const int mlen[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    int mon, year;
    if (argc >= 3) {                    // cal <month> <year>
        mon  = atoi(argv[1]);
        year = atoi(argv[2]);
    } else {                            // no args: the current month from the clock
        nyx_tm t;
        if (time(&t) != 0) { printf("cal: cannot read the clock\n"); return 1; }
        mon  = t.mon;
        year = t.year;
    }
    if (mon < 1 || mon > 12 || year < 1) {
        printf("cal: usage: cal [month year]\n");
        return 1;
    }

    int days = mlen[mon];
    if (mon == 2 && is_leap(year)) days = 29;
    int start = day_of_week(year, mon, 1);       // weekday of the 1st (0=Sun)

    // Title centered over the 20-char week grid, then Sunday-first day labels.
    char title[32];
    int tl = snprintf(title, sizeof(title), "%s %d", mname[mon], year);
    int pad = (20 - tl) / 2;
    for (int i = 0; i < pad; i++) printf(" ");
    printf("%s\n", title);
    printf("Su Mo Tu We Th Fr Sa\n");

    for (int i = 0; i < start; i++) printf("   ");   // blanks before day 1
    for (int d = 1; d <= days; d++) {
        printf("%2d ", d);
        if ((start + d) % 7 == 0) printf("\n");      // wrap after Saturday
    }
    if ((start + days) % 7 != 0) printf("\n");        // finish a partial last row
    return 0;
}
