#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include "fake_stdlib.h"

#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_IFMT  00170000

struct stat {
    int st_dev;
    int st_ino;
    int st_mode;
    int st_nlink;
    int st_uid;
    int st_gid;
    int st_rdev;
    long st_size;
    long st_atime;
    long st_mtime;
    long st_ctime;
};

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int mkdir(const char *path, int mode);

#endif
