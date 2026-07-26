#include "../../core/kernel.h"
#include "ata.h"

static int ata_busy_wait(uint16_t ctrl, int timeout_ms) {
    uint16_t cmd_port = ctrl - ATA_PRIMARY_CTRL + ATA_PRIMARY_CMD;
    while (timeout_ms--) {
        if (!(inb(cmd_port) & ATA_STATUS_BSY))
            return 0;
        for (volatile int i = 0; i < 1000; i++);
    }
    return -1;
}

static int ata_drq_wait(uint16_t data_port) {
    int timeout = 30000;
    while (timeout--) {
        uint8_t st = inb(data_port + ATA_PRIMARY_CMD - ATA_PRIMARY_DATA);
        if (st & ATA_STATUS_ERR) return -1;
        if (st & ATA_STATUS_DRQ) return 0;
        if (!(st & ATA_STATUS_BSY)) return -1;
    }
    return -1;
}

int ata_init(void) {
    outb(ATA_PRIMARY_CTRL, 0x04);
    for (volatile int i = 0; i < 20000; i++);
    return 0;
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buf) {
    if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
        return -1;

    uint8_t drv = 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_DRIVE, drv);
    outb(ATA_PRIMARY_SECCNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MI, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD, ATA_CMD_READ);

    uint16_t* ptr = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
            return -1;
        if (ata_drq_wait(ATA_PRIMARY_DATA) < 0)
            return -1;
        for (int i = 0; i < 256; i++)
            ptr[i] = inw(ATA_PRIMARY_DATA);
        ptr += 256;
    }
    return count;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void* buf) {
    if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
        return -1;

    uint8_t drv = 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F);
    outb(ATA_PRIMARY_DRIVE, drv);
    outb(ATA_PRIMARY_SECCNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba));
    outb(ATA_PRIMARY_LBA_MI, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_CMD, ATA_CMD_WRITE);

    const uint16_t* ptr = (const uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
            return -1;
        if (ata_drq_wait(ATA_PRIMARY_DATA) < 0)
            return -1;
        for (int i = 0; i < 256; i++)
            outw(ATA_PRIMARY_DATA, ptr[i]);
        ptr += 256;
    }

    // Wait for command completion
    if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
        return -1;

    // Flush cache
    outb(ATA_PRIMARY_CMD, ATA_CMD_CACHE_FLUSH);
    if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
        return -1;

    return count;
}

int ata_identify(uint8_t drive, uint16_t* buf) {
    if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
        return -1;

    uint8_t drv = 0xE0 | (drive << 4);
    outb(ATA_PRIMARY_DRIVE, drv);
    outb(ATA_PRIMARY_SECCNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MI, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    uint8_t st = inb(ATA_PRIMARY_CMD);
    if (st == 0) return -1;

    if (ata_busy_wait(ATA_PRIMARY_CTRL, 30000) < 0)
        return -1;

    for (int i = 0; i < 256; i++)
        buf[i] = inw(ATA_PRIMARY_DATA);
    return 0;
}
