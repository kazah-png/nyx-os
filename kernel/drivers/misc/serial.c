#include "../../core/kernel.h"

#define SERIAL_PORT 0x3F8

// ---- Kernel log ring (backs `dmesg`) ----------------------------------------------------
// Every character the kernel writes to the serial console is also captured here, so the
// boot messages ([INIT]/[LOGIN]/…) can be reviewed after they scroll off the framebuffer —
// especially on the real UMPC, which has no physical serial port, so this RAM copy is the
// ONLY way to see them. A plain ring: when full it overwrites the oldest byte. Lockless like
// serial_putchar itself (a diagnostic log; a rare garbled byte under SMP is acceptable, and a
// lock in this path could deadlock — see the note in serial_putchar). `klog_muted` stops a
// `dmesg` dump (which re-enters serial_putchar) from capturing its own output forever.
#define KLOG_SIZE 16384
static char klog_buf[KLOG_SIZE];
static uint32_t klog_head = 0;    // next write index
static int klog_full = 0;         // set once the ring has wrapped
static volatile int klog_muted = 0;

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
    if (!klog_muted) {                          // also capture into the dmesg ring
        klog_buf[klog_head++] = c;
        if (klog_head >= KLOG_SIZE) { klog_head = 0; klog_full = 1; }
    }
}

// Dump the captured kernel log to the console, oldest byte first. Muted while dumping so the
// output (which flows back through serial_putchar via printf) is not re-captured. The ring
// holds the CR that serial_puts inserts before each LF; drop it so lines render cleanly.
void klog_dump(void) {
    klog_muted = 1;
    uint32_t start = klog_full ? klog_head : 0;         // oldest surviving byte
    uint32_t total = klog_full ? KLOG_SIZE : klog_head;
    for (uint32_t k = 0; k < total; k++) {
        char c = klog_buf[(start + k) % KLOG_SIZE];
        if (c != '\r') printf("%c", c);
    }
    klog_muted = 0;
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
