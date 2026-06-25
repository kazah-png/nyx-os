#ifndef RTC_H
#define RTC_H

#include "kernel.h"

#define CMOS_ADDR    0x70
#define CMOS_DATA    0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_WEEKDAY  0x06
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

#define RTC_UIP      0x80

// rtc_time_t defined in kernel.h

void rtc_init(void);
uint8_t rtc_read_register(uint8_t reg);
void rtc_read_time(rtc_time_t* t);

#endif
