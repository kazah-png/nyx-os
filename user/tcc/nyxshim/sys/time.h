#ifndef NYX_TCC_SYS_TIME_H
#define NYX_TCC_SYS_TIME_H
struct timeval { long tv_sec; long tv_usec; };
int gettimeofday(struct timeval* tv, void* tz);
#endif
