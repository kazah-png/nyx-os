#ifndef GPT_H
#define GPT_H

// GUID Partition Table writer/parser (UEFI). The real-hardware target is a UEFI
// laptop with an NVMe disk; a UEFI firmware boots from a GPT disk with an EFI
// System Partition, so a bootable real install needs NyxOS to lay down a valid
// GPT (this file) + a FAT ESP (fat.c, later) + the ext2 root — versus the legacy
// MBR + i386-pc GRUB that only BIOS/CSM machines boot.
//
// Rung E1 (this file): build + parse the protective MBR, the primary/backup GPT
// headers (with the two CRC-32s the spec mandates) and the 128-entry partition
// array; a full-disk writer that lays a 2-partition (ESP + NyxOS) layout; and a
// `gpt` listing wired into `disks`. All pure byte-layout/CRC logic is KAT'd and
// host-validated with `sgdisk -v` / `gdisk -l`.

#include "../core/kernel.h"

// Standard partition type GUIDs (on-disk mixed-endian byte order).
extern const uint8_t GPT_TYPE_ESP[16];    // C12A7328-F81F-11D2-BA4B-00A0C93EC93B
extern const uint8_t GPT_TYPE_LINUX[16];  // 0FC63DAF-8483-4772-8E79-3D69D8477DE4 (Linux filesystem data)

typedef struct {
    uint64_t my_lba, alt_lba;             // this header's LBA + the backup's
    uint64_t first_usable, last_usable;   // usable-LBA window for partitions
    uint64_t part_entry_lba;              // LBA of the partition entry array
    uint32_t num_entries, entry_size;     // array geometry (typically 128 x 128 B)
    uint32_t array_crc;                   // CRC-32 of the entry array
    uint8_t  disk_guid[16];
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  uniq_guid[16];
    uint64_t start_lba, end_lba, attrs;
    char     name[37];                    // partition name decoded from UTF-16LE (ASCII-only), NUL-terminated
} gpt_entry_t;

// --- pure builders (KAT'd) --------------------------------------------------
void gpt_build_pmbr(uint8_t* sec, uint64_t disk_sectors);
void gpt_build_header(uint8_t* sec, uint64_t my_lba, uint64_t alt_lba,
                      uint64_t first_usable, uint64_t last_usable,
                      uint64_t part_entry_lba, uint32_t num_entries,
                      uint32_t entry_size, uint32_t array_crc,
                      const uint8_t disk_guid[16]);
void gpt_build_entry(uint8_t* e, const uint8_t type_guid[16], const uint8_t uniq_guid[16],
                     uint64_t start_lba, uint64_t end_lba, uint64_t attrs, const char* name);

// --- pure parsers (KAT'd) ---------------------------------------------------
int  gpt_parse_header(const uint8_t* sec, gpt_header_t* out);   // 0 ok, <0: -1 no sig, -2 bad size, -3 bad CRC
int  gpt_parse_entry(const uint8_t* e, gpt_entry_t* out);       // 0 ok (used), 1 empty (zero type GUID)

// --- device-level (blockdev) ------------------------------------------------
// Write a complete GPT (protective MBR + primary/backup headers + both entry
// arrays) describing an ESP + a NyxOS partition. WRITES the disk. 0 on success.
int  gpt_write_disk(uint8_t dev, uint64_t disk_sectors,
                    uint64_t esp_start, uint64_t esp_sectors,
                    uint64_t nyx_start, uint64_t nyx_sectors,
                    const uint8_t disk_guid[16], const uint8_t esp_guid[16], const uint8_t nyx_guid[16]);
int  gpt_list(uint8_t dev);            // read + print the GPT partition table; -1 if the disk is not GPT
int  gpt_find_esp(uint8_t dev, uint64_t* start, uint64_t* sectors);  // locate the EFI System Partition; 0 ok, -1 none

int  gpt_selftest(void);               // KAT the CRC + header/entry byte layout + a build->parse round-trip

#endif
