#ifndef NYX_TCC_TIME_H
#define NYX_TCC_TIME_H
/* time_t, struct tm and localtime/gmtime/strftime now live in the NyxOS libc
 * (added v6.5.111); redefining them here clashed with libc.h. Defer to libc.h
 * for the calendar types and add only time(), which libc.h omits (it normally
 * comes from syscall.h, excluded in the tcc build via NYX_LIBC_NO_SYSCALL). */
#include "libc.h"
time_t time(time_t* t);
#endif
