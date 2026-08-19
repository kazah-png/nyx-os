#include "../../core/kernel.h"
#include "ata.h"
#include "nvme.h"
#include "blockdev.h"

// One page-aligned bounce sector for the NVMe DMA path. The block layer runs
// single-threaded (ext2 is guarded by its own lock; the installer commands are
// sequential), and every use copies in/out immediately, so one buffer is enough.
static uint8_t blk_bounce[512] __attribute__((aligned(4096)));

int blk_read1(uint8_t dev, uint32_t lba, void* buf) {
    if (dev == BLK_NVME0) {
        if (nvme_read_blocks(lba, 1, blk_bounce) != 0) return -1;
        __builtin_memcpy(buf, blk_bounce, 512);
        return 0;
    }
    return ata_read_sectors(dev, lba, 1, buf) < 0 ? -1 : 0;
}

int blk_write1(uint8_t dev, uint32_t lba, const void* buf) {
    if (dev == BLK_NVME0) {
        __builtin_memcpy(blk_bounce, buf, 512);
        if (nvme_write_blocks(lba, 1, blk_bounce) != 0) return -1;
        return 0;
    }
    return ata_write_sectors(dev, lba, 1, buf) < 0 ? -1 : 0;
}

int blk_read(uint8_t dev, uint32_t lba, uint32_t count, void* buf) {
    if (dev != BLK_NVME0)                          // ATA reads a whole run in one PIO burst
        return ata_read_sectors(dev, lba, (uint8_t)count, buf) < 0 ? -1 : 0;
    uint8_t* p = (uint8_t*)buf;
    for (uint32_t i = 0; i < count; i++)
        if (blk_read1(dev, lba + i, p + (uint64_t)i * 512) != 0) return -1;
    return 0;
}

int blk_write(uint8_t dev, uint32_t lba, uint32_t count, const void* buf) {
    if (dev != BLK_NVME0)
        return ata_write_sectors(dev, lba, (uint8_t)count, buf) < 0 ? -1 : 0;
    const uint8_t* p = (const uint8_t*)buf;
    for (uint32_t i = 0; i < count; i++)
        if (blk_write1(dev, lba + i, p + (uint64_t)i * 512) != 0) return -1;
    return 0;
}
