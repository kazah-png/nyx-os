#include "kernel.h"

#define MOUSE_BUFFER 16

static volatile int mouse_x = 0;
static volatile int mouse_y = 0;
static volatile uint8_t mouse_buttons = 0;

static volatile uint8_t packet[3];
static volatile int packet_idx = 0;

static void ps2_wait_write(void) {
    while (inb(0x64) & 2);
}

static void ps2_wait_read(void) {
    while (!(inb(0x64) & 1));
}

static uint8_t ps2_read(void) {
    ps2_wait_read();
    return inb(0x60);
}

static void ps2_write(uint16_t port, uint8_t val) {
    ps2_wait_write();
    outb(port, val);
}

static void mouse_write(uint8_t val) {
    ps2_write(0x64, 0xD4);
    ps2_write(0x60, val);
}



int mouse_init(void) {
    // Enable auxiliary device
    ps2_write(0x64, 0xA8);

    // Read controller config, enable IRQ12 + mouse clock
    ps2_write(0x64, 0x20);
    uint8_t config = ps2_read();
    config |= 0x02;  // Enable IRQ12
    config |= 0x20;  // Enable mouse clock
    ps2_write(0x64, 0x60);
    ps2_write(0x60, config);

    // Set mouse defaults
    mouse_write(0xF6);
    ps2_read();

    // Enable mouse data reporting
    mouse_write(0xF4);
    if (ps2_read() != 0xFA) {
        serial_puts("[MOUSE] No ACK\n");
        return -1;
    }

    serial_puts("[MOUSE] Initialized\n");
    return 0;
}

void mouse_irq_handler(void* unused) {
    (void)unused;
    if (!(inb(0x64) & 1)) return;
    uint8_t data = inb(0x60);

    if (packet_idx == 0) {
        if (!(data & 0x08)) return;
        packet[0] = data;
        packet_idx = 1;
    } else if (packet_idx == 1) {
        packet[1] = data;
        packet_idx = 2;
    } else if (packet_idx == 2) {
        packet[2] = data;
        packet_idx = 0;

        mouse_buttons = packet[0] & 0x07;
        int dx = (int)(int8_t)packet[1];
        int dy = -(int)(int8_t)packet[2];
        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > 4095) mouse_x = 4095;
        if (mouse_y > 4095) mouse_y = 4095;
    }
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }
void mouse_set_pos(int x, int y) { mouse_x = x; mouse_y = y; }
