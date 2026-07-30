#ifndef NYX_TCC_FCNTL_H
#define NYX_TCC_FCNTL_H
/* NyxOS open(): bit0 = O_CREAT; there is no O_WRONLY/O_RDWR access mode, so map
 * the access-mode flags to 0. open(name, O_WRONLY|O_CREAT|O_TRUNC) -> flags 3. */
#define O_RDONLY 0
#define O_WRONLY 0
#define O_RDWR   0
#define O_CREAT  1
#define O_TRUNC  2
#define O_APPEND 4
#define O_BINARY 0
int open(const char* path, int flags, ...);
#endif
