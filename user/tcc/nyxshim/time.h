#ifndef NYX_TCC_TIME_H
#define NYX_TCC_TIME_H
typedef long time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
time_t      time(time_t* t);
struct tm*  localtime(const time_t* t);
#endif
