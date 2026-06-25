#include "kernel.h"

#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

// RTL8139 registers (I/O)
#define RTL_REG_MAC      0x00
#define RTL_REG_RBSTART  0x30
#define RTL_REG_CR       0x37
#define RTL_REG_CAPR     0x38
#define RTL_REG_IMR      0x3C
#define RTL_REG_ISR      0x3E
#define RTL_REG_TCR      0x40
#define RTL_REG_RCR      0x44
#define RTL_REG_TXADDR0  0x20
#define RTL_REG_TXADDR1  0x24
#define RTL_REG_TXADDR2  0x28
#define RTL_REG_TXADDR3  0x2C
#define RTL_REG_TXSTATUS0 0x10
#define RTL_REG_CONFIG1  0x52

#define RTL_CR_RST       0x10
#define RTL_CR_RE        0x08
#define RTL_CR_TE        0x04
#define RTL_RCR_AB       0x08
#define RTL_RCR_AM       0x04
#define RTL_RCR_APM      0x02
#define RTL_RCR_AAP      0x01

#define RX_BUF_SIZE      8192
#define TX_BUF_SIZE      1536

static uint16_t rtl_io_base = 0;
static uint8_t rtl_mac[6];
static uint8_t* rx_buffer = NULL;
static uint8_t* tx_buffers[4];
static int tx_cur = 0;
static int rtl_initialized = 0;

// PCI config I/O
static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC) | 0x80000000;
    outl(0xCF8, address);
    return inl(0xCFC);
}

static void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (offset & 0xFC) | 0x80000000;
    outl(0xCF8, address);
    outl(0xCFC, value);
}

static uint8_t rtl_readb(uint16_t reg) {
    return inb(rtl_io_base + reg);
}

static void rtl_writeb(uint16_t reg, uint8_t val) {
    outb(rtl_io_base + reg, val);
}

static void rtl_writel(uint16_t reg, uint32_t val) {
    outl(rtl_io_base + reg, val);
}

static void rtl_writew(uint16_t reg, uint16_t val) {
    outw(rtl_io_base + reg, val);
}

// ISR bits
#define RTL_ISR_ROK  0x0001
#define RTL_ISR_RER  0x0002
#define RTL_ISR_TOK  0x0004
#define RTL_ISR_TER  0x0008

int rtl8139_init(void) {
    printf("[RTL8139] Probing PCI...\n");
    uint32_t vid_did = pci_read_config(0, 0, 0, 0);
    printf("[RTL8139] Vendor:Device at bus0 = 0x%x\n", vid_did);

    for (int bus = 0; bus < 1; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor = pci_read_config(bus, slot, 0, 0);
            if (vendor == 0xFFFFFFFF || vendor == 0) continue;
            uint16_t vid = vendor & 0xFFFF;
            uint16_t did = (vendor >> 16) & 0xFFFF;
            if (vid == RTL8139_VENDOR_ID && did == RTL8139_DEVICE_ID) {
                printf("[RTL8139] Found at %d:%d.0\n", bus, slot);
                uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
                rtl_io_base = bar0 & 0xFFFC;
                printf("[RTL8139] I/O base: 0x%x\n", rtl_io_base);

                // Enable bus mastering
                uint32_t cmd = pci_read_config(bus, slot, 0, 0x04);
                cmd |= 0x07;
                pci_write_config(bus, slot, 0, 0x04, cmd);

                // Wake up from power saving mode (CONFIG1, offset 0x52)
                uint8_t config1 = rtl_readb(RTL_REG_CONFIG1);
                config1 &= ~0x08;  // Clear PM_Enable
                rtl_writeb(RTL_REG_CONFIG1, config1);

                // Read MAC address
                for (int i = 0; i < 6; i++) {
                    rtl_mac[i] = rtl_readb(RTL_REG_MAC + i);
                }
                printf("[RTL8139] MAC: ");
                for (int mi = 0; mi < 6; mi++) {
                    printf("%x", rtl_mac[mi]);
                    if (mi < 5) printf(":");
                }
                printf("\n");

                // Soft reset
                rtl_writeb(RTL_REG_CR, RTL_CR_RST);
                int timeout = 1000;
                while ((rtl_readb(RTL_REG_CR) & RTL_CR_RST) && timeout--) {
                    for (volatile int d = 0; d < 1000; d++);
                }
                if (timeout <= 0) {
                    printf("[RTL8139] Reset timeout!\n");
                    return -1;
                }
                printf("[RTL8139] Reset complete\n");

                // Re-read MAC after reset (some variants need this)
                for (int i = 0; i < 6; i++) {
                    rtl_mac[i] = rtl_readb(RTL_REG_MAC + i);
                }

                // Allocate RX buffer (256-byte aligned for RBSTART)
                uint8_t* rx_raw = (uint8_t*)kmalloc(RX_BUF_SIZE + 256);
                if (!rx_raw) return -1;
                rx_buffer = (uint8_t*)(((uintptr_t)rx_raw + 255) & ~255);
                memset_asm(rx_buffer, 0, RX_BUF_SIZE + 16);

                // Allocate TX buffers (256-byte aligned)
                for (int i = 0; i < 4; i++) {
                    uint8_t* tx_raw = (uint8_t*)kmalloc(TX_BUF_SIZE + 256);
                    if (!tx_raw) return -1;
                    tx_buffers[i] = (uint8_t*)(((uintptr_t)tx_raw + 255) & ~255);
                    memset_asm(tx_buffers[i], 0, TX_BUF_SIZE);
                }

                // Set RX buffer address
                uintptr_t rx_phys = (uintptr_t)rx_buffer;
                rtl_writel(RTL_REG_RBSTART, (uint32_t)rx_phys);
                printf("[RTL8139] RX buffer at 0x%x\n", (uint32_t)rx_phys);

                // Set TX buffer addresses
                for (int i = 0; i < 4; i++) {
                    rtl_writel(RTL_REG_TXADDR0 + i*4, (uint32_t)tx_buffers[i]);
                }

                // Configure RX: accept broadcast, multicast, physical match
                rtl_writew(RTL_REG_RCR, RTL_RCR_AB | RTL_RCR_AM | RTL_RCR_APM | RTL_RCR_AAP);

                // Enable transmitter and receiver
                rtl_writeb(RTL_REG_CR, RTL_CR_TE | RTL_CR_RE);

                // Mask all interrupts (polling mode)
                rtl_writeb(RTL_REG_IMR, 0x00);

                rtl_initialized = 1;
                printf("[RTL8139] Initialized successfully\n");

                // Register network interface
                for (int i = 0; i < 8; i++) {
                    if (!net_interfaces[i].name[0]) {
                        strcpy(net_interfaces[i].name, "eth0");
                        for (int j = 0; j < 6; j++) net_interfaces[i].mac[j] = rtl_mac[j];
                        net_interfaces[i].ip = 0x00000000;
                        net_interfaces[i].mtu = 1500;
                        net_interfaces[i].flags = 1;
                        break;
                    }
                }
                return 0;
            }
        }
    }
    printf("[RTL8139] No RTL8139 found\n");
    return -1;
}

int rtl8139_send_packet(const uint8_t* data, uint32_t len) {
    if (!rtl_initialized || len > TX_BUF_SIZE) return -1;
    memcpy(tx_buffers[tx_cur], data, len);
    rtl_writel(RTL_REG_TXSTATUS0 + tx_cur * 4, len | 0x80000000);
    tx_cur = (tx_cur + 1) % 4;
    return len;
}

int rtl8139_receive_packet(uint8_t* buffer, uint32_t max_len) {
    if (!rtl_initialized) return -1;
    uint16_t capr = rtl_readb(RTL_REG_CAPR) | (rtl_readb(RTL_REG_CAPR + 1) << 8);
    uint16_t rx_read = capr + 16;
    if (rx_read >= RX_BUF_SIZE) rx_read -= RX_BUF_SIZE;
    uint16_t capr2 = rtl_readb(RTL_REG_CAPR) | (rtl_readb(RTL_REG_CAPR+1) << 8);
    if ((int)rx_read == (int)capr2) return 0;

    uint16_t packet_len = rx_buffer[rx_read+2] | (rx_buffer[rx_read+3] << 8);
    if (packet_len == 0xFFF0) {
        uint16_t val = rx_read + 4;
        rtl_writeb(RTL_REG_CAPR, (uint8_t)(val & 0xFF));
        rtl_writeb(RTL_REG_CAPR + 1, (uint8_t)((val >> 8) & 0xFF));
        return 0;
    }
    packet_len -= 4;
    if (packet_len > max_len) packet_len = max_len;
    uint16_t data_start = (rx_read + 4) % RX_BUF_SIZE;

    if (data_start + packet_len <= RX_BUF_SIZE) {
        memcpy(buffer, &rx_buffer[data_start], packet_len);
    } else {
        uint32_t first_part = RX_BUF_SIZE - data_start;
        memcpy(buffer, &rx_buffer[data_start], first_part);
        memcpy(buffer + first_part, rx_buffer, packet_len - first_part);
    }
    uint16_t new_capr = rx_read + packet_len + 4;
    if (new_capr >= RX_BUF_SIZE) new_capr -= RX_BUF_SIZE;
    rtl_writeb(RTL_REG_CAPR, (uint8_t)(new_capr & 0xFF));
    rtl_writeb(RTL_REG_CAPR + 1, (uint8_t)((new_capr >> 8) & 0xFF));
    rtl_writew(RTL_REG_ISR, RTL_ISR_ROK);
    return packet_len;
}
