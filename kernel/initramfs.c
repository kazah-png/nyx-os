#include "kernel.h"
#include "initramfs.h"

// Generated initramfs data (from mkinitramfs.py -c mode)
#include "initramfs_data.h"

static uint8_t* initramfs_data_ptr = (uint8_t*)initramfs_data;
static uint32_t initramfs_size = INITRAMFS_SIZE;

static uint32_t parse_hex(const char* s, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        val <<= 4;
        char c = s[i];
        if (c >= '0' && c <= '9') val |= c - '0';
        else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    }
    return val;
}

static uint32_t align4(uint32_t x) {
    return (x + 3) & ~3;
}

int initramfs_load(void) {
    if (!initramfs_data_ptr || initramfs_size < sizeof(cpio_header_t)) {
        printf("[INITRAMFS] No initramfs data (size=%u)\n", initramfs_size);
        return -1;
    }
    printf("[INITRAMFS] Data at 0x%x, %u bytes\n", (uint32_t)(uintptr_t)initramfs_data_ptr, initramfs_size);
    return 0;
}

void initramfs_boot(void) {
    if (!initramfs_data_ptr) return;
    printf("[INITRAMFS] Loading...\n");

    uint32_t offset = 0;
    int file_count = 0;

    while (offset + sizeof(cpio_header_t) <= initramfs_size) {
        cpio_header_t* hdr = (cpio_header_t*)(initramfs_data_ptr + offset);
        if (strncmp(hdr->c_magic, CPIO_MAGIC, 6) != 0) break;

        uint32_t namesize = parse_hex(hdr->c_namesize, 8);
        uint32_t filesize = parse_hex(hdr->c_filesize, 8);

        uint32_t name_offset = offset + sizeof(cpio_header_t);
        const char* name = (const char*)(initramfs_data_ptr + name_offset);

        uint32_t name_padded = align4(sizeof(cpio_header_t) + namesize);
        uint32_t data_offset = offset + name_padded;

        if (strcmp(name, "TRAILER!!!") == 0) break;

        if (filesize > 0) {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "/%s", name);
            vfs_create_from_mem(path, initramfs_data_ptr + data_offset, filesize);
            file_count++;
        }

        uint32_t data_padded = align4(filesize);
        offset = data_offset + data_padded;
    }

    printf("[INITRAMFS] Loaded %d files\n", file_count);
}
