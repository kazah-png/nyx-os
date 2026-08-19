#ifndef FAT_H
#define FAT_H

// FAT32 formatter/writer for the EFI System Partition (rung E2 of the UEFI-install
// path). A UEFI firmware boots \EFI\BOOT\BOOTX64.EFI from a FAT-formatted ESP, so a
// bootable real install needs NyxOS to format the GPT's ESP and populate /EFI/BOOT
// with the loader + grub.cfg. All pure geometry/byte-layout logic is KAT'd and the
// written image is host-validated with `fsck.fat` + mtools read-back.

#include "../core/kernel.h"

// FAT32 volume geometry, derived from the partition size (see fat_compute_geom).
typedef struct {
    uint32_t part_lba;        // partition start on the disk (LBA)
    uint32_t total_sectors;   // sectors in the partition
    uint32_t sec_per_clus;    // sectors per cluster
    uint32_t reserved;        // reserved sectors (boot + FSInfo + backups)
    uint32_t num_fats;        // 2
    uint32_t fat_size;        // sectors per FAT
    uint32_t data_start;      // first data sector (relative to the partition) = cluster 2
    uint32_t clusters;        // count of data clusters
} fat_geom_t;

int  fat_compute_geom(fat_geom_t* g, uint32_t part_lba, uint32_t total_sectors); // 0 ok, -1 too small for FAT32

// Format the partition FAT32 and populate /EFI/BOOT with BOOTX64.EFI (`boot_efi`,
// boot_len bytes) + grub.cfg (`cfg`, cfg_len bytes). WRITES the disk. 0 on success.
int  fat_format_esp(uint8_t dev, uint32_t part_lba, uint32_t total_sectors,
                    const void* boot_efi, uint32_t boot_len,
                    const char* cfg, uint32_t cfg_len);

int  fat_selftest(void);   // KAT the BPB / FAT-entry / 8.3-name / dir-entry / geometry math

#endif
