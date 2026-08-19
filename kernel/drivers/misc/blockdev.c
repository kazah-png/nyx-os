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
    uint8_t* p = (uint8_t*)buf;
    if (dev == BLK_NVME0) {
        for (uint32_t i = 0; i < count; i++)
            if (blk_read1(dev, lba + i, p + (uint64_t)i * 512) != 0) return -1;
        return 0;
    }
    while (count) {                                // ATA: <=128-sector PIO bursts (count is a uint8_t on the wire)
        uint32_t n = count > 128 ? 128 : count;
        if (ata_read_sectors(dev, lba, (uint8_t)n, p) < 0) return -1;
        lba += n; p += (uint64_t)n * 512; count -= n;
    }
    return 0;
}

int blk_write(uint8_t dev, uint32_t lba, uint32_t count, const void* buf) {
    const uint8_t* p = (const uint8_t*)buf;
    if (dev == BLK_NVME0) {
        for (uint32_t i = 0; i < count; i++)
            if (blk_write1(dev, lba + i, p + (uint64_t)i * 512) != 0) return -1;
        return 0;
    }
    while (count) {                                // ATA: <=128-sector PIO bursts
        uint32_t n = count > 128 ? 128 : count;
        if (ata_write_sectors(dev, lba, (uint8_t)n, p) < 0) return -1;
        lba += n; p += (uint64_t)n * 512; count -= n;
    }
    return 0;
}
