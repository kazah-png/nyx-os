#include "../../core/kernel.h"
#include "rtc.h"

static int rtc_initialized = 0;

static int rtc_is_update_in_progress(void) {
    outb(CMOS_ADDR, RTC_STATUS_A);
    return inb(CMOS_DATA) & RTC_UIP;
}

uint8_t rtc_read_register(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

void rtc_init(void) {
    outb(CMOS_ADDR, RTC_STATUS_B);
    io_wait();
    uint8_t reg_b = inb(CMOS_DATA);

    // Set 24-hour mode (bit 1) and binary mode (bit 2)
    reg_b |= 0x06;
    outb(CMOS_ADDR, RTC_STATUS_B);
    io_wait();
    outb(CMOS_DATA, reg_b);

    rtc_initialized = 1;
}

void rtc_read_time(rtc_time_t* t) {
    if (!t) return;

    uint8_t reg_b = rtc_read_register(RTC_STATUS_B);
    int binary = (reg_b & 0x04) != 0;

    uint8_t second, minute, hour, day, month, year;

    // Read all registers atomically (retry if UIP was set during read)
    int tries = 3;
    while (tries--) {
        while (rtc_is_update_in_progress());

        second = rtc_read_register(RTC_SECONDS);
        minute = rtc_read_register(RTC_MINUTES);
        hour   = rtc_read_register(RTC_HOURS);
        day    = rtc_read_register(RTC_DAY);
        month  = rtc_read_register(RTC_MONTH);
        year   = rtc_read_register(RTC_YEAR);

        // If UIP was still clear during entire read, we're good
        if (!rtc_is_update_in_progress()) break;
    }

    // If not using the RTC time in bootup, we can just rely on our safe reads

    // Convert BCD to binary if needed
    if (!binary) {
        second = (second >> 4) * 10 + (second & 0x0F);
        minute = (minute >> 4) * 10 + (minute & 0x0F);
        hour   = (hour >> 4) * 10 + (hour & 0x0F);
        day    = (day >> 4) * 10 + (day & 0x0F);
        month  = (month >> 4) * 10 + (month & 0x0F);
        year   = (year >> 4) * 10 + (year & 0x0F);
    }

    // Check for 12-hour mode and convert
    if (!(reg_b & 0x02)) {
        uint8_t pm = hour & 0x80;
        hour &= 0x7F;
        if (pm) hour += 12;
        if (hour == 24) hour = 12;
        if (!pm && hour == 12) hour = 0;
    }

    t->second = second;
    t->minute = minute;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = 2000 + year; // assume 2000-2099 range
}
