#include "../../core/kernel.h"
#include "ata.h"
#include "nvme.h"
#include "blockdev.h"

// The NVMe path bounces through a page-aligned DMA region and batches contiguous
// runs into single multi-block commands inside nvme_read_blocks/nvme_write_blocks,
// so callers here may pass buffers of any alignment. The block layer runs
// single-threaded (ext2 is guarded by its own lock; the installer commands are
// sequential).

int blk_read1(uint8_t dev, uint32_t lba, void* buf) {
    if (dev == BLK_NVME0)
        return nvme_read_blocks(lba, 1, buf) == 0 ? 0 : -1;
    return ata_read_sectors(dev, lba, 1, buf) < 0 ? -1 : 0;
}

int blk_write1(uint8_t dev, uint32_t lba, const void* buf) {
    if (dev == BLK_NVME0)
        return nvme_write_blocks(lba, 1, buf) == 0 ? 0 : -1;
    return ata_write_sectors(dev, lba, 1, buf) < 0 ? -1 : 0;
}

int blk_read(uint8_t dev, uint32_t lba, uint32_t count, void* buf) {
    if (dev == BLK_NVME0)
        return nvme_read_blocks(lba, count, buf) == 0 ? 0 : -1;
    uint8_t* p = (uint8_t*)buf;
    while (count) {                                // ATA: <=128-sector PIO bursts (count is a uint8_t on the wire)
        uint32_t n = count > 128 ? 128 : count;
        if (ata_read_sectors(dev, lba, (uint8_t)n, p) < 0) return -1;
        lba += n; p += (uint64_t)n * 512; count -= n;
    }
    return 0;
}

int blk_write(uint8_t dev, uint32_t lba, uint32_t count, const void* buf) {
    if (dev == BLK_NVME0)
        return nvme_write_blocks(lba, count, buf) == 0 ? 0 : -1;
    const uint8_t* p = (const uint8_t*)buf;
    while (count) {                                // ATA: <=128-sector PIO bursts
        uint32_t n = count > 128 ? 128 : count;
        if (ata_write_sectors(dev, lba, (uint8_t)n, p) < 0) return -1;
        lba += n; p += (uint64_t)n * 512; count -= n;
    }
    return 0;
}
