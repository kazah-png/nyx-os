#include "../core/kernel.h"
#include "../drivers/misc/blockdev.h"
#include "fat.h"

// FAT32 on the EFI System Partition. Single sectors-per-cluster keeps the writer
// simple; the geometry follows the Microsoft FAT spec (FATSz computation included)
// so `fsck.fat` and a real UEFI firmware both accept the volume.

#define SECSZ        512u
#define FAT_RESERVED 32u
#define FAT_NUMFATS  2u
#define ATTR_RO   0x01u
#define ATTR_DIR  0x10u
#define ATTR_ARCH 0x20u
#define ATTR_VOL  0x08u
#define FAT_EOC   0x0FFFFFFFu

// Local byte-compare (the freestanding kernel has no memcmp symbol). 0 == equal.
static int fbcmp(const void* a, const void* b, int n) {
    const uint8_t* x = (const uint8_t*)a; const uint8_t* y = (const uint8_t*)b;
    for (int i = 0; i < n; i++) if (x[i] != y[i]) return 1;
    return 0;
}

// --- little-endian scalar stores/loads --------------------------------------
static void put_le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t get_le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// Compute FAT32 geometry via the FAT-spec FATSz formula (sec_per_clus = 1).
int fat_compute_geom(fat_geom_t* g, uint32_t part_lba, uint32_t total_sectors) {
    if (!g) return -1;
    g->part_lba      = part_lba;
    g->total_sectors = total_sectors;
    g->sec_per_clus  = 1;
    g->reserved      = FAT_RESERVED;
    g->num_fats      = FAT_NUMFATS;
    // FAT spec: FATSz = ceil((TotSec - Reserved) / ((256*SPC + NumFATs)/2))
    uint32_t tmp1 = total_sectors - g->reserved;
    uint32_t tmp2 = (256u * g->sec_per_clus + g->num_fats) / 2u;
    if (tmp2 == 0) return -1;
    g->fat_size    = (tmp1 + (tmp2 - 1)) / tmp2;
    g->data_start  = g->reserved + g->num_fats * g->fat_size;
    if (g->data_start >= total_sectors) return -1;
    g->clusters    = (total_sectors - g->data_start) / g->sec_per_clus;
    if (g->clusters < 65525u) return -1;                 // below this it would be FAT16, not FAT32
    return 0;
}

// Build the FAT32 boot sector (BPB) into a zeroed 512-byte buffer.
static void fat_build_bpb(uint8_t* s, const fat_geom_t* g, uint32_t vol_id) {
    for (int i = 0; i < 512; i++) s[i] = 0;
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90;               // jmp + nop
    __builtin_memcpy(s + 3, "NYXOS   ", 8);              // OEM name
    put_le16(s + 11, SECSZ);                             // bytes/sector
    s[13] = (uint8_t)g->sec_per_clus;                    // sectors/cluster
    put_le16(s + 14, (uint16_t)g->reserved);             // reserved sectors
    s[16] = (uint8_t)g->num_fats;                        // # FATs
    put_le16(s + 17, 0);                                 // root entries (0 on FAT32)
    put_le16(s + 19, 0);                                 // total sectors 16 (0 -> use 32)
    s[21] = 0xF8;                                        // media type (fixed disk)
    put_le16(s + 22, 0);                                 // FATSz16 (0 on FAT32)
    put_le16(s + 24, 32);                                // sectors/track
    put_le16(s + 26, 64);                                // # heads
    put_le32(s + 28, g->part_lba);                       // hidden sectors (partition start)
    put_le32(s + 32, g->total_sectors);                  // total sectors 32
    put_le32(s + 36, g->fat_size);                       // FATSz32
    put_le16(s + 40, 0);                                 // ExtFlags
    put_le16(s + 42, 0);                                 // FS version
    put_le32(s + 44, 2);                                 // root-dir first cluster
    put_le16(s + 48, 1);                                 // FSInfo sector
    put_le16(s + 50, 6);                                 // backup boot sector
    s[64] = 0x80;                                        // drive number
    s[66] = 0x29;                                        // extended boot signature
    put_le32(s + 67, vol_id);                            // volume serial
    __builtin_memcpy(s + 71, "NYXOS EFI  ", 11);         // volume label
    __builtin_memcpy(s + 82, "FAT32   ", 8);             // fs type
    s[510] = 0x55; s[511] = 0xAA;                        // boot signature
}

// Build the FSInfo sector.
static void fat_build_fsinfo(uint8_t* s, uint32_t free_count, uint32_t next_free) {
    for (int i = 0; i < 512; i++) s[i] = 0;
    put_le32(s + 0,     0x41615252u);                    // lead signature "RRaA"
    put_le32(s + 484,   0x61417272u);                    // struct signature "rrAa"
    put_le32(s + 488,   free_count);                     // free cluster count
    put_le32(s + 492,   next_free);                      // next-free hint
    put_le32(s + 508,   0xAA550000u);                    // trail signature
}

// Format a name into an 11-byte 8.3 field ("BOOTX64.EFI" -> "BOOTX64 EFI").
static void fat_83_name(uint8_t out[11], const char* name) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    for (; name[i] && name[i] != '.' && j < 8; i++, j++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        out[j] = (uint8_t)c;
    }
    while (name[i] && name[i] != '.') i++;                // skip past the base
    if (name[i] == '.') {
        i++;
        for (int k = 0; name[i] && k < 3; i++, k++) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            out[8 + k] = (uint8_t)c;
        }
    }
}

// Build a 32-byte directory entry from an already-formatted 8.3 name.
static void fat_dir_entry(uint8_t* e, const uint8_t name83[11], uint8_t attr, uint32_t first_clus, uint32_t size) {
    for (int i = 0; i < 32; i++) e[i] = 0;
    __builtin_memcpy(e + 0, name83, 11);
    e[11] = attr;
    put_le16(e + 14, 0);                                 // create time
    put_le16(e + 16, 0x0021);                            // create date = 1980-01-01 (valid)
    put_le16(e + 18, 0x0021);                            // last access date
    put_le16(e + 20, (uint16_t)(first_clus >> 16));      // first cluster high
    put_le16(e + 22, 0);                                 // write time
    put_le16(e + 24, 0x0021);                            // write date
    put_le16(e + 26, (uint16_t)(first_clus & 0xFFFF));   // first cluster low
    put_le32(e + 28, size);
}

// FAT entry value for cluster `k` under our fixed layout: root(2)/EFI(3)/BOOT(4)
// are one cluster each; BOOTX64.EFI runs [cb, cb+bc) and grub.cfg runs [cg, cg+gc).
static uint32_t fat_value_for(uint32_t k, uint32_t cb, uint32_t bc, uint32_t cg, uint32_t gc) {
    if (k == 0) return 0x0FFFFFF8u;                      // media descriptor in FAT[0]
    if (k == 1) return 0x0FFFFFFFu;                      // FAT[1]
    if (k == 2 || k == 3 || k == 4) return FAT_EOC;      // root / EFI / BOOT (1 cluster each)
    if (k >= cb && k < cb + bc) return (k == cb + bc - 1) ? FAT_EOC : k + 1;
    if (k >= cg && k < cg + gc) return (k == cg + gc - 1) ? FAT_EOC : k + 1;
    return 0;                                            // free
}

#define FAT_ZERO_SECS 64                                        // 32 KB batch for clearing the FATs
static uint8_t fat_sec[SECSZ] __attribute__((aligned(4096)));                  // scratch sector (off-stack)
static uint8_t fat_zero[FAT_ZERO_SECS * SECSZ] __attribute__((aligned(4096))); // stays zero (BSS); never written

// relative sector (within the partition) of a data cluster's first sector
static uint32_t fat_clus_sector(const fat_geom_t* g, uint32_t clus) {
    return g->data_start + (clus - 2) * g->sec_per_clus;
}

int fat_format_esp(uint8_t dev, uint32_t part_lba, uint32_t total_sectors,
                   const void* boot_efi, uint32_t boot_len,
                   const char* cfg, uint32_t cfg_len) {
    fat_geom_t g;
    if (fat_compute_geom(&g, part_lba, total_sectors) != 0) return -1;

    uint32_t clus_bytes = g.sec_per_clus * SECSZ;
    uint32_t cb = 5;                                     // BOOTX64.EFI first cluster
    uint32_t bc = (boot_len + clus_bytes - 1) / clus_bytes; if (!bc) bc = 1;
    uint32_t cg = cb + bc;                               // grub.cfg first cluster
    uint32_t gc = (cfg_len + clus_bytes - 1) / clus_bytes; if (!gc) gc = 1;
    uint32_t last_clus = cg + gc - 1;
    if (last_clus - 2 + 1 > g.clusters) return -2;       // does not fit

    // 1. boot sector (+ backup at sector 6) and FSInfo (+ backup at 7)
    uint32_t used = last_clus - 1;                       // clusters 2..last_clus
    fat_build_bpb(fat_sec, &g, 0x4E595853u /* "NYXS" */);
    if (blk_write1(dev, part_lba + 0, fat_sec) != 0) return -3;
    if (blk_write1(dev, part_lba + 6, fat_sec) != 0) return -3;
    fat_build_fsinfo(fat_sec, g.clusters - used, last_clus + 1);
    if (blk_write1(dev, part_lba + 1, fat_sec) != 0) return -4;
    if (blk_write1(dev, part_lba + 7, fat_sec) != 0) return -4;

    // 2. zero both FATs (batched 32 KB runs), then write the FAT sectors our chains
    //    touch (to both copies). fat_zero is BSS-zero and never written.
    for (uint32_t f = 0; f < g.num_fats; f++) {
        uint32_t base = part_lba + g.reserved + f * g.fat_size, s = 0;
        while (s < g.fat_size) {
            uint32_t n = g.fat_size - s > FAT_ZERO_SECS ? FAT_ZERO_SECS : g.fat_size - s;
            if (blk_write(dev, base + s, n, fat_zero) != 0) return -5;
            s += n;
        }
    }
    uint32_t fat_secs_used = last_clus / 128u + 1u;      // 128 FAT32 entries per 512-B sector
    for (uint32_t s = 0; s < fat_secs_used; s++) {
        __builtin_memset(fat_sec, 0, SECSZ);
        for (uint32_t j = 0; j < 128; j++) {
            uint32_t k = s * 128u + j;
            uint32_t v = fat_value_for(k, cb, bc, cg, gc);
            if (v) put_le32(fat_sec + j * 4, v);
        }
        for (uint32_t f = 0; f < g.num_fats; f++)
            if (blk_write1(dev, part_lba + g.reserved + f * g.fat_size + s, fat_sec) != 0) return -6;
    }

    // 3. root directory (cluster 2): volume-label entry + "EFI" subdir
    uint8_t nm[11];
    __builtin_memset(fat_sec, 0, SECSZ);
    __builtin_memcpy(nm, "NYXOS EFI  ", 11);
    fat_dir_entry(fat_sec + 0,  nm, ATTR_VOL, 0, 0);
    fat_83_name(nm, "EFI");
    fat_dir_entry(fat_sec + 32, nm, ATTR_DIR, 3, 0);
    if (blk_write1(dev, part_lba + fat_clus_sector(&g, 2), fat_sec) != 0) return -7;

    // 4. /EFI (cluster 3): ".", "..", "BOOT"
    __builtin_memset(fat_sec, 0, SECSZ);
    __builtin_memcpy(nm, ".          ", 11); fat_dir_entry(fat_sec + 0,  nm, ATTR_DIR, 3, 0);
    __builtin_memcpy(nm, "..         ", 11); fat_dir_entry(fat_sec + 32, nm, ATTR_DIR, 0, 0);
    fat_83_name(nm, "BOOT");                 fat_dir_entry(fat_sec + 64, nm, ATTR_DIR, 4, 0);
    if (blk_write1(dev, part_lba + fat_clus_sector(&g, 3), fat_sec) != 0) return -8;

    // 5. /EFI/BOOT (cluster 4): ".", "..", "BOOTX64.EFI", "grub.cfg"
    __builtin_memset(fat_sec, 0, SECSZ);
    __builtin_memcpy(nm, ".          ", 11); fat_dir_entry(fat_sec + 0,  nm, ATTR_DIR, 4, 0);
    __builtin_memcpy(nm, "..         ", 11); fat_dir_entry(fat_sec + 32, nm, ATTR_DIR, 3, 0);
    fat_83_name(nm, "BOOTX64.EFI");          fat_dir_entry(fat_sec + 64, nm, ATTR_ARCH, cb, boot_len);
    fat_83_name(nm, "GRUB.CFG");             fat_dir_entry(fat_sec + 96, nm, ATTR_ARCH, cg, cfg_len);
    if (blk_write1(dev, part_lba + fat_clus_sector(&g, 4), fat_sec) != 0) return -9;

    // 6. file data: BOOTX64.EFI then grub.cfg, one cluster (sector) at a time
    const uint8_t* bp = (const uint8_t*)boot_efi;
    for (uint32_t i = 0; i < bc; i++) {
        __builtin_memset(fat_sec, 0, SECSZ);
        uint32_t off = i * clus_bytes, n = boot_len > off ? boot_len - off : 0;
        if (n > clus_bytes) n = clus_bytes;
        if (n) __builtin_memcpy(fat_sec, bp + off, n);
        if (blk_write1(dev, part_lba + fat_clus_sector(&g, cb + i), fat_sec) != 0) return -10;
    }
    const uint8_t* gp = (const uint8_t*)cfg;
    for (uint32_t i = 0; i < gc; i++) {
        __builtin_memset(fat_sec, 0, SECSZ);
        uint32_t off = i * clus_bytes, n = cfg_len > off ? cfg_len - off : 0;
        if (n > clus_bytes) n = clus_bytes;
        if (n) __builtin_memcpy(fat_sec, gp + off, n);
        if (blk_write1(dev, part_lba + fat_clus_sector(&g, cg + i), fat_sec) != 0) return -11;
    }

    if (dev != BLK_NVME0) ata_flush();
    return 0;
}

// KAT: BPB byte-layout, FATSz geometry, FAT-entry pack, 8.3 naming, dir entries.
int fat_selftest(void) {
    // geometry for a 64 MiB ESP (131072 sectors): FATSz = 1016, clusters >= 65525
    fat_geom_t g;
    if (fat_compute_geom(&g, 2048, 131072) != 0) return 1;
    if (g.reserved != 32 || g.num_fats != 2 || g.sec_per_clus != 1) return 2;
    if (g.fat_size != 1016) return 3;                    // (131072-32) / ((256+2)/2), ceil
    if (g.data_start != 32 + 2 * 1016) return 4;
    if (g.clusters < 65525u) return 5;
    // a tiny volume must be rejected as not-FAT32
    fat_geom_t tg;
    if (fat_compute_geom(&tg, 0, 4096) == 0) return 6;

    // BPB layout
    static uint8_t s[512];
    fat_build_bpb(s, &g, 0x12345678u);
    if (s[0] != 0xEB || s[2] != 0x90)             return 7;
    if (get_le16(s + 11) != 512)                  return 8;   // bytes/sector
    if (s[13] != 1)                               return 9;   // sec/cluster
    if (get_le16(s + 14) != 32)                   return 10;  // reserved
    if (s[16] != 2)                               return 11;  // # FATs
    if (get_le32(s + 32) != 131072)               return 12;  // total sectors
    if (get_le32(s + 36) != 1016)                 return 13;  // FATSz32
    if (get_le32(s + 44) != 2)                    return 14;  // root cluster
    if (get_le32(s + 28) != 2048)                 return 15;  // hidden sectors
    if (get_le32(s + 67) != 0x12345678u)          return 16;  // volume id
    if (fbcmp(s + 82, "FAT32   ", 8))  return 17;
    if (s[510] != 0x55 || s[511] != 0xAA)         return 18;

    // FSInfo
    fat_build_fsinfo(s, 100, 7);
    if (get_le32(s + 0)   != 0x41615252u)         return 19;
    if (get_le32(s + 484) != 0x61417272u)         return 20;
    if (get_le32(s + 488) != 100)                 return 21;
    if (get_le32(s + 508) != 0xAA550000u)         return 22;

    // 8.3 name formatting
    uint8_t nm[11];
    fat_83_name(nm, "BOOTX64.EFI");
    if (fbcmp(nm, "BOOTX64 EFI", 11))  return 23;
    fat_83_name(nm, "grub.cfg");
    if (fbcmp(nm, "GRUB    CFG", 11))  return 24;   // lowercased -> upper
    fat_83_name(nm, "EFI");
    if (fbcmp(nm, "EFI        ", 11))  return 25;

    // directory entry
    uint8_t e[32];
    fat_83_name(nm, "BOOTX64.EFI");
    fat_dir_entry(e, nm, ATTR_ARCH, 0x00030005u, 4096);
    if (fbcmp(e, "BOOTX64 EFI", 11))   return 26;
    if (e[11] != ATTR_ARCH)                       return 27;
    if (get_le16(e + 20) != 0x0003)               return 28;  // first cluster high
    if (get_le16(e + 26) != 0x0005)               return 29;  // first cluster low
    if (get_le32(e + 28) != 4096)                 return 30;  // size

    // FAT chain values: BOOTX64 = [5,5+4), grub.cfg = [9,9+1)
    if (fat_value_for(0, 5, 4, 9, 1) != 0x0FFFFFF8u) return 31;
    if (fat_value_for(1, 5, 4, 9, 1) != 0x0FFFFFFFu) return 32;
    if (fat_value_for(2, 5, 4, 9, 1) != FAT_EOC)     return 33;  // root, 1 cluster
    if (fat_value_for(4, 5, 4, 9, 1) != FAT_EOC)     return 34;  // BOOT, 1 cluster
    if (fat_value_for(5, 5, 4, 9, 1) != 6)           return 35;  // BOOTX64 cluster 0 -> next
    if (fat_value_for(8, 5, 4, 9, 1) != FAT_EOC)     return 36;  // BOOTX64 last -> EOC
    if (fat_value_for(9, 5, 4, 9, 1) != FAT_EOC)     return 37;  // grub.cfg, 1 cluster
    if (fat_value_for(10, 5, 4, 9, 1) != 0)          return 38;  // free
    return 0;
}
