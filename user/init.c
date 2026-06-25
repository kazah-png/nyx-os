#include "libc.h"

int main(void) {
    printf("init: starting hello.elf via exec...\n");
    int ret = exec("/hello.elf");
    printf("init: exec returned %d (should not happen)\n", ret);
    for (;;);
}
