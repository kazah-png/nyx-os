#include "../../core/kernel.h"

#define MOUSE_BUFFER 16

static volatile int mouse_x = 0;
static volatile int mouse_y = 0;
static volatile int mouse_z = 0;             // accumulated wheel notches (+up / -down)
static volatile uint8_t mouse_buttons = 0;
static int mouse_has_wheel = 0;              // 1 = IntelliMouse: 4-byte packets with Z

static volatile uint8_t packet[4];
static volatile int packet_idx = 0;

// Every wait is BOUNDED: on real hardware whose i8042 has no auxiliary (mouse) device —
// common on modern laptops/UMPCs where the touchpad is USB-HID — the status bit we poll
// never changes, so an unbounded `while` here hangs the whole boot. ~100k port reads is
// far longer than a present mouse ever needs to answer, yet still bails on a dead port.
#define PS2_TIMEOUT 100000

// Wait for the input buffer to drain (bit 1 clear) before writing. Returns 0, or -1 on timeout.
static int ps2_wait_write(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++)
        if (!(inb(0x64) & 2)) return 0;
    return -1;
}

// Wait for the output buffer to fill (bit 0 set) before reading. Returns 0, or -1 on timeout.
static int ps2_wait_read(void) {
    for (int i = 0; i < PS2_TIMEOUT; i++)
        if (inb(0x64) & 1) return 0;
    return -1;
}

static uint8_t ps2_read(void) {
    if (ps2_wait_read() != 0) return 0xFF;   // timeout -> no data; 0xFF != 0xFA ACK, so callers bail
    return inb(0x60);
}

static void ps2_write(uint16_t port, uint8_t val) {
    ps2_wait_write();                        // best-effort; write anyway on timeout
    outb(port, val);
}

static void mouse_write(uint8_t val) {
    ps2_write(0x64, 0xD4);
    ps2_write(0x60, val);
}

// Enable the IntelliMouse scroll wheel: the standard "magic knock" — set the sample
// rate to 200, then 100, then 80 — makes a wheel mouse switch its device ID to 0x03
// and start sending 4-byte packets (the 4th byte is a signed Z/wheel delta). Returns
// 1 if the device reports the wheel ID. QEMU's PS/2 mouse implements this.
static int mouse_enable_wheel(void) {
    mouse_write(0xF3); ps2_read();   // set sample rate
    mouse_write(200);  ps2_read();
    mouse_write(0xF3); ps2_read();
    mouse_write(100);  ps2_read();
    mouse_write(0xF3); ps2_read();
    mouse_write(80);   ps2_read();
    mouse_write(0xF2); ps2_read();   // get device ID (ACK)
    uint8_t id = ps2_read();         // 0x03 = wheel mouse
    return id == 0x03;
}

int mouse_init(void) {
    // Flush any stale data from PS/2 controller (bounded — see PS2_TIMEOUT above)
    for (int i = 0; i < PS2_TIMEOUT && (inb(0x64) & 1); i++) inb(0x60);
    for (int i = 0; i < PS2_TIMEOUT && (inb(0x64) & 2); i++);  // wait for input buffer empty

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

    // Try to turn on the scroll wheel while reporting is still off (defaults disable
    // it), so the knock's replies aren't interleaved with movement packets.
    mouse_has_wheel = mouse_enable_wheel();

    // Enable mouse data reporting
    mouse_write(0xF4);
    if (ps2_read() != 0xFA) {
        serial_puts("[MOUSE] No ACK\n");
        return -1;
    }

    serial_puts(mouse_has_wheel ? "[MOUSE] Initialized (wheel)\n" : "[MOUSE] Initialized\n");
    return 0;
}

void mouse_irq_handler(void* unused) {
    (void)unused;
    uint8_t st = inb(0x64);
    if ((st & 0x21) != 0x21) return;  // OBF=1, mouse bit=1 → mouse data
    uint8_t data = inb(0x60);

    int last = mouse_has_wheel ? 3 : 2;      // index of the final byte in a packet

    if (packet_idx == 0) {
        if (!(data & 0x08)) return;          // byte 1 always has bit 3 set — resync
        packet[0] = data;
        packet_idx = 1;
    } else if (packet_idx < last) {
        packet[packet_idx++] = data;
    } else {
        packet[packet_idx] = data;           // final byte -> apply the whole packet
        packet_idx = 0;

        mouse_buttons = packet[0] & 0x07;

        // Bits 6/7 of byte 0 are the X/Y overflow flags: when either is set the
        // movement counters wrapped and dx/dy are meaningless (QEMU raises them on
        // a large/fast jump), so keep the button state but DROP the movement this
        // packet rather than teleport the cursor. Buttons (bits 0-2) stay valid.
        if (!(packet[0] & 0xC0)) {
            int dx = (int)(int8_t)packet[1];
            int dy = -(int)(int8_t)packet[2];
            mouse_x += dx;
            mouse_y += dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 4095) mouse_x = 4095;
            if (mouse_y > 4095) mouse_y = 4095;
        }

        if (mouse_has_wheel) {
            // 4th byte: signed Z in the low nibble (buttons 4/5 in bits 4-5 ignored).
            int8_t z = (int8_t)(packet[3] << 4) >> 4;   // sign-extend the 4-bit field
            if (z > 0) mouse_z++;                        // one accumulated notch up
            else if (z < 0) mouse_z--;                   // one notch down
        }
    }
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
int mouse_get_buttons(void) { return mouse_buttons; }
int mouse_get_z(void) { return mouse_z; }
void mouse_set_pos(int x, int y) { mouse_x = x; mouse_y = y; }
