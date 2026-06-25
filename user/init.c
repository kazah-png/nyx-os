#include "libc.h"

int main(void) {
    printf("\n*** NyxOS Userspace Init ***\n");
    printf("PID: %d\n", getpid());
    printf("Welcome to NyxOS userspace!\n");
    printf("Testing printf formats: int=%d hex=%x str=\"%s\" char='%c' ptr=%p\n",
           42, 0xdead, "hello", 'X', 0x12345678);

    char *buf = (char*)malloc(64);
    if (buf) {
        int n = snprintf(buf, 64, "malloc+snprintf: %d + %d = %d\n", 10, 20, 30);
        write(1, buf, n);
        free(buf);
    }

    printf("Init complete, exiting.\n");
    return 0;
}
