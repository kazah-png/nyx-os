#ifndef NYX_TCC_UNISTD_H
#define NYX_TCC_UNISTD_H
#include "libc.h"   /* size_t */
typedef long ssize_t;
typedef long off_t;
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#ifndef R_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int     close(int fd);
int     unlink(const char* path);
off_t   lseek(int fd, off_t offset, int whence);
char*   getcwd(char* buf, size_t size);
int     access(const char* path, int mode);
int     execvp(const char* file, char* const argv[]);
#endif
