// ============================================================
// caldate.c - calendar date helpers
// ============================================================
// day_of_week() computes the weekday of a Gregorian date with Sakamoto's algorithm — a
// compact table method that folds in the leap-year rules (every 4th year, except centuries,
// except every 400th). The kernel uses it for the taskbar clock instead of trusting the
// CMOS weekday register, which QEMU and many BIOSes leave stale. Correctness is pinned by
// caldate_selftest() (the CI `caldate` KAT). Pure integer logic — no state, no float.
#include "kernel.h"
#include "caldate.h"

int day_of_week(int y, int m, int d) {
    // Per-month offset table (Sakamoto). January/February borrow from the prior year so
    // the leap day lands correctly, hence the y-- for m < 3.
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 1 || m > 12) return 0;
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;   // 0 = Sunday
}

// ---- Known-answer test --------------------------------------------------------------
int caldate_selftest(void) {
    static const struct { int y, m, d, w; } v[] = {
        { 1970,  1,  1, 4 },   // Thursday  — the Unix epoch
        { 2000,  1,  1, 6 },   // Saturday  — Y2K
        { 1999, 12, 31, 5 },   // Friday    — the day before
        { 2024,  2, 29, 4 },   // Thursday  — a leap day
        { 2016,  2, 29, 1 },   // Monday    — another leap day
        { 2025, 12, 25, 4 },   // Thursday  — Christmas 2025
        { 2026,  8,  8, 6 },   // Saturday
        { 2026,  8,  9, 0 },   // Sunday    — the next day
        { 1900,  1,  1, 1 },   // Monday    — 1900 is NOT a leap year (century, not /400)
        { 2100,  3,  1, 1 },   // Monday    — 2100 is NOT a leap year either
    };
    for (int i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++)
        if (day_of_week(v[i].y, v[i].m, v[i].d) != v[i].w) return i + 1;
    return 0;
}
