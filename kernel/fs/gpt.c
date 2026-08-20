#include "../core/kernel.h"
#include "../drivers/misc/blockdev.h"
#include "gpt.h"

// Standard type GUIDs, already in on-disk (mixed-endian) byte order.
const uint8_t GPT_TYPE_ESP[16] = {
    0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B };
const uint8_t GPT_TYPE_LINUX[16] = {
    0xAF,0x3D,0xC6,0x0F, 0x83,0x84, 0x72,0x47, 0x8E,0x79, 0x3D,0x69,0xD8,0x47,0x7D,0xE4 };

#define GPT_SIG      "EFI PART"
#define GPT_REVISION 0x00010000u
#define GPT_HDR_SIZE 92u
#define GPT_NUM_ENTRIES 128u
#define GPT_ENTRY_SIZE  128u
#define GPT_ARRAY_BYTES (GPT_NUM_ENTRIES * GPT_ENTRY_SIZE)     // 16384
#define GPT_ARRAY_SECTORS (GPT_ARRAY_BYTES / 512)              // 32

// Local byte-compare (the freestanding kernel has no memcmp symbol). 0 == equal.
static int gbcmp(const void* a, const void* b, int n) {
    const uint8_t* x = (const uint8_t*)a; const uint8_t* y = (const uint8_t*)b;
    for (int i = 0; i < n; i++) if (x[i] != y[i]) return 1;
    return 0;
}

// --- little-endian scalar store/load (GPT is defined little-endian) ---------
static void put_le16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put_le64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get_le32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t get_le64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

// Protective MBR (LBA 0): a single 0xEE partition covering the whole disk so
// legacy tooling sees the disk as "used" and won't clobber the GPT.
void gpt_build_pmbr(uint8_t* sec, uint64_t disk_sectors) {
    for (int i = 0; i < 512; i++) sec[i] = 0;
    uint8_t* e = sec + 446;
    e[0] = 0x00;                          // status (not bootable)
    e[1] = 0x00; e[2] = 0x02; e[3] = 0x00;// CHS of first sector (LBA 1)
    e[4] = 0xEE;                          // type = GPT protective
    e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF;// CHS of last sector (saturated)
    put_le32(e + 8, 1);                   // first LBA = 1
    uint64_t sz = disk_sectors ? disk_sectors - 1 : 0;
    put_le32(e + 12, sz > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)sz);
    sec[510] = 0x55; sec[511] = 0xAA;
}

// GPT header into a zeroed 512-byte sector. The 92-byte header's own CRC-32 is
// computed last, over the header with the HeaderCRC32 field held at zero.
void gpt_build_header(uint8_t* sec, uint64_t my_lba, uint64_t alt_lba,
                      uint64_t first_usable, uint64_t last_usable,
                      uint64_t part_entry_lba, uint32_t num_entries,
                      uint32_t entry_size, uint32_t array_crc,
                      const uint8_t disk_guid[16]) {
    for (int i = 0; i < 512; i++) sec[i] = 0;
    __builtin_memcpy(sec + 0x00, GPT_SIG, 8);
    put_le32(sec + 0x08, GPT_REVISION);
    put_le32(sec + 0x0C, GPT_HDR_SIZE);
    put_le32(sec + 0x10, 0);              // HeaderCRC32 (zero while hashing)
    put_le32(sec + 0x14, 0);              // reserved
    put_le64(sec + 0x18, my_lba);
    put_le64(sec + 0x20, alt_lba);
    put_le64(sec + 0x28, first_usable);
    put_le64(sec + 0x30, last_usable);
    __builtin_memcpy(sec + 0x38, disk_guid, 16);
    put_le64(sec + 0x48, part_entry_lba);
    put_le32(sec + 0x50, num_entries);
    put_le32(sec + 0x54, entry_size);
    put_le32(sec + 0x58, array_crc);
    put_le32(sec + 0x10, crc32_calc(sec, GPT_HDR_SIZE));
}

// One 128-byte partition entry. `name` is stored as UTF-16LE (ASCII subset).
void gpt_build_entry(uint8_t* e, const uint8_t type_guid[16], const uint8_t uniq_guid[16],
                     uint64_t start_lba, uint64_t end_lba, uint64_t attrs, const char* name) {
    for (int i = 0; i < 128; i++) e[i] = 0;
    __builtin_memcpy(e + 0x00, type_guid, 16);
    __builtin_memcpy(e + 0x10, uniq_guid, 16);
    put_le64(e + 0x20, start_lba);
    put_le64(e + 0x28, end_lba);
    put_le64(e + 0x30, attrs);
    for (int i = 0; i < 36 && name && name[i]; i++)
        put_le16(e + 0x38 + i * 2, (uint16_t)(uint8_t)name[i]);
}

static uint8_t gpt_scratch[512];          // off-stack CRC recompute buffer (block layer is single-threaded)

int gpt_parse_header(const uint8_t* sec, gpt_header_t* out) {
    if (gbcmp(sec, GPT_SIG, 8) != 0) return -1;
    uint32_t hsize = get_le32(sec + 0x0C);
    if (hsize < GPT_HDR_SIZE || hsize > 512) return -2;
    uint32_t stored = get_le32(sec + 0x10);
    __builtin_memcpy(gpt_scratch, sec, 512);
    put_le32(gpt_scratch + 0x10, 0);
    if (crc32_calc(gpt_scratch, hsize) != stored) return -3;
    if (out) {
        out->my_lba         = get_le64(sec + 0x18);
        out->alt_lba        = get_le64(sec + 0x20);
        out->first_usable   = get_le64(sec + 0x28);
        out->last_usable    = get_le64(sec + 0x30);
        __builtin_memcpy(out->disk_guid, sec + 0x38, 16);
        out->part_entry_lba = get_le64(sec + 0x48);
        out->num_entries    = get_le32(sec + 0x50);
        out->entry_size     = get_le32(sec + 0x54);
        out->array_crc      = get_le32(sec + 0x58);
    }
    return 0;
}

int gpt_parse_entry(const uint8_t* e, gpt_entry_t* out) {
    int used = 0;
    for (int i = 0; i < 16; i++) if (e[i]) { used = 1; break; }
    if (!used) return 1;                  // zero type GUID -> unused slot
    if (out) {
        __builtin_memcpy(out->type_guid, e + 0x00, 16);
        __builtin_memcpy(out->uniq_guid, e + 0x10, 16);
        out->start_lba = get_le64(e + 0x20);
        out->end_lba   = get_le64(e + 0x28);
        out->attrs     = get_le64(e + 0x30);
        int j = 0;
        for (; j < 36; j++) {             // decode the ASCII subset of the UTF-16LE name
            uint16_t c = (uint16_t)e[0x38 + j * 2] | ((uint16_t)e[0x38 + j * 2 + 1] << 8);
            if (!c) break;
            out->name[j] = (c < 0x80) ? (char)c : '?';
        }
        out->name[j] = 0;
    }
    return 0;
}

// --- device-level: write / list --------------------------------------------
static uint8_t gpt_arr[GPT_ARRAY_BYTES] __attribute__((aligned(4096)));  // 16 KB entry array (off-stack)
static uint8_t gpt_hdr[512];

int gpt_write_disk(uint8_t dev, uint64_t disk_sectors,
                   uint64_t esp_start, uint64_t esp_sectors,
                   uint64_t nyx_start, uint64_t nyx_sectors,
                   const uint8_t disk_guid[16], const uint8_t esp_guid[16], const uint8_t nyx_guid[16]) {
    if (disk_sectors < 2 * GPT_ARRAY_SECTORS + 4) return -1;

    uint64_t entry_lba    = 2;
    uint64_t first_usable = entry_lba + GPT_ARRAY_SECTORS;          // 34
    uint64_t backup_hdr   = disk_sectors - 1;
    uint64_t backup_arr   = backup_hdr - GPT_ARRAY_SECTORS;          // disk-33
    uint64_t last_usable  = backup_arr - 1;                          // disk-34

    // sanity: both partitions inside the usable window, ESP before NyxOS
    if (esp_start < first_usable || esp_start + esp_sectors - 1 > last_usable) return -2;
    if (nyx_start < esp_start + esp_sectors || nyx_start + nyx_sectors - 1 > last_usable) return -3;

    // 1. partition entry array (entry 0 = ESP, entry 1 = NyxOS ext2)
    __builtin_memset(gpt_arr, 0, sizeof(gpt_arr));
    gpt_build_entry(gpt_arr + 0 * GPT_ENTRY_SIZE, GPT_TYPE_ESP,   esp_guid,
                    esp_start, esp_start + esp_sectors - 1, 0, "EFI System");
    gpt_build_entry(gpt_arr + 1 * GPT_ENTRY_SIZE, GPT_TYPE_LINUX, nyx_guid,
                    nyx_start, nyx_start + nyx_sectors - 1, 0, "NyxOS");
    uint32_t acrc = crc32_calc(gpt_arr, GPT_ARRAY_BYTES);

    // 2. protective MBR + primary header + primary array
    gpt_build_pmbr(gpt_hdr, disk_sectors);
    if (blk_write1(dev, 0, gpt_hdr) != 0) return -4;
    gpt_build_header(gpt_hdr, 1, backup_hdr, first_usable, last_usable,
                     entry_lba, GPT_NUM_ENTRIES, GPT_ENTRY_SIZE, acrc, disk_guid);
    if (blk_write1(dev, 1, gpt_hdr) != 0) return -5;
    if (blk_write(dev, (uint32_t)entry_lba, GPT_ARRAY_SECTORS, gpt_arr) != 0) return -6;

    // 3. backup array + backup header (same array, my/alt + entry LBA swapped)
    if (blk_write(dev, (uint32_t)backup_arr, GPT_ARRAY_SECTORS, gpt_arr) != 0) return -7;
    gpt_build_header(gpt_hdr, backup_hdr, 1, first_usable, last_usable,
                     backup_arr, GPT_NUM_ENTRIES, GPT_ENTRY_SIZE, acrc, disk_guid);
    if (blk_write1(dev, (uint32_t)backup_hdr, gpt_hdr) != 0) return -8;

    blk_flush(dev);
    return 0;
}

static const char* gpt_type_name(const uint8_t* g) {
    if (gbcmp(g, GPT_TYPE_ESP, 16) == 0)   return "EFI System";
    if (gbcmp(g, GPT_TYPE_LINUX, 16) == 0) return "Linux filesystem";
    return "other";
}

int gpt_list(uint8_t dev) {
    if (blk_read1(dev, 1, gpt_hdr) != 0) return -1;
    gpt_header_t h;
    if (gpt_parse_header(gpt_hdr, &h) != 0) return -1;
    printf("  GPT: %u entries x %u B, usable LBA %u..%u\n",
           h.num_entries, h.entry_size, (uint32_t)h.first_usable, (uint32_t)h.last_usable);
    uint32_t shown = 0;
    for (uint32_t i = 0; i < h.num_entries && i < GPT_NUM_ENTRIES; i++) {
        uint32_t off = i * h.entry_size;
        if (off + h.entry_size > GPT_ARRAY_BYTES) break;
        if ((off % 512) == 0)                                   // (re)load the sector this entry lives in
            if (blk_read1(dev, (uint32_t)h.part_entry_lba + off / 512, gpt_arr) != 0) return -1;
        gpt_entry_t e;
        if (gpt_parse_entry(gpt_arr + (off % 512), &e) != 0) continue;
        uint32_t mib = (uint32_t)(((e.end_lba - e.start_lba + 1) * 512ULL) >> 20);
        printf("  p%u  %-16s start %-9u %u MiB  \"%s\"\n",
               i + 1, gpt_type_name(e.type_guid), (uint32_t)e.start_lba, mib, e.name);
        shown++;
    }
    if (!shown) printf("  (no partitions defined)\n");
    return 0;
}

// Locate the first partition of a given type GUID in the on-disk GPT, returning its
// LBA range. Powers the ESP (FAT writer) + NyxOS (ext2 install) lookups. 0 ok, -1 none.
static int gpt_find_type(uint8_t dev, const uint8_t type[16], uint64_t* start, uint64_t* sectors) {
    if (blk_read1(dev, 1, gpt_hdr) != 0) return -1;
    gpt_header_t h;
    if (gpt_parse_header(gpt_hdr, &h) != 0) return -1;
    for (uint32_t i = 0; i < h.num_entries && i < GPT_NUM_ENTRIES; i++) {
        uint32_t off = i * h.entry_size;
        if (off + h.entry_size > GPT_ARRAY_BYTES) break;
        if ((off % 512) == 0)
            if (blk_read1(dev, (uint32_t)h.part_entry_lba + off / 512, gpt_arr) != 0) return -1;
        gpt_entry_t e;
        if (gpt_parse_entry(gpt_arr + (off % 512), &e) != 0) continue;
        if (gbcmp(e.type_guid, type, 16) == 0) {
            if (start)   *start   = e.start_lba;
            if (sectors) *sectors = e.end_lba - e.start_lba + 1;
            return 0;
        }
    }
    return -1;
}
int gpt_find_esp(uint8_t dev, uint64_t* start, uint64_t* sectors) { return gpt_find_type(dev, GPT_TYPE_ESP, start, sectors); }
int gpt_find_nyx(uint8_t dev, uint64_t* start, uint64_t* sectors) { return gpt_find_type(dev, GPT_TYPE_LINUX, start, sectors); }

// KAT: exercise the CRC + header/entry byte layout + a build->parse round-trip.
// Vectors are hand-checked against the UEFI spec's field offsets; the whole
// on-disk image is separately validated on the host with `sgdisk -v`/`gdisk`.
int gpt_selftest(void) {
    // CRC-32 is the reflected IEEE variant GPT mandates (== crc32_calc's KAT vector).
    if (crc32_calc((const uint8_t*)"123456789", 9) != 0xCBF43926u) return 1;

    // protective MBR
    static uint8_t sec[512];
    gpt_build_pmbr(sec, 0x00100000ULL);           // 1,048,576 sectors
    if (sec[446 + 4] != 0xEE)             return 2;   // type 0xEE
    if (get_le32(sec + 446 + 8) != 1)     return 3;   // first LBA
    if (get_le32(sec + 446 + 12) != 0x000FFFFFu) return 4; // size = disk-1
    if (sec[510] != 0x55 || sec[511] != 0xAA)    return 5;

    // a partition entry, read back
    static uint8_t ent[128];
    gpt_build_entry(ent, GPT_TYPE_ESP, GPT_TYPE_LINUX, 2048, 133119, 0, "EFI System");
    gpt_entry_t pe;
    if (gpt_parse_entry(ent, &pe) != 0)          return 6;
    if (gbcmp(pe.type_guid, GPT_TYPE_ESP, 16) != 0) return 7;
    if (pe.start_lba != 2048 || pe.end_lba != 133119)         return 8;
    if (strcmp(pe.name, "EFI System") != 0)                   return 9;
    static uint8_t zero[128];
    for (int i = 0; i < 128; i++) zero[i] = 0;
    if (gpt_parse_entry(zero, 0) != 1)           return 10;  // empty slot detected

    // build an array with that one entry, hash it, feed it into a header
    static uint8_t arr[GPT_ARRAY_BYTES];
    for (uint32_t i = 0; i < GPT_ARRAY_BYTES; i++) arr[i] = 0;
    __builtin_memcpy(arr, ent, 128);
    uint32_t acrc = crc32_calc(arr, GPT_ARRAY_BYTES);

    static const uint8_t dg[16] = { 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                                    0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x01 };
    gpt_build_header(sec, 1, 0x000FFFFFULL, 34, 0x000FFFDEULL, 2,
                     GPT_NUM_ENTRIES, GPT_ENTRY_SIZE, acrc, dg);
    if (gbcmp(sec, GPT_SIG, 8) != 0)  return 11;
    if (get_le32(sec + 0x08) != GPT_REVISION)    return 12;
    if (get_le32(sec + 0x0C) != GPT_HDR_SIZE)    return 13;

    gpt_header_t h;
    if (gpt_parse_header(sec, &h) != 0)          return 14;  // signature + header CRC valid
    if (h.my_lba != 1 || h.alt_lba != 0x000FFFFFULL) return 15;
    if (h.first_usable != 34 || h.last_usable != 0x000FFFDEULL) return 16;
    if (h.part_entry_lba != 2)                   return 17;
    if (h.num_entries != GPT_NUM_ENTRIES || h.entry_size != GPT_ENTRY_SIZE) return 18;
    if (h.array_crc != acrc)                     return 19;
    if (gbcmp(h.disk_guid, dg, 16) != 0) return 20;

    // tamper: flipping a header byte must break the header CRC
    sec[0x18] ^= 0xFF;
    if (gpt_parse_header(sec, 0) != -3)          return 21;
    sec[0x18] ^= 0xFF;
    if (gpt_parse_header(sec, 0) != 0)           return 22;  // restored
    // a non-GPT sector is rejected
    sec[0] = 'X';
    if (gpt_parse_header(sec, 0) != -1)          return 23;
    return 0;
}
