#include "kernel.h"
#include "ata.h"
#include "ext2.h"
#include "spinlock.h"

ext2_fs_t ext2_fs;

// SMP FS lock (T1 track, v5.9.25). The role-split scratch buffers below
// (ext2_bufs[5]) make the driver safe against SAME-THREAD nested block reads — but
// they are global, so two cores running ext2 operations at once (a /mnt file op on
// one core, a login passwd check on another) would trample all five and corrupt
// on-disk metadata — the exact class of bug the buffer comment warns can "survive a
// reboot". The preempt_disable() in the vfs.c wrappers was a single-core FS lock,
// meaningless to another core. This is the real one: every EXTERNAL entry into the
// driver (the vfs.c fs_* mount wrappers and auth.c's passwd helpers) wraps its
// ext2_* call in ext2_lock_acquire/release. ext2.c itself never takes it — the
// public functions call each other (write_file/mkdir/unlink -> ext2_resolve), so an
// inner acquire would self-deadlock; the caller's hold is what serializes. Held
// across the whole op including PIO block I/O (the ATA path is polled — no ATA IRQ
// handler — so interrupts-off is safe). ext2_mount runs once at boot on the BSP
// before any AP or user process, so it is intentionally left unlocked.
static spinlock_t ext2_lock = SPINLOCK_INIT;
uint64_t ext2_lock_acquire(void)        { return spin_lock_irqsave(&ext2_lock); }
void     ext2_lock_release(uint64_t fl) { spin_unlock_irqrestore(&ext2_lock, fl); }

/* Bytes an on-disk directory entry occupies before its name: inode + rec_len +
 * name_len + file_type. `de->name` lives at +8, not +4 — the driver had that
 * wrong in four places and wrote past the end of the block buffer each time. */
#define EXT2_DIRENT_HDR   8u


// ---- Sector cache (v5.9.26) -------------------------------------------------
// Every disk access funnels through read_sectors / write_sectors, so a cache here
// is coherent BY CONSTRUCTION: a write refreshes the cached line in place and a read
// consults it first, so there is no higher-level invalidation to get wrong — the
// class of bug that makes FS caches dangerous. Direct-mapped by sector number,
// write-through (the disk is always current, so nothing to flush and a crash can't
// lose a cached write). It removes the driver's biggest cost: ext2 re-reads the same
// metadata blocks (superblock, group descriptors, inode tables, bitmaps) on every
// single path resolution, and each block was fetched A SECTOR AT A TIME over PIO.
// Guarded by ext2_lock like the rest of the driver (read/write_sectors run only
// under it, except boot mount which is single-core), so the lines need no lock of
// their own. 256 lines x 512 B = 128 KB of static BSS (zero-init, not in the binary).
#define SC_LINES 256
static uint8_t  sc_data[SC_LINES][512];
static uint32_t sc_tag[SC_LINES];       // FS-relative sector number cached in this line
static uint8_t  sc_valid[SC_LINES];
static uint64_t sc_hits = 0, sc_misses = 0;

static void sc_read1(uint32_t lba, void* buf) {
    uint32_t i = lba % SC_LINES;
    if (sc_valid[i] && sc_tag[i] == lba) {              // hit: serve from cache, no disk
        __builtin_memcpy(buf, sc_data[i], 512);
        sc_hits++;
        return;
    }
    ata_read_sectors(ext2_fs.drive, ext2_fs.part_start_lba + lba, 1, buf);
    __builtin_memcpy(sc_data[i], buf, 512);
    sc_tag[i] = lba; sc_valid[i] = 1;
    sc_misses++;
}

static void sc_write1(uint32_t lba, const void* buf) {
    ata_write_sectors(ext2_fs.drive, ext2_fs.part_start_lba + lba, 1, buf);
    uint32_t i = lba % SC_LINES;
    __builtin_memcpy(sc_data[i], buf, 512);            // write-through: keep the line coherent
    sc_tag[i] = lba; sc_valid[i] = 1;
}

static void read_sectors(uint32_t lba, uint8_t count, void* buf) {
    for (uint8_t i = 0; i < count; i++) sc_read1(lba + i, (uint8_t*)buf + (uint32_t)i * 512);
}

// Cache stats for `df`.
void ext2_cache_stats(uint64_t* hits, uint64_t* misses) {
    if (hits)   *hits   = sc_hits;
    if (misses) *misses = sc_misses;
}

static uint32_t block_to_lba(uint32_t block) {
    uint32_t block_size = ext2_fs.block_size;
    uint32_t sectors_per_block = block_size / 512;
    return block * sectors_per_block;
}

/* THREE buffers, not one, and the distinction is the whole point.
 *
 * There used to be a single shared staging buffer, handed to everyone who needed
 * to hold a block. That is safe only if no holder ever calls another block-layer
 * function while its data is live — and they do, constantly. The worst case was
 * free_inode_blocks(): it read an indirect block into the shared buffer, then
 * walked it calling ext2_free_block() on each entry — and ext2_free_block loads
 * the block BITMAP into that same buffer. After the very first free, the array
 * being iterated WAS the bitmap, so the loop went on "freeing" bitmap bytes
 * reinterpreted as block numbers. Deleting one file could release arbitrary
 * blocks across the disk, and this is the one subsystem whose damage survives a
 * reboot.
 *
 * Splitting by ROLE makes the reentrancy safe by construction rather than by
 * everyone remembering:
 *   0 = general staging (the caller's data block)
 *   1 = bitmap staging  (allocator/free paths only — never a caller's data)
 *   2 = auxiliary       (a nested read while 0 is still live)
 *   3 = auxiliary 2     (the doubly-indirect walk needs two levels live)
 *   4 = metadata        (BGD / inode reads, consumed immediately — these are
 *                        called FROM the free path, so they must not land on
 *                        the caller's staged block either)
 */
static uint8_t* ext2_bufs[5]     = { NULL, NULL, NULL, NULL, NULL };
static uint32_t ext2_buf_sizes[5] = { 0, 0, 0, 0, 0 };

static uint8_t* get_buf_n(int n) {
    if (ext2_bufs[n] && ext2_buf_sizes[n] >= ext2_fs.block_size)
        return ext2_bufs[n];
    if (ext2_bufs[n]) kfree(ext2_bufs[n]);
    ext2_bufs[n] = (uint8_t*)kmalloc(ext2_fs.block_size);
    ext2_buf_sizes[n] = ext2_bufs[n] ? ext2_fs.block_size : 0;
    return ext2_bufs[n];
}

static uint8_t* get_block_buf(void)  { return get_buf_n(0); }
static uint8_t* get_bitmap_buf(void) { return get_buf_n(1); }
static uint8_t* get_aux_buf(void)    { return get_buf_n(2); }
static uint8_t* get_aux2_buf(void)   { return get_buf_n(3); }
static uint8_t* get_meta_buf(void)   { return get_buf_n(4); }

/* Seconds since the Unix epoch, from the RTC.
 *
 * The driver had no time source at all, so every inode field that holds one was
 * left at zero. For atime/mtime that is merely ugly (every file dates to 1970);
 * for DTIME it is a real inconsistency, because ext2 defines a freed inode as
 * one with a non-zero dtime and e2fsck reports "deleted inode has zero dtime"
 * for every file NyxOS has ever removed. Civil-date-to-days is Howard Hinnant's
 * days_from_civil, which is exact for the whole proleptic Gregorian range. */
static uint32_t ext2_now(void) {
    rtc_time_t t;
    rtc_read_time(&t);
    if (t.year < 1970 || t.month < 1 || t.month > 12) return 0;

    int32_t y = (int32_t)t.year;
    uint32_t m = t.month, d = t.day ? t.day : 1;
    y -= (m <= 2);
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153 * (m + (m > 2 ? (uint32_t)-3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return (uint32_t)(days * 86400 + t.hour * 3600 + t.minute * 60 + t.second);
}

static int read_block_group_bgd(uint32_t group, ext2_bgd_t* bgd) {
    uint32_t bgd_per_block = ext2_fs.block_size / sizeof(ext2_bgd_t);
    uint32_t bgd_block_offset = group / bgd_per_block;
    uint32_t bgd_in_block = group % bgd_per_block;

    uint32_t block = ext2_fs.bgd_block + bgd_block_offset;
    uint8_t* buf = get_meta_buf();
    if (!buf) return -1;

    ext2_read_block(block, buf);
    __builtin_memcpy(bgd, buf + bgd_in_block * sizeof(ext2_bgd_t), sizeof(ext2_bgd_t));
    return 0;
}

void init_ext2(void) {
    for (int i = 0; i < 5; i++) { ext2_bufs[i] = NULL; ext2_buf_sizes[i] = 0; }
}

int ext2_mount(uint8_t drive, uint32_t part_lba) {
    ext2_fs.drive = drive;
    ext2_fs.part_start_lba = part_lba;

    uint8_t sb_buf[1024];
    read_sectors(2, 2, sb_buf);
    __builtin_memcpy(&ext2_fs.sb, sb_buf, sizeof(ext2_superblock_t));

    if (ext2_fs.sb.magic != EXT2_SUPER_MAGIC)
        return -1;

    /* Every geometry field below comes off the DISK, and each one of them fed
     * either a divide or a buffer size further down:
     *   log_block_size -> `1024 << n`, undefined for n >= 22 and absurd well
     *                     before that; it sizes every kmalloc'd block buffer.
     *   inodes_per_group -> the divisor in ext2_read_inode. Zero is a #DE, i.e.
     *                     a kernel fault raised by plugging in a bad image.
     *   inode_size     -> block_size / inode_size. Zero-or-too-big is the same #DE.
     *   *_per_group    -> the bit count handed to find_free_bit, which walks a
     *                     buffer only block_size*8 bits long.
     * A corrupt or hostile image is not an exotic input for a filesystem driver —
     * it is the normal one. Validate once here, so nothing downstream has to. */
    if (ext2_fs.sb.log_block_size > 2) return -1;          /* 1024 / 2048 / 4096 */
    ext2_fs.block_size = 1024 << ext2_fs.sb.log_block_size;
    ext2_fs.inodes_per_group = ext2_fs.sb.inodes_per_group;
    ext2_fs.blocks_per_group = ext2_fs.sb.blocks_per_group;
    ext2_fs.inode_size = ext2_fs.sb.inode_size ? ext2_fs.sb.inode_size : 128;

    if (ext2_fs.inodes_per_group == 0 || ext2_fs.blocks_per_group == 0) return -1;
    if (ext2_fs.inode_size < sizeof(ext2_inode_t) ||
        ext2_fs.inode_size > ext2_fs.block_size) return -1;
    if (ext2_fs.blocks_per_group > ext2_fs.block_size * 8) return -1;
    if (ext2_fs.inodes_per_group > ext2_fs.block_size * 8) return -1;

    if (ext2_fs.block_size == 1024)
        ext2_fs.bgd_block = 2;
    else
        ext2_fs.bgd_block = 1;

    uint32_t block_groups = (ext2_fs.sb.total_blocks + ext2_fs.blocks_per_group - 1) / ext2_fs.blocks_per_group;
    uint32_t bgd_size = block_groups * sizeof(ext2_bgd_t);
    ext2_fs.bgd_blocks = (bgd_size + ext2_fs.block_size - 1) / ext2_fs.block_size;

    return 0;
}

int ext2_read_inode(uint32_t ino, ext2_inode_t* inode) {
    if (ino == 0) return -1;
    uint32_t group = (ino - 1) / ext2_fs.inodes_per_group;
    uint32_t index = (ino - 1) % ext2_fs.inodes_per_group;

    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return -1;

    uint32_t inode_table_block = bgd.inode_table;
    uint32_t bytes_per_inode = ext2_fs.inode_size;
    uint32_t inodes_per_block = ext2_fs.block_size / bytes_per_inode;
    uint32_t block = inode_table_block + index / inodes_per_block;
    uint32_t offset_in_block = (index % inodes_per_block) * bytes_per_inode;

    uint8_t* buf = get_meta_buf();
    if (!buf) return -1;

    ext2_read_block(block, buf);
    __builtin_memcpy(inode, buf + offset_in_block, sizeof(ext2_inode_t));
    return 0;
}

int ext2_read_block(uint32_t block, void* buf) {
    uint32_t lba = block_to_lba(block);
    uint32_t sectors = ext2_fs.block_size / 512;
    for (uint32_t i = 0; i < sectors; i++)
        read_sectors(lba + i, 1, (uint8_t*)buf + i * 512);
    return 0;
}

int ext2_read_inode_block(ext2_inode_t* inode, uint32_t iblock, void* buf) {
    uint32_t ptrs_per_block = ext2_fs.block_size / 4;

    if (iblock < 12) {
        if (inode->block[iblock] == 0) return -1;
        return ext2_read_block(inode->block[iblock], buf);
    }

    iblock -= 12;
    if (iblock < ptrs_per_block) {
        if (inode->block[12] == 0) return -1;
        uint32_t* indirect = (uint32_t*)get_aux_buf();   /* buf may BE buffer 0 */
        ext2_read_block(inode->block[12], indirect);
        if (indirect[iblock] == 0) return -1;
        return ext2_read_block(indirect[iblock], buf);
    }

    iblock -= ptrs_per_block;
    if (iblock < ptrs_per_block * ptrs_per_block) {
        if (inode->block[13] == 0) return -1;
        uint32_t* dindirect = (uint32_t*)get_aux_buf();
        ext2_read_block(inode->block[13], dindirect);
        uint32_t block_idx = dindirect[iblock / ptrs_per_block];
        if (block_idx == 0) return -1;
        uint32_t* indirect = (uint32_t*)get_aux2_buf(); /* dindirect[] still live */
        ext2_read_block(block_idx, indirect);
        uint32_t target = indirect[iblock % ptrs_per_block];
        if (target == 0) return -1;
        return ext2_read_block(target, buf);
    }

    return -1;
}

static uint32_t resolve_path(const char* path) {
    if (!path || path[0] != '/') return 0;
    if (path[1] == '\0') return EXT2_ROOT_INO;

    uint32_t cur_ino = EXT2_ROOT_INO;
    char component[MAX_FILENAME];
    const char* p = path + 1;

    while (*p) {
        /* `component[ci++] = *p++` with no ceiling. This is the single most
         * travelled function in the driver — every open, stat, readdir and write
         * enters through it — so an over-long path component was a kernel stack
         * smash reachable from any unprivileged process that could name a file. */
        int ci = 0;
        while (*p && *p != '/') {
            if (ci >= (int)sizeof(component) - 1) return 0;   /* name cannot exist */
            component[ci++] = *p++;
        }
        component[ci] = '\0';
        if (*p == '/') p++;

        if (ci == 0) continue;

        ext2_inode_t dir_inode;
        if (ext2_read_inode(cur_ino, &dir_inode) < 0) return 0;

        int found = 0;
        uint32_t bytes_left = dir_inode.size;
        uint32_t iblock = 0;

        while (bytes_left > 0 && !found) {
            uint8_t* block_buf_local = get_block_buf();
            if (ext2_read_inode_block(&dir_inode, iblock, block_buf_local) < 0) break;
            uint32_t off = 0;
            while (off + EXT2_DIRENT_HDR <= ext2_fs.block_size && off < bytes_left) {
                ext2_dirent_t* de = (ext2_dirent_t*)(block_buf_local + off);
                /* The old guard nudged `off` forward by 1 on a rec_len below the
                 * header size, which kept the loop alive walking misaligned
                 * garbage instead of stopping at an obviously corrupt block. */
                if (de->rec_len < EXT2_DIRENT_HDR) break;
                if (off + de->rec_len > ext2_fs.block_size) break;
                if (de->inode == 0) { off += de->rec_len; continue; }

                uint8_t name_len = de->name_len & 0xFF;
                /* A 255-byte name near the end of the block reads past the buffer;
                 * one longer than a component can be simply cannot match. */
                if (off + EXT2_DIRENT_HDR + name_len > ext2_fs.block_size) break;
                if (name_len >= MAX_FILENAME) { off += de->rec_len; continue; }

                char dname[MAX_FILENAME];
                __builtin_memcpy(dname, de->name, name_len);
                dname[name_len] = '\0';
                if (strcmp(dname, component) == 0) {
                    cur_ino = de->inode;
                    found = 1;
                    break;
                }
                off += de->rec_len;
            }
            bytes_left -= ext2_fs.block_size;
            iblock++;
        }

        if (!found) return 0;
    }

    return cur_ino;
}

// ========== VFS driver functions ==========

uint32_t ext2_resolve(const char* path) {
    return resolve_path(path);
}

uint32_t ext2_get_size(const char* path) {
    uint32_t ino = resolve_path(path);
    if (!ino) return 0;
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return 0;
    return inode.size;
}

int ext2_read_file(const char* path, void* buf, uint32_t maxlen) {
    uint32_t ino = resolve_path(path);
    if (!ino) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if (!(inode.mode & EXT2_S_IFREG)) return -1;

    uint32_t remaining = inode.size;
    if (remaining > maxlen) remaining = maxlen;
    uint32_t total = 0;
    uint32_t iblock = 0;

    while (remaining > 0) {
        uint8_t* block_buf_local = get_block_buf();
        if (ext2_read_inode_block(&inode, iblock, block_buf_local) < 0) break;
        uint32_t chunk = (remaining < ext2_fs.block_size) ? remaining : ext2_fs.block_size;
        __builtin_memcpy((uint8_t*)buf + total, block_buf_local, chunk);
        total += chunk;
        remaining -= chunk;
        iblock++;
    }

    return total;
}

// ========== EXT2 Write Operations ==========

static void write_sectors(uint32_t lba, uint8_t count, const void* buf) {
    for (uint8_t i = 0; i < count; i++) sc_write1(lba + i, (const uint8_t*)buf + (uint32_t)i * 512);
}

int ext2_write_block(uint32_t block, const void* buf) {
    uint32_t lba = block_to_lba(block);
    uint32_t sectors = ext2_fs.block_size / 512;
    for (uint32_t i = 0; i < sectors; i++)
        write_sectors(lba + i, 1, (const uint8_t*)buf + i * 512);
    return 0;
}

int ext2_write_inode(uint32_t ino, const ext2_inode_t* inode) {
    if (ino == 0) return -1;
    uint32_t group = (ino - 1) / ext2_fs.inodes_per_group;
    uint32_t index = (ino - 1) % ext2_fs.inodes_per_group;

    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return -1;

    uint32_t inode_table_block = bgd.inode_table;
    uint32_t bytes_per_inode = ext2_fs.inode_size;
    uint32_t inodes_per_block = ext2_fs.block_size / bytes_per_inode;
    uint32_t block = inode_table_block + index / inodes_per_block;
    uint32_t offset_in_block = (index % inodes_per_block) * bytes_per_inode;

    uint8_t* buf = get_block_buf();
    if (!buf) return -1;

    ext2_read_block(block, buf);
    __builtin_memcpy(buf + offset_in_block, inode, sizeof(ext2_inode_t));
    ext2_write_block(block, buf);
    return 0;
}

static void write_bgd(uint32_t group, const ext2_bgd_t* bgd) {
    uint32_t bgd_per_block = ext2_fs.block_size / sizeof(ext2_bgd_t);
    uint32_t bgd_block_offset = group / bgd_per_block;
    uint32_t bgd_in_block = group % bgd_per_block;
    uint32_t block = ext2_fs.bgd_block + bgd_block_offset;
    uint8_t* buf = get_meta_buf();   /* reached from the free path — never buffer 0 */
    if (!buf) return;
    ext2_read_block(block, buf);
    __builtin_memcpy(buf + bgd_in_block * sizeof(ext2_bgd_t), bgd, sizeof(ext2_bgd_t));
    ext2_write_block(block, buf);
}

/* The block group descriptor tracks how many DIRECTORIES live in the group, and
 * nothing in the driver ever touched it — so e2fsck reported "directories count
 * wrong for group #0" as soon as NyxOS created its second directory. It is
 * advisory (allocators use it to spread directories out) but it is part of the
 * format, and a filesystem is only consistent if the summaries agree. */
static void bgd_adjust_dir_count(int delta) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return;
    if (delta > 0) bgd.used_dirs_count++;
    else if (bgd.used_dirs_count > 0) bgd.used_dirs_count--;
    write_bgd(0, &bgd);
}

int ext2_sync_bgd(uint32_t group) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return -1;
    write_bgd(group, &bgd);
    return 0;
}

int ext2_sync_superblock(void) {
    uint8_t sb_buf[1024];
    __builtin_memset(sb_buf, 0, 1024);
    __builtin_memcpy(sb_buf, &ext2_fs.sb, sizeof(ext2_superblock_t));
    write_sectors(2, 2, sb_buf);
    return 0;
}

// Find a free bit in a bitmap (block or inode)
static int find_free_bit(const uint8_t* bitmap, uint32_t size_bits) {
    for (uint32_t i = 0; i < size_bits; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8))))
            return i;
    }
    return -1;
}

/* Bit index <-> block number, for group 0.
 *
 * ext2 numbers the block bitmap from `first_data_block`, NOT from zero: bit i in
 * group g stands for block `first_data_block + g*blocks_per_group + i`. On the
 * 1024-byte-block image NyxOS actually ships (makefile: mkfs.ext2 -b 1024)
 * first_data_block is 1, and the allocator used the bit index AS the block
 * number — so it handed out block K while marking bit K, which really owns
 * block K+1. Both halves of that are wrong in the same breath: the block it
 * returns is the one the bitmap still calls used (the last inode-table block on
 * a fresh image), and the block it marks used is one nobody holds.
 *
 * It looked like it worked because the collision lands on the TAIL of the inode
 * table — inodes ~897-1024 on the shipped image — which an almost-empty
 * filesystem never reaches. It is a silent overwrite of live metadata that only
 * surfaces once the disk fills up or something runs fsck, and it is written to
 * a real disk, so it outlives the reboot that hides every other bug here.
 *
 * These two must always move together: alloc converts one way, free the other. */
static inline uint32_t bit_to_block(uint32_t bit)   { return ext2_fs.sb.first_data_block + bit; }
static inline uint32_t block_to_bit(uint32_t block) { return block - ext2_fs.sb.first_data_block; }

uint32_t ext2_alloc_block(void) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return 0;

    uint8_t* buf = get_bitmap_buf();
    if (!buf) return 0;

    ext2_read_block(bgd.block_bitmap, buf);

    /* The bitmap is ONE block: it can only describe block_size*8 blocks, however
     * many blocks_per_group claims. ext2_mount rejects an image where those
     * disagree, so this is the belt to that braces — find_free_bit walks a raw
     * byte array and cannot tell where it ends. */
    uint32_t blocks_in_group = ext2_fs.blocks_per_group;
    if (blocks_in_group > ext2_fs.block_size * 8) blocks_in_group = ext2_fs.block_size * 8;
    int bit = find_free_bit(buf, blocks_in_group);
    if (bit < 0) return 0;

    uint32_t block_num = bit_to_block((uint32_t)bit);   // group 0 blocks
    if (block_num >= ext2_fs.sb.total_blocks) return 0;

    // Mark bit as used
    buf[bit / 8] |= (1 << (bit % 8));
    ext2_write_block(bgd.block_bitmap, buf);

    // Update BGD
    if (bgd.free_blocks_count > 0) bgd.free_blocks_count--;
    write_bgd(0, &bgd);

    // Update superblock
    if (ext2_fs.sb.free_blocks > 0) ext2_fs.sb.free_blocks--;

    // Zero out the block before returning
    __builtin_memset(buf, 0, ext2_fs.block_size);
    ext2_write_block(block_num, buf);

    return block_num;
}

uint32_t ext2_alloc_inode(void) {
    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return 0;

    uint8_t* buf = get_block_buf();
    if (!buf) return 0;

    ext2_read_block(bgd.inode_bitmap, buf);

    /* Same one-block ceiling as the block bitmap. Inode numbering needs no
     * first_data_block adjustment: inodes are 1-based, so bit i IS inode i+1. */
    uint32_t inodes_in_group = ext2_fs.inodes_per_group;
    if (inodes_in_group > ext2_fs.block_size * 8) inodes_in_group = ext2_fs.block_size * 8;
    int bit = find_free_bit(buf, inodes_in_group);
    if (bit < 0) return 0;

    uint32_t ino = bit + 1;

    // Mark bit as used
    buf[bit / 8] |= (1 << (bit % 8));
    ext2_write_block(bgd.inode_bitmap, buf);

    // Update BGD
    if (bgd.free_inodes_count > 0) bgd.free_inodes_count--;
    write_bgd(0, &bgd);

    // Update superblock
    if (ext2_fs.sb.free_inodes > 0) ext2_fs.sb.free_inodes--;

    return ino;
}

int ext2_write_file(const char* path, const void* buf, uint32_t len) {
    uint32_t ino = ext2_resolve(path);
    if (!ino) {
        if (ext2_create_file(path) < 0) return -1;
        ino = ext2_resolve(path);
        if (!ino) return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if (!(inode.mode & EXT2_S_IFREG)) return -1;

    // Calculate how many blocks needed
    uint32_t blocks_needed = (len + ext2_fs.block_size - 1) / ext2_fs.block_size;

    // Write data block by block
    const uint8_t* data = (const uint8_t*)buf;
    uint32_t remaining = len;
    uint32_t iblock = 0;

    while (remaining > 0) {
        uint32_t block_num = 0;
        uint32_t chunk = (remaining < ext2_fs.block_size) ? remaining : ext2_fs.block_size;
        /* Was a 4096-byte array on the KERNEL stack, sized by a guess rather than
         * by ext2_fs.block_size — every access below is block_size-wide, so any
         * image with a larger block ran straight off the end of it, in a function
         * that also descends into the ATA layer. block_size is bounded at mount
         * now, but a block-sized buffer belongs on the heap regardless: this is
         * exactly buffer 0's documented role, the caller's staged data block.
         * Nothing called from inside this loop touches buffer 0 (the allocator
         * uses 1 and 4, the indirect walk uses 2), and ext2_write_inode — which
         * does use 0 — runs after the loop has finished with it. */
        uint8_t* block_buf = get_block_buf();
        if (!block_buf) return -1;
        __builtin_memset(block_buf, 0, ext2_fs.block_size);
        __builtin_memcpy(block_buf, data, chunk);

        if (iblock < 12) {
            if (inode.block[iblock] == 0) {
                inode.block[iblock] = ext2_alloc_block();
                if (inode.block[iblock] == 0) return -1;
            }
            block_num = inode.block[iblock];
        } else {
            uint32_t ptrs_per_block = ext2_fs.block_size / 4;
            uint32_t sind_iblock = iblock - 12;
            if (sind_iblock < ptrs_per_block) {
                if (inode.block[12] == 0) {
                    inode.block[12] = ext2_alloc_block();
                    if (inode.block[12] == 0) return -1;
                    /* There used to be a memset(block_buf)+write here to zero the
                     * fresh indirect block. block_buf holds THIS iteration's file
                     * data, already staged above — so zeroing it wiped the 13th
                     * block of every file that needed an indirect block, and the
                     * write at the bottom of the loop then stored those zeros as
                     * the user's data. Deleted rather than rewritten with a spare
                     * buffer: ext2_alloc_block already zeroes what it hands back. */
                }
                uint8_t* ibuf = get_aux_buf();   /* buf still holds dindirect[] */
                ext2_read_block(inode.block[12], ibuf);
                uint32_t* indirect = (uint32_t*)ibuf;
                if (indirect[sind_iblock] == 0) {
                    indirect[sind_iblock] = ext2_alloc_block();
                    if (indirect[sind_iblock] == 0) return -1;
                    ext2_write_block(inode.block[12], ibuf);
                }
                block_num = indirect[sind_iblock];
            } else {
                return -1;
            }
        }

        ext2_write_block(block_num, block_buf);
        data += chunk;
        remaining -= chunk;
        iblock++;
    }

    // Clear old blocks beyond new size (file shrunk)
    // For simplicity, we leave them allocated but the inode's block[] for them is left as-is
    // A proper implementation would free them

    // Update inode size and block count
    inode.size = len;
    /* i_blocks charges the inode for EVERY block it owns, metadata included, so
     * a file with an indirect block owed one more than its data blocks. e2fsck
     * reported "i_blocks is 46, should be 48" on the first file large enough to
     * need one. (The writer never goes past singly-indirect, so that is the only
     * metadata block it can own.) */
    uint32_t meta_blocks = inode.block[12] ? 1 : 0;
    inode.blocks_512 = (blocks_needed + meta_blocks) * (ext2_fs.block_size / 512);
    inode.mtime = inode.ctime = ext2_now();

    // Write inode back
    ext2_write_inode(ino, &inode);
    /* The in-memory superblock's free counters were decremented by every
     * ext2_alloc_block above and then never written anywhere — ext2_sync_
     * superblock existed but had no callers, so the on-disk totals drifted
     * further from the truth with every write and e2fsck reported them wrong
     * from the first file onward. */
    ext2_sync_superblock();

    return len;
}

static int add_dirent_to_parent(uint32_t parent_ino, const char* name,
                                uint32_t new_ino, uint8_t file_type);
static void ext2_free_inode(uint32_t ino);

/* Split "/a/b/c" into parent "/a/b" and child "c".
 *
 * create_file, mkdir and unlink each did this inline, and all three the same
 * wrong way: `memcpy(parent_path, path, last_slash)` and `strcpy(filename, ...)`
 * into 256-byte STACK arrays, with nothing anywhere bounding the path length.
 * A long enough path smashed the kernel stack before the driver ever looked at
 * the disk. Callers do bound their paths today, but a filesystem driver that is
 * only safe because of what its callers happen to do is one refactor away from
 * being unsafe — so it checks its own inputs. */
static int split_parent_child(const char* path, char* parent, uint32_t parent_sz,
                              char* child, uint32_t child_sz) {
    if (!path || path[0] != '/') return -1;

    int last_slash = -1, len = 0;
    while (path[len]) { if (path[len] == '/') last_slash = len; len++; }
    if (last_slash < 0) return -1;

    uint32_t clen = (uint32_t)(len - last_slash - 1);
    if (clen == 0 || clen >= child_sz) return -1;      /* "" or too long to hold */
    __builtin_memcpy(child, path + last_slash + 1, clen);
    child[clen] = '\0';

    if (last_slash == 0) { parent[0] = '/'; parent[1] = '\0'; return 0; }
    if ((uint32_t)last_slash >= parent_sz) return -1;
    __builtin_memcpy(parent, path, last_slash);
    parent[last_slash] = '\0';
    return 0;
}

int ext2_create_file(const char* path) {
    if (!path || path[0] != '/') return -1;
    if (ext2_resolve(path)) return -1;                 // already exists

    char parent_path[MAX_PATH];
    char filename[MAX_FILENAME];
    if (split_parent_child(path, parent_path, sizeof(parent_path),
                                 filename,    sizeof(filename)) < 0) return -1;

    uint32_t parent_ino = ext2_resolve(parent_path);
    if (!parent_ino) return -1;

    uint32_t new_ino = ext2_alloc_inode();
    if (!new_ino) return -1;

    ext2_inode_t new_inode;
    memset_asm(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = EXT2_S_IFREG | 0x1A4; // 0644
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.size = 0;
    new_inode.links_count = 1;
    new_inode.blocks_512 = 0;
    new_inode.atime = new_inode.ctime = new_inode.mtime = ext2_now();

    if (ext2_write_inode(new_ino, &new_inode) < 0) { ext2_free_inode(new_ino); return -1; }

    /* This used to be a verbatim copy of add_dirent_to_parent, carrying its own
     * copy of every bound bug that function had — and it also did
     * `parent_inode.links_count++` for a plain file, which is simply not how ext2
     * counts links (only subdirectories add one, via their ".."). Every file ever
     * created inflated its parent's link count by one, which fsck reports and
     * which nothing in NyxOS ever put back. Both problems go away by calling the
     * one implementation instead of shadowing it. */
    if (add_dirent_to_parent(parent_ino, filename, new_ino, EXT2_FT_REG_FILE) < 0) {
        ext2_free_inode(new_ino);
        return -1;
    }
    ext2_sync_superblock();      // free_inodes changed; push the count to disk
    return 0;
}

int ext2_readdir(const char* path, dirent_t* entries, uint32_t max_entries) {
    uint32_t ino = resolve_path(path);
    if (!ino) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    if (!(inode.mode & EXT2_S_IFDIR)) return -1;

    int count = 0;
    uint32_t bytes_left = inode.size;
    uint32_t iblock = 0;

    while (bytes_left > 0 && count < (int)max_entries) {
        uint8_t* block_buf_local = get_block_buf();
        if (ext2_read_inode_block(&inode, iblock, block_buf_local) < 0) break;
        uint32_t off = 0;
        while (off + EXT2_DIRENT_HDR <= ext2_fs.block_size && off < bytes_left &&
               count < (int)max_entries) {
            ext2_dirent_t* de = (ext2_dirent_t*)(block_buf_local + off);
            // A rec_len below the 8-byte header size makes `off += de->rec_len`
            // below stand still (or go backwards), spinning this loop forever on
            // a corrupt or hostile image. Stop instead.
            if (de->rec_len < EXT2_DIRENT_HDR) break;
            if (off + de->rec_len > ext2_fs.block_size) break;
            if (de->inode == 0) { off += de->rec_len; continue; }

            uint8_t name_len = de->name_len & 0xFF;
            // ext2 allows names up to 255 bytes; dirent_t.name is MAX_FILENAME
            // (128). Copying unclamped wrote up to 128 bytes past the field, and
            // for the last slot past the whole caller array — a kernel stack
            // smash with attacker-chosen bytes, reachable from an unprivileged
            // process because ext2_create does not bound the name either.
            if (name_len >= MAX_FILENAME) name_len = MAX_FILENAME - 1;
            // The name must also lie inside the block we actually read.
            if (off + EXT2_DIRENT_HDR + name_len > ext2_fs.block_size) break;

            if (name_len > 0) {
                __builtin_memcpy(entries[count].name, de->name, name_len);
                entries[count].name[name_len] = '\0';
                entries[count].ino = de->inode;
                entries[count].type = (de->file_type == EXT2_FT_DIR) ? 1 : 0;
                count++;
            }
            off += de->rec_len;
        }
        bytes_left -= ext2_fs.block_size;
        iblock++;
    }

    return count;
}

// ========== Block and inode freeing ==========

static void ext2_free_block(uint32_t block_num) {
    /* Mirror of bit_to_block in the allocator — see the note there. A block below
     * first_data_block is not describable by this bitmap at all (it is the boot
     * block); freeing it would underflow into a wild byte index. */
    if (block_num < ext2_fs.sb.first_data_block) return;
    uint32_t bit = block_to_bit(block_num);
    if (bit >= ext2_fs.block_size * 8) return;      /* outside group 0's bitmap */

    ext2_bgd_t bgd;
    if (read_block_group_bgd(0, &bgd) < 0) return;
    uint8_t* buf = get_bitmap_buf();   /* NOT the shared staging buffer */
    if (!buf) return;
    ext2_read_block(bgd.block_bitmap, buf);
    if (buf[bit / 8] & (1 << (bit % 8))) {
        buf[bit / 8] &= ~(1 << (bit % 8));
        ext2_write_block(bgd.block_bitmap, buf);
        bgd.free_blocks_count++;
        write_bgd(0, &bgd);
        ext2_fs.sb.free_blocks++;
    }
}

static void free_inode_blocks(ext2_inode_t* inode) {
    uint32_t ptrs_per_block = ext2_fs.block_size / 4;
    // Free direct blocks
    for (int i = 0; i < 12; i++) {
        if (inode->block[i]) { ext2_free_block(inode->block[i]); inode->block[i] = 0; }
    }
    // Free singly-indirect block
    if (inode->block[12]) {
        uint8_t* buf = get_block_buf();
        ext2_read_block(inode->block[12], buf);
        uint32_t* indirect = (uint32_t*)buf;
        for (uint32_t i = 0; i < ptrs_per_block; i++)
            if (indirect[i]) ext2_free_block(indirect[i]);
        ext2_free_block(inode->block[12]);
        inode->block[12] = 0;
    }
    // Free doubly-indirect block
    if (inode->block[13]) {
        uint8_t* buf = get_block_buf();
        ext2_read_block(inode->block[13], buf);
        uint32_t* dindirect = (uint32_t*)buf;
        for (uint32_t i = 0; i < ptrs_per_block; i++) {
            if (dindirect[i]) {
                /* This asked for get_block_buf() — the buffer `dindirect` is
                 * still walking. Reading the level-2 block into it overwrote the
                 * level-1 array mid-loop, so from the first iteration onward the
                 * walk was freeing block numbers read out of the wrong table.
                 * Precisely the aliasing this file's five-buffer split exists to
                 * prevent; the split just never reached this line. */
                uint8_t* ibuf = get_aux_buf();
                ext2_read_block(dindirect[i], ibuf);
                uint32_t* indirect = (uint32_t*)ibuf;
                for (uint32_t j = 0; j < ptrs_per_block; j++)
                    if (indirect[j]) ext2_free_block(indirect[j]);
                ext2_free_block(dindirect[i]);
            }
        }
        ext2_free_block(inode->block[13]);
        inode->block[13] = 0;
    }
}

static void ext2_free_inode(uint32_t ino) {
    if (ino < EXT2_ROOT_INO) return;
    uint32_t group = (ino - 1) / ext2_fs.inodes_per_group;
    uint32_t index = (ino - 1) % ext2_fs.inodes_per_group;
    ext2_bgd_t bgd;
    if (read_block_group_bgd(group, &bgd) < 0) return;
    uint8_t* buf = get_block_buf();
    if (!buf) return;
    ext2_read_block(bgd.inode_bitmap, buf);
    if (buf[index / 8] & (1 << (index % 8))) {
        buf[index / 8] &= ~(1 << (index % 8));
        ext2_write_block(bgd.inode_bitmap, buf);
        bgd.free_inodes_count++;
        write_bgd(0, &bgd);
        ext2_fs.sb.free_inodes++;
    }
}

// ========== Directory helpers ==========

/* THE one place a directory entry is created. ext2_create_file used to carry a
 * line-for-line copy of this function, which meant every bug below existed twice
 * and had to be found twice; it now calls this.
 *
 * The bugs were all one shape — a field written without asking how much room it
 * had. `name_len` is a uint8 on disk but was assigned an unbounded strlen, so a
 * 300-byte name recorded itself as 44 while the memcpy still copied 300. And the
 * name field starts 8 bytes into the entry, not 4, so `memset(de->name, 0,
 * rec_len - 4)` cleared four bytes PAST the entry — off the end of the whole
 * block buffer whenever the entry was the last one in its block, which is the
 * common case, because the last entry's rec_len runs to the end of the block.
 * That is a heap overflow on the write path of the only filesystem whose damage
 * survives a reboot. */
static int add_dirent_to_parent(uint32_t parent_ino, const char* name,
                                 uint32_t new_ino, uint8_t file_type)
{
    ext2_inode_t parent_inode;
    if (ext2_read_inode(parent_ino, &parent_inode) < 0) return -1;
    if (!(parent_inode.mode & EXT2_S_IFDIR)) return -1;

    /* Bounded to what ext2_readdir can hand back. ext2 itself permits 255, but
     * dirent_t.name is MAX_FILENAME, so a longer name would be created and then
     * be invisible in every listing — a file you cannot see is worse than a name
     * you cannot use. Refuse it here instead. */
    uint32_t name_len = strlen(name);
    if (name_len == 0 || name_len >= MAX_FILENAME) return -1;
    uint32_t entry_size = EXT2_DIRENT_HDR + name_len;
    entry_size = (entry_size + 3) & ~3;

    uint32_t iblock = 0;
    uint8_t* buf = get_block_buf();
    if (!buf) return -1;
    int found_space = 0;

    while (iblock < 12 && iblock * ext2_fs.block_size < parent_inode.size) {
        if (parent_inode.block[iblock] == 0) break;
        ext2_read_block(parent_inode.block[iblock], buf);
        uint32_t off = 0;
        while (off + EXT2_DIRENT_HDR <= ext2_fs.block_size) {
            ext2_dirent_t* de = (ext2_dirent_t*)(buf + off);
            /* A rec_len under the header size makes `off += rec_len` stand still
             * or walk backwards; one that overruns the block puts every write
             * below outside the buffer. Either way the block is corrupt — stop
             * reading it rather than trusting the arithmetic. */
            if (de->rec_len < EXT2_DIRENT_HDR) break;
            if (off + de->rec_len > ext2_fs.block_size) break;
            uint32_t de_name_len = de->name_len & 0xFF;

            if (de->inode == 0) {
                if (de->rec_len >= entry_size) {
                    de->inode = new_ino;
                    de->name_len = (uint8_t)name_len;
                    de->file_type = file_type;
                    __builtin_memset(de->name, 0, de->rec_len - EXT2_DIRENT_HDR);
                    __builtin_memcpy(de->name, name, name_len);
                    ext2_write_block(parent_inode.block[iblock], buf);
                    found_space = 1;
                    break;
                }
            } else {
                uint32_t min_size = EXT2_DIRENT_HDR + de_name_len;
                min_size = (min_size + 3) & ~3;
                if (de->rec_len >= min_size + entry_size) {
                    uint32_t remaining = de->rec_len - min_size;
                    de->rec_len = min_size;
                    ext2_dirent_t* new_de = (ext2_dirent_t*)(buf + off + min_size);
                    new_de->inode = new_ino;
                    new_de->rec_len = remaining;
                    new_de->name_len = (uint8_t)name_len;
                    new_de->file_type = file_type;
                    __builtin_memset(new_de->name, 0, remaining - EXT2_DIRENT_HDR);
                    __builtin_memcpy(new_de->name, name, name_len);
                    ext2_write_block(parent_inode.block[iblock], buf);
                    found_space = 1;
                    break;
                }
            }
            off += de->rec_len;
        }
        if (found_space) break;
        iblock++;
    }

    if (!found_space) {
        /* Checked BEFORE allocating: the old order allocated and wrote the block,
         * then discovered there was no direct slot to hang it off and returned,
         * leaking that block on the disk with no way to ever reclaim it. */
        if (iblock >= 12) return -1;
        uint32_t new_block = ext2_alloc_block();
        if (!new_block) return -1;
        __builtin_memset(buf, 0, ext2_fs.block_size);
        ext2_dirent_t* de = (ext2_dirent_t*)buf;
        de->inode = new_ino;
        de->rec_len = ext2_fs.block_size;
        de->name_len = (uint8_t)name_len;
        de->file_type = file_type;
        __builtin_memcpy(de->name, name, name_len);
        ext2_write_block(new_block, buf);
        parent_inode.block[iblock] = new_block;
        parent_inode.size += ext2_fs.block_size;
    }

    ext2_write_inode(parent_ino, &parent_inode);
    return 0;
}

int ext2_mkdir(const char* path) {
    if (!path || path[0] != '/') return -1;
    if (ext2_resolve(path)) return -1;

    char parent_path[MAX_PATH];
    char dirname[MAX_FILENAME];
    if (split_parent_child(path, parent_path, sizeof(parent_path),
                                 dirname,     sizeof(dirname)) < 0) return -1;

    uint32_t parent_ino = ext2_resolve(parent_path);
    if (!parent_ino) return -1;

    uint32_t new_ino = ext2_alloc_inode();
    if (!new_ino) return -1;

    ext2_inode_t new_inode;
    __builtin_memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.mode = EXT2_S_IFDIR | 0x1FF; // 0777
    new_inode.uid = 0;
    new_inode.gid = 0;
    new_inode.size = ext2_fs.block_size;
    new_inode.links_count = 2; // . and ..
    new_inode.blocks_512 = ext2_fs.block_size / 512;
    new_inode.atime = new_inode.ctime = new_inode.mtime = ext2_now();

    // Allocate first block for . and .. entries
    uint32_t first_block = ext2_alloc_block();
    if (!first_block) { ext2_free_inode(new_ino); return -1; }
    new_inode.block[0] = first_block;

    uint8_t* buf = get_block_buf();
    if (!buf) { ext2_free_block(first_block); ext2_free_inode(new_ino); return -1; }
    __builtin_memset(buf, 0, ext2_fs.block_size);

    // Entry for "."
    ext2_dirent_t* de = (ext2_dirent_t*)buf;
    de->inode = new_ino;
    de->rec_len = 12;
    de->name_len = 1;
    de->file_type = EXT2_FT_DIR;
    de->name[0] = '.';

    // Entry for ".."
    ext2_dirent_t* de2 = (ext2_dirent_t*)(buf + 12);
    de2->inode = parent_ino;
    de2->rec_len = ext2_fs.block_size - 12;
    de2->name_len = 2;
    de2->file_type = EXT2_FT_DIR;
    de2->name[0] = '.'; de2->name[1] = '.';

    ext2_write_block(first_block, buf);

    if (ext2_write_inode(new_ino, &new_inode) < 0) {
        ext2_free_block(first_block);
        ext2_free_inode(new_ino);
        return -1;
    }

    // Add entry in parent
    if (add_dirent_to_parent(parent_ino, dirname, new_ino, EXT2_FT_DIR) < 0) {
        free_inode_blocks(&new_inode);
        ext2_free_inode(new_ino);
        return -1;
    }

    // Increment parent's link count for ".."
    ext2_inode_t pinode;
    if (ext2_read_inode(parent_ino, &pinode) == 0) {
        pinode.links_count++;
        ext2_write_inode(parent_ino, &pinode);
    }

    bgd_adjust_dir_count(+1);
    ext2_sync_superblock();
    return 0;
}

int ext2_unlink(const char* path) {
    if (!path || path[0] != '/') return -1;
    uint32_t ino = ext2_resolve(path);
    if (!ino || ino < EXT2_ROOT_INO) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) return -1;

    // Check if directory is empty (only . and ..)
    if (inode.mode & EXT2_S_IFDIR) {
        ext2_inode_t dir_inode = inode;
        uint32_t count = 0;
        uint32_t bytes_left = dir_inode.size;
        uint32_t iblock = 0;
        while (bytes_left > 0 && count == 0) {
            uint8_t* dbuf = get_block_buf();
            if (ext2_read_inode_block(&dir_inode, iblock, dbuf) < 0) break;
            uint32_t off = 0;
            while (off + EXT2_DIRENT_HDR <= ext2_fs.block_size && off < bytes_left) {
                ext2_dirent_t* de = (ext2_dirent_t*)(dbuf + off);
                if (de->rec_len < EXT2_DIRENT_HDR) break;          /* would not advance */
                if (off + de->rec_len > ext2_fs.block_size) break;
                uint32_t nl = de->name_len & 0xFF;
                if (off + EXT2_DIRENT_HDR + nl > ext2_fs.block_size) break;
                /* "Empty" used to mean "no entry whose name is longer than two
                 * characters", which quietly counts a real file called `ab` as
                 * absent — rmdir then deleted a directory that still had a child,
                 * orphaning that inode and its blocks on disk. Compare the actual
                 * name against "." and ".." instead of its length. */
                if (de->inode &&
                    !(nl == 1 && de->name[0] == '.') &&
                    !(nl == 2 && de->name[0] == '.' && de->name[1] == '.')) {
                    count++;
                    break;
                }
                off += de->rec_len;
            }
            if (bytes_left < ext2_fs.block_size) break;
            bytes_left -= ext2_fs.block_size;
            iblock++;
        }
        if (count > 0) return -1; // directory not empty
    }

    char parent_path[MAX_PATH];
    char child_name[MAX_FILENAME];
    if (split_parent_child(path, parent_path, sizeof(parent_path),
                                 child_name,  sizeof(child_name)) < 0) return -1;

    uint32_t parent_ino = ext2_resolve(parent_path);
    if (!parent_ino) return -1;

    // Remove directory entry from parent
    ext2_inode_t parent_inode;
    if (ext2_read_inode(parent_ino, &parent_inode) < 0) return -1;

    uint32_t child_name_len = strlen(child_name);
    int removed = 0;
    uint32_t piblock = 0;
    uint32_t pbytes_left = parent_inode.size;

    /* Only direct blocks: the write-back below stores through
     * parent_inode.block[piblock], and past 11 that slot is not a data block at
     * all — block[12] is the singly-indirect POINTER, so a directory big enough
     * to reach it would have had its indirect block overwritten with directory
     * data, and block[15] and beyond is off the end of the inode struct.
     * add_dirent_to_parent never grows a directory past 12 blocks anyway. */
    while (pbytes_left > 0 && !removed && piblock < 12) {
        if (parent_inode.block[piblock] == 0) break;
        uint8_t* pbuf = get_block_buf();
        if (ext2_read_inode_block(&parent_inode, piblock, pbuf) < 0) break;
        uint32_t off = 0;
        while (off + EXT2_DIRENT_HDR <= ext2_fs.block_size && off < pbytes_left) {
            ext2_dirent_t* de = (ext2_dirent_t*)(pbuf + off);
            if (de->rec_len < EXT2_DIRENT_HDR) break;
            if (off + de->rec_len > ext2_fs.block_size) break;
            uint32_t de_name_len = de->name_len & 0xFF;
            if (off + EXT2_DIRENT_HDR + de_name_len > ext2_fs.block_size) break;

            if (de->inode && de_name_len == child_name_len &&
                de_name_len < sizeof(child_name)) {
                char dname[MAX_FILENAME];
                __builtin_memcpy(dname, de->name, de_name_len);
                dname[de_name_len] = '\0';
                if (strcmp(dname, child_name) == 0) {
                    de->inode = 0;
                    ext2_write_block(parent_inode.block[piblock], pbuf);
                    removed = 1;
                    break;
                }
            }
            off += de->rec_len;
        }
        if (pbytes_left < ext2_fs.block_size) break;
        pbytes_left -= ext2_fs.block_size;
        piblock++;
    }

    if (!removed) return -1;

    // If directory, decrement parent link count
    if (inode.mode & EXT2_S_IFDIR) {
        parent_inode.links_count--;
    }

    // Decrement inode link count
    if (inode.links_count > 0) {
        inode.links_count--;
        if (inode.links_count == 0) {
            // Free all blocks and inode
            free_inode_blocks(&inode);
            /* ext2 marks an inode dead by its dtime, not by its link count
             * alone. Leaving it zero is what made e2fsck say "deleted inode has
             * zero dtime" for every file NyxOS had ever removed. */
            inode.dtime = ext2_now();
            inode.size = 0;
            ext2_write_inode(ino, &inode);
            ext2_free_inode(ino);
        } else {
            ext2_write_inode(ino, &inode);
        }
    }

    ext2_write_inode(parent_ino, &parent_inode);
    if (inode.mode & EXT2_S_IFDIR) bgd_adjust_dir_count(-1);
    ext2_sync_superblock();
    return 0;
}
