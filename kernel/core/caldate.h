#ifndef CALDATE_H
#define CALDATE_H
// Calendar date helpers. The CMOS RTC exposes a weekday register (0x06) but firmware and
// emulators frequently leave it wrong, so the day of week is computed from the date
// instead (Sakamoto's algorithm). See caldate.c.
#include "kernel.h"

// Day of week for a proleptic-Gregorian date: returns 0=Sunday .. 6=Saturday.
// y is the full year (e.g. 2026), m in 1..12, d in 1..31.
int day_of_week(int y, int m, int d);

// Known-answer test over a spread of reference dates (epoch, Y2K, leap days, century
// edges); returns 0 if all pass.
int caldate_selftest(void);

#endif // CALDATE_H
