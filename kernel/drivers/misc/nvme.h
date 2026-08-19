#ifndef NVME_H
#define NVME_H

// Minimal NVMe block driver for real-hardware install (the target laptop's disk
// is an NVMe SSD on PCIe, invisible to the legacy ATA/IDE path). NVMe is a
// standard register interface, so one driver serves QEMU's `-device nvme`
// (1b36:0010) and a real SSD (e.g. Silicon Motion 126f:2263) alike.
//
// Rung 1 (this file): discover the controller via PCI class 01:08, map its BAR0
// MMIO (see map_mmio), reset + enable it, and stand up the admin queue. IDENTIFY,
// the I/O queue pair and PRP read/write, then wiring into disks/mkfs/nyxinstall,
// are the following rungs.

// Controller register offsets within BAR0.
#define NVME_REG_CAP   0x00   // Controller Capabilities (64-bit)
#define NVME_REG_VS    0x08   // Version
#define NVME_REG_CC    0x14   // Controller Configuration
#define NVME_REG_CSTS  0x1C   // Controller Status
#define NVME_REG_AQA   0x24   // Admin Queue Attributes
#define NVME_REG_ASQ   0x28   // Admin Submission Queue base (64-bit)
#define NVME_REG_ACQ   0x30   // Admin Completion Queue base (64-bit)
#define NVME_REG_DBS   0x1000 // Doorbell registers base

int nvme_init(void);             // probe + reset + enable + admin queue; 0 on success, <0 on failure
int nvme_identify(void);         // admin IDENTIFY controller + namespace -> model/serial/capacity/LBA size
int nvme_create_io_queues(void); // set-features + create one I/O SQ/CQ pair (qid 1)
int nvme_io(int write, uint64_t slba, void* buf);  // one-block I/O; buf page-aligned. WRITE mutates the disk.
int nvme_read_blocks(uint64_t lba, uint32_t count, void* buf);        // multi-block read; buf page-aligned
int nvme_write_blocks(uint64_t lba, uint32_t count, const void* buf); // multi-block write; buf page-aligned. WRITES.
int nvme_dump_lba(uint64_t lba); // READ-ONLY: read a block + hex-dump it (safe on a real disk)
int nvme_io_selftest(void);      // QEMU-ONLY write+read round-trip (WRITES; never a real disk)
int         nvme_io_ready(void);        // non-zero once the I/O queue is up
const char* nvme_model_str(void);       // IDENTIFY model string (valid after nvme_identify)
uint64_t    nvme_capacity_blocks(void); // namespace size in logical blocks
uint32_t    nvme_block_size(void);      // bytes per logical block (512 or 4096)
int nvme_present(void);          // non-zero once a controller has been brought up
int nvme_selftest(void);         // KAT the pure register-math (CAP/doorbell/IDENTIFY/queue/IO SQEs)

#endif
