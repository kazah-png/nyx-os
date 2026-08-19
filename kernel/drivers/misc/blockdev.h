#ifndef BLOCKDEV_H
#define BLOCKDEV_H

// Block-device dispatch for the installer stack. A `dev` id selects the backend:
// ATA drives are 0/1; BLK_NVME0 is the NVMe namespace 1. This lets ext2 / nyxpart
// / mkfs / mount / nyxinstall target either disk without knowing the driver. All
// I/O is in 512-byte sectors. The NVMe path bounces through a page-aligned buffer
// because its DMA needs an alignment that ext2's kmalloc'd block buffers lack.
#define BLK_NVME0 0x10

int blk_read1(uint8_t dev, uint32_t lba, void* buf);          // one 512-B sector
int blk_write1(uint8_t dev, uint32_t lba, const void* buf);   // one 512-B sector
int blk_read(uint8_t dev, uint32_t lba, uint32_t count, void* buf);
int blk_write(uint8_t dev, uint32_t lba, uint32_t count, const void* buf);

#endif
