#include "kernel.h"

#define SERIAL_PORT 0x3F8

void init_serial(void) {
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x01);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}

void serial_putchar(char c) {
    // NOTE: No THRE busy-wait! In QEMU TCG with SMP, inb+outb across VCPU
    // switches can deadlock (BSP reads THRE=ready, gets preempted by AP,
    // then outb to a potentially different state). Raw outb is safe because
    // QEMU's 16550 emulation always accepts writes into the transmit FIFO.
    outb(SERIAL_PORT, c);
}

void serial_puts(const char* str) {
    while (*str) {
        if (*str == '\n') serial_putchar('\r');
        serial_putchar(*str++);
    }
}

char serial_getchar(void) {
    while (!(inb(SERIAL_PORT + 5) & 0x01));
    return inb(SERIAL_PORT);
}

char serial_getchar_nonblock(void) {
    if (inb(SERIAL_PORT + 5) & 0x01) {
        return inb(SERIAL_PORT);
    }
    return 0;
}
