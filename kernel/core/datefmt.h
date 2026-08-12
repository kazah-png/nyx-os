#ifndef DATEFMT_H
#define DATEFMT_H

#include "kernel.h"   // rtc_time_t + fixed-width types

// strftime(3)-style formatting of a broken-down RTC time into `out` (always NUL-
// terminated, never writes past cap). Supports: %Y %y %m %d %e %H %I %M %S %p
// %A %a %B %b %j %n %t %%  (an unknown %X is emitted literally). The weekday and
// day-of-year are computed from the date (rtc_time_t carries no weekday). Returns
// the number of bytes written (excluding the NUL).
int date_format(char* out, int cap, const char* fmt, const rtc_time_t* t);

int datefmt_selftest(void);

#endif // DATEFMT_H
