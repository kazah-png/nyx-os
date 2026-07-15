#include "kernel.h"
#include "ext2.h"

#define MAX_INODES    128
#define MAX_NAME      64
#define MAX_CHILDREN  64
#define BLOCK_SIZE    512

// Mount table
static mount_entry_t mount_table[MAX_MOUNT_POINTS];
static int mount_count = 0;

#define VFS_MOUNT_DIRENTS 64   // max entries loaded per mounted directory open

typedef struct vfs_node {
    char name[MAX_NAME];
    uint32_t type;         // 0=file, 1=dir
    uint32_t size;
    uint8_t* data;
    uint32_t node_id;
    struct vfs_node* parent;
    struct vfs_node* children[MAX_CHILDREN];
    uint32_t child_count;
    uint32_t readdir_idx;  // per-directory readdir cursor
    uint8_t  mount_backed; // 1 = transient node mirroring a mounted-FS file
    char     mpath[MAX_NAME]; // path within the mount (e.g. "/foo.txt")
    void*    mount_ent;    // mount_entry_t* to flush writes through
    uint32_t dev_type;    // 0 = regular file; else a /dev special (DEV_* below)
    uint32_t proc_type;   // 0 = not /proc; else a PROC_* generated node (below)
    uint32_t proc_pid;    // for PROC_PID_* nodes: which process this reflects
} vfs_node_t;

/* Special device nodes under /dev. read/write of these bypass ino->data. */
#define DEV_NULL    1     // reads -> EOF, writes discarded
#define DEV_ZERO    2     // reads -> endless zero bytes, writes discarded
#define DEV_RANDOM  3     // reads -> pseudo-random bytes, writes discarded

/* Generated /proc nodes. Their content is synthesized on read (vfs_pread) from
 * live kernel state — nothing is stored in ino->data. The per-pid dirs are
 * created/removed on the fly by proc_sync() to track the process table. */
#define PROC_MEMINFO     1   // MemTotal/MemUsed/MemFree
#define PROC_UPTIME      2   // seconds since boot
#define PROC_VERSION     3   // kernel version banner
#define PROC_CPUINFO     4   // arch / cpu summary
#define PROC_PID_DIR     5   // /proc/<pid> directory (proc_pid set)
#define PROC_PID_STATUS  6   // /proc/<pid>/status  (proc_pid set)
#define PROC_PID_CMDLINE 7   // /proc/<pid>/cmdline (proc_pid set)
#define PROC_PID_MAPS    8   // /proc/<pid>/maps    (mapped memory regions)

/* xorshift64 PRNG for /dev/random, lazily seeded from the tick counter. */
static uint64_t dev_rng_state = 0;
static uint8_t dev_rand_byte(void) {
    extern uint32_t get_ticks(void);
    if (!dev_rng_state) {
        dev_rng_state = ((uint64_t)get_ticks() << 16) ^ 0x9E3779B97F4A7C15ULL;
        if (!dev_rng_state) dev_rng_state = 0x1234567ABCDEFULL;
    }
    dev_rng_state ^= dev_rng_state << 13;
    dev_rng_state ^= dev_rng_state >> 7;
    dev_rng_state ^= dev_rng_state << 17;
    return (uint8_t)(dev_rng_state >> 33);
}

static vfs_node_t nodes[MAX_INODES];
static uint32_t node_count = 0;
static vfs_node_t* current_dir = NULL;

// Free list so transient mount-backed nodes (allocated per open, freed on close)
// don't exhaust the fixed pool. Ramdisk tree nodes are never freed.
static vfs_node_t* free_nodes[MAX_INODES];
static int free_node_count = 0;

static vfs_node_t* alloc_node(void) {
    // preempt_disable: the node pool + free-list are not reentrant. A preemptive
    // context switch mid-update (to another FS caller) would corrupt
    // free_node_count / hand two callers the same node.
    preempt_disable();
    vfs_node_t* node = NULL;
    if (free_node_count > 0) {
        node = free_nodes[--free_node_count];
    } else if (node_count < MAX_INODES) {
        node = &nodes[node_count++];
    }
    if (node) {
        memset_asm(node, 0, sizeof(vfs_node_t));
        node->node_id = (uint32_t)(node - nodes);
    }
    preempt_enable();
    return node;
}

static void free_node(vfs_node_t* n) {
    preempt_disable();
    if (n && free_node_count < MAX_INODES)
        free_nodes[free_node_count++] = n;
    preempt_enable();
}

static vfs_node_t* find_child(vfs_node_t* dir, const char* name) {
    for (uint32_t i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

static char* path_token(char* path, char* token) {
    if (!path || !*path) return NULL;
    while (*path == '/') path++;
    if (!*path) return NULL;
    int i = 0;
    while (*path && *path != '/' && i < MAX_NAME-1) {
        token[i++] = *path++;
    }
    token[i] = '\0';
    return path;
}

static vfs_node_t* resolve_path(const char* path) {
    if (!path || !*path) return current_dir;
    char buf[256];
    strncpy(buf, path, 255);
    buf[255] = '\0';
    char* p = buf;

    vfs_node_t* dir;
    if (buf[0] == '/') {
        dir = &nodes[0];
        p++;
    } else {
        dir = current_dir;
    }

    char token[MAX_NAME];
    vfs_node_t* result = dir;
    // Walk directory components until the last one
    while (1) {
        char* next = path_token(p, token);
        if (!next || !*token) break;
        // Check if there are more components (i.e., this is a directory)
        if (*next) {
            result = find_child(dir, token);
            if (!result || result->type != 1) return NULL;
            dir = result;
            p = next;
        } else {
            // Last component - can be file or directory
            result = find_child(dir, token);
            return result;
        }
    }
    return result;
}

static vfs_node_t* resolve_parent(const char* path, char* child_name) {
    if (!path || !*path) return NULL;
    char buf[256];
    strncpy(buf, path, 255);
    buf[255] = '\0';
    char* p = buf;
    if (buf[0] == '/') p++;

    vfs_node_t* dir = (buf[0] == '/') ? &nodes[0] : current_dir;

    char token[MAX_NAME];
    char prev[MAX_NAME] = "";
    vfs_node_t* result = dir;
    while ((p = path_token(p, token)) != NULL && *token) {
        strncpy(prev, token, MAX_NAME-1);
        vfs_node_t* next = find_child(dir, token);
        if (!next) {
            if (child_name) strncpy(child_name, token, MAX_NAME-1);
            return dir;
        }
        if (next->type != 1) {
            if (child_name) strncpy(child_name, token, MAX_NAME-1);
            return dir;
        }
        result = dir;   // true parent of the token just consumed — so a path that
                        // ENDS in an existing directory returns its parent (not the
                        // walk's start node), and unlink/rename of a nested dir works
        dir = next;
    }
    if (child_name) strncpy(child_name, prev, MAX_NAME-1);
    return result;
}

// ==================== /proc (generated filesystem) ====================
// /proc exposes live kernel state as readable text, synthesized on demand — the
// static files (meminfo/uptime/version/cpuinfo) are created once at boot, while
// the per-process /proc/<pid> dirs (+ status/cmdline) are reconciled with the
// process table by proc_sync() at the VFS entry points ring 3 uses. No storage:
// content is generated in vfs_pread, so nothing here occupies ino->data.

static vfs_node_t* proc_node = NULL;   // the /proc directory node (cached at boot)

// Create a child node of `parent` tagged as a /proc node. Returns it, or NULL if
// the pool or the parent's child array is full (a partial /proc is acceptable).
static vfs_node_t* proc_make(vfs_node_t* parent, const char* name, uint32_t type,
                             uint32_t ptype, uint32_t pid) {
    if (!parent || parent->child_count >= MAX_CHILDREN) return NULL;
    vfs_node_t* n = alloc_node();
    if (!n) return NULL;
    strncpy(n->name, name, MAX_NAME - 1);
    n->type = type;
    n->proc_type = ptype;
    n->proc_pid = pid;
    n->parent = parent;
    parent->children[parent->child_count++] = n;
    return n;
}

// Unsigned base-10 into buf (NUL-terminated); returns the digit count.
static int proc_utoa(uint32_t v, char* buf) {
    char tmp[12]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

// Synthesize a /proc node's full text into buf; returns the byte length.
static int proc_generate(vfs_node_t* ino, char* buf, int bufsz) {
    extern uint64_t memory_total, memory_used;
    extern volatile uint32_t tick_count;
    buf[0] = '\0';
    switch (ino->proc_type) {
        case PROC_MEMINFO: {
            uint32_t tot  = (uint32_t)(memory_total / 1024);
            uint32_t used = (uint32_t)(memory_used  / 1024);
            uint32_t freeb = (uint32_t)((memory_total > memory_used ?
                                         memory_total - memory_used : 0) / 1024);
            snprintf(buf, bufsz,
                     "MemTotal: %u kB\nMemUsed:  %u kB\nMemFree:  %u kB\n",
                     tot, used, freeb);
            break;
        }
        case PROC_UPTIME: {
            uint32_t ms = tick_count;            // 1000 Hz timer -> ms
            snprintf(buf, bufsz, "%u.%02u\n", ms / 1000, (ms % 1000) / 10);
            break;
        }
        case PROC_VERSION:
            snprintf(buf, bufsz, "NyxOS version %s (x86_64)\n", KERNEL_VERSION);
            break;
        case PROC_CPUINFO:
            snprintf(buf, bufsz, "arch\t: x86_64\nvendor\t: NyxOS\n");
            break;
        case PROC_PID_STATUS: {
            process_t* p = find_process(ino->proc_pid);
            if (p) {
                const char* st = p->state == 1 ? "R (running)" :
                                 p->state == 2 ? "Z (zombie)"  :
                                 p->state == 3 ? "S (sleeping)" : "P (parked)";
                snprintf(buf, bufsz, "Name:\t%s\nPid:\t%u\nPPid:\t%u\nState:\t%s\n",
                         p->comm, p->pid, p->ppid, st);
            }
            break;
        }
        case PROC_PID_CMDLINE: {
            process_t* p = find_process(ino->proc_pid);
            if (p) snprintf(buf, bufsz, "%s\n", p->cmdline[0] ? p->cmdline : p->comm);
            break;
        }
        case PROC_PID_MAPS: {
            // Mapped memory regions of the process — the address ranges present in
            // its page tables, coalesced by permission, labelled by VA (program /
            // shared libc / dlopen'd lib / mmap / stack). Shows what dlopen mapped.
            process_t* p = find_process(ino->proc_pid);
            if (!p || !p->page_directory) break;
            static vm_region_t regs[24];      // static: this runs preempt-safe under vfs_pread
            int nr = vm_collect_regions((uint64_t*)p->page_directory, regs, 24);
            int off = 0;
            for (int r = 0; r < nr && off < bufsz - 64; r++) {
                const char* label =
                    regs[r].start >= 0x7FFF00000000ULL          ? "[stack]"  :
                    regs[r].start >= MMAP_BASE                  ? "[mmap]"   :
                    regs[r].start >= 0x31000000ULL              ? "[dynlib]" :
                    regs[r].start >= SHARED_LIBC_BASE - 0x1000  ? "[libc]"   :
                    regs[r].start <  0x100000ULL                ? "[program]": "[heap]";
                off += snprintf(buf + off, bufsz - off, "%012llx-%012llx r%c%c  %s\n",
                                (unsigned long long)regs[r].start, (unsigned long long)regs[r].end,
                                regs[r].writable ? 'w' : '-',
                                regs[r].exec ? 'x' : '-', label);
            }
            break;
        }
    }
    return (int)strlen(buf);
}

// Reconcile /proc/<pid> dirs with the live process table: drop dead ones, add
// new ones (each with status + cmdline children). Idempotent and cheap; called
// at vfs_open / vfs_isdir so an open/getdents/chdir sees the current pids. The
// node-pool churn is guarded — alloc/free plus the children[] edits run with
// preemption off so a kernel-context FS caller can't race it.
static void proc_sync(void) {
    if (!proc_node) return;
    extern process_t* process_table[];
    extern int process_count;
    preempt_disable();
    // 1. Remove pid dirs whose process is gone (free their children first).
    for (uint32_t i = 0; i < proc_node->child_count; ) {
        vfs_node_t* c = proc_node->children[i];
        if (c->proc_type == PROC_PID_DIR && !find_process(c->proc_pid)) {
            for (uint32_t j = 0; j < c->child_count; j++) free_node(c->children[j]);
            free_node(c);
            proc_node->children[i] = proc_node->children[--proc_node->child_count];
        } else i++;
    }
    // 2. Add a dir for each live process that lacks one.
    for (int i = 0; i < process_count; i++) {
        process_t* p = process_table[i];
        if (!p || p->pid == 0) continue;
        int have = 0;
        for (uint32_t k = 0; k < proc_node->child_count; k++) {
            vfs_node_t* c = proc_node->children[k];
            if (c->proc_type == PROC_PID_DIR && c->proc_pid == p->pid) { have = 1; break; }
        }
        if (have) continue;
        char name[12];
        proc_utoa(p->pid, name);
        vfs_node_t* d = proc_make(proc_node, name, 1, PROC_PID_DIR, p->pid);
        if (!d) continue;                        // pool full — skip this pid
        proc_make(d, "status",  0, PROC_PID_STATUS,  p->pid);
        proc_make(d, "cmdline", 0, PROC_PID_CMDLINE, p->pid);
        proc_make(d, "maps",    0, PROC_PID_MAPS,    p->pid);
    }
    preempt_enable();
}

void init_vfs(void) {
    node_count = 0;
    vfs_node_t* root = alloc_node();
    strcpy(root->name, "/");
    root->type = 1;
    root->parent = root;
    current_dir = root;

    // Default directories:
    vfs_mkdir("/home", 0755);
    vfs_mkdir("/home/user", 0755);
    vfs_mkdir("/tmp", 0755);
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/opt", 0755);
    vfs_mkdir("/dev", 0755);
    vfs_mkdir("/bin", 0755);
    vfs_mkdir("/proc", 0755);
    vfs_mkdir("/sys", 0755);

    // Create a welcome file
    int fd = vfs_open("/home/user/welcome.txt", 1, 0644);
    vfs_write(fd, "Welcome to NyxOS v1.0.0\n", 24);
    vfs_write(fd, "Type 'help' for commands.\n", 26);
    vfs_close(fd);

    // Special device files under /dev — regular-looking nodes whose read/write are
    // intercepted by dev_type in vfs_pread/vfs_pwrite. urandom is an alias of random.
    static const struct { const char* path; uint32_t dt; } devs[] = {
        {"/dev/null", DEV_NULL}, {"/dev/zero", DEV_ZERO},
        {"/dev/random", DEV_RANDOM}, {"/dev/urandom", DEV_RANDOM},
    };
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        int dfd = vfs_open(devs[i].path, 1, 0);           // O_CREAT
        if (dfd >= 0) ((vfs_node_t*)(uintptr_t)(uint32_t)dfd)->dev_type = devs[i].dt;
    }

    // /proc static files — generated on read (proc_generate); per-pid dirs are
    // added later by proc_sync(). /proc was created by vfs_mkdir above.
    proc_node = resolve_path("/proc");
    static const struct { const char* name; uint32_t pt; } procf[] = {
        {"meminfo", PROC_MEMINFO}, {"uptime", PROC_UPTIME},
        {"version", PROC_VERSION}, {"cpuinfo", PROC_CPUINFO},
    };
    for (unsigned i = 0; i < sizeof(procf) / sizeof(procf[0]); i++)
        proc_make(proc_node, procf[i].name, 0, procf[i].pt, 0);
}

int vfs_open(const char* path, int flags, mode_t mode) {
    (void)mode;

    // Refresh /proc's per-pid dirs so an open/getdents under /proc sees the
    // current process table (cheap no-op when nothing changed).
    proc_sync();

    // Mount paths (e.g. /mnt/...) are backed by a real filesystem, not the
    // ramdisk tree. Create a transient node that mirrors the FS file so that
    // fd-based read/write/close work and writes flush back to disk.
    mount_entry_t* me = vfs_find_mount(path);
    if (me) {
        const char* sub = path + strlen(me->mount_point);
        if (!*sub) sub = "/";

        // Directory? readdir returns -1 for non-dirs, so probe by loading its
        // entries once into the node; vfs_readdir then serves them by index.
        dirent_t* dents = (dirent_t*)kmalloc(sizeof(dirent_t) * VFS_MOUNT_DIRENTS);
        int nd = (me->readdir && dents) ? me->readdir(sub, dents, VFS_MOUNT_DIRENTS) : -1;
        if (nd >= 0) {
            vfs_node_t* dn = alloc_node();
            if (!dn) { if (dents) kfree(dents); return -1; }
            dn->type = 1; dn->mount_backed = 1; dn->mount_ent = me;
            strncpy(dn->mpath, sub, MAX_NAME - 1);
            dn->data = (uint8_t*)dents; dn->size = (uint32_t)nd;
            return (int)(uintptr_t)dn;
        }
        if (dents) kfree(dents);

        // Otherwise a regular file.
        uint32_t exists = me->resolve ? me->resolve(sub) : 0;
        if (!exists && !(flags & 1)) return -1;        // read of a missing file
        vfs_node_t* n = alloc_node();
        if (!n) return -1;
        n->type = 0;
        n->mount_backed = 1;
        n->mount_ent = me;
        strncpy(n->mpath, sub, MAX_NAME - 1);
        if (!exists && (flags & 1)) {
            if (me->write_file) me->write_file(sub, "", 0);   // create empty
        } else if (exists && me->read_file) {
            uint32_t sz = me->get_size ? me->get_size(sub) : 0;
            if (sz > 0) {
                n->data = (uint8_t*)kmalloc(sz);
                if (n->data) {
                    int r = me->read_file(sub, n->data, sz);
                    n->size = (r > 0) ? (uint32_t)r : 0;
                }
            }
        }
        return (int)(uintptr_t)n;
    }

    vfs_node_t* ino = resolve_path(path);

    if (flags & 1) { // O_CREAT — create a file if it does not exist
        if (!ino) {
            char child_name[MAX_NAME];
            vfs_node_t* parent = resolve_parent(path, child_name);
            if (!parent || parent->type != 1) return -1;
            ino = alloc_node();
            if (!ino) return -1;
            strncpy(ino->name, child_name, MAX_NAME-1);
            ino->type = 0;
            ino->parent = parent;
            parent->children[parent->child_count++] = ino;
        }
        if (flags & 2) {                 // O_TRUNC — reset an existing file to empty
            if (ino->data) { kfree(ino->data); ino->data = 0; }
            ino->size = 0;
        }
        return (int)(uintptr_t)ino;
    }

    if (!ino) return -1;
    ino->readdir_idx = 0;  // reset readdir cursor on open
    return (int)(uintptr_t)ino;
}

int vfs_read(int fd, void* buf, size_t count) {
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!ino || ino->type != 0) return -1;
    if (count > ino->size) count = ino->size;
    if (ino->data) memcpy(buf, ino->data, count);
    return count;
}

// Flush a mount-backed node's full contents back to its filesystem.
static void flush_mount_node(vfs_node_t* ino) {
    if (!ino->mount_backed || !ino->mount_ent) return;
    mount_entry_t* me = (mount_entry_t*)ino->mount_ent;
    if (me->write_file) me->write_file(ino->mpath, ino->data ? ino->data : (uint8_t*)"", ino->size);
}

int vfs_write(int fd, const void* buf, size_t count) {
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!ino || ino->type != 0) return -1;
    if (!ino->data || ino->size < count) {
        uint8_t* new_data = (uint8_t*)kmalloc(count + BLOCK_SIZE);
        if (!new_data) return -1;
        if (ino->data) {
            memcpy(new_data, ino->data, ino->size);
            kfree(ino->data);
        }
        ino->data = new_data;
    }
    memcpy(ino->data, buf, count);
    ino->size = count;
    flush_mount_node(ino);            // persist to disk if this is a mount file
    return count;
}

// Offset-aware read: copy up to `count` bytes starting at `offset`. Returns the
// number of bytes read (0 at/after EOF), or -1 on a bad handle.
int vfs_pread(int fd, void* buf, uint32_t count, uint32_t offset) {
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!ino || ino->type != 0) return -1;
    if (ino->dev_type) {                       // /dev special: content is generated
        switch (ino->dev_type) {
            case DEV_NULL:   return 0;         // always EOF
            case DEV_ZERO:   memset_asm(buf, 0, count); return (int)count;
            case DEV_RANDOM: for (uint32_t i = 0; i < count; i++)
                                 ((uint8_t*)buf)[i] = dev_rand_byte();
                             return (int)count;
        }
    }
    if (ino->proc_type) {                      // /proc: content synthesized on read
        char gbuf[1024];               // maps can list many regions
        int len = proc_generate(ino, gbuf, sizeof(gbuf));
        if (offset >= (uint32_t)len) return 0;
        uint32_t avail = (uint32_t)len - offset;
        if (count > avail) count = avail;
        if (count) memcpy(buf, gbuf + offset, count);
        return (int)count;
    }
    if (offset >= ino->size) return 0;
    uint32_t avail = ino->size - offset;
    if (count > avail) count = avail;
    if (ino->data && count) memcpy(buf, ino->data + offset, count);
    return (int)count;
}

// Offset-aware write: write `count` bytes at `offset`, growing the file (and
// zero-filling any gap) as needed without discarding existing content.
int vfs_pwrite(int fd, const void* buf, uint32_t count, uint32_t offset) {
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!ino || ino->type != 0) return -1;
    if (ino->dev_type) return (int)count;      // /dev/null etc: accept + discard
    uint32_t end = offset + count;
    if (end < offset) return -1;                 // overflow
    if (!ino->data || end > ino->size) {
        uint8_t* nd = (uint8_t*)kmalloc(end + BLOCK_SIZE);
        if (!nd) return -1;
        memset_asm(nd, 0, end + BLOCK_SIZE);     // zero-fill gaps beyond old data
        if (ino->data) { memcpy(nd, ino->data, ino->size); kfree(ino->data); }
        ino->data = nd;
    }
    if (count) memcpy(ino->data + offset, buf, count);
    if (end > ino->size) ino->size = end;
    flush_mount_node(ino);            // persist to disk if this is a mount file
    return (int)count;
}

int vfs_close(int fd) {
    if (fd <= 0) return 0;
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (ino->mount_backed) {          // transient mirror — release it
        if (ino->data) kfree(ino->data);
        free_node(ino);
    }
    return 0;
}

// 1 if `path` names a directory in the ramdisk tree (used by chdir to reject a
// file). Mount points are directories too.
int vfs_isdir(const char* path) {
    if (vfs_find_mount(path)) return 1;
    proc_sync();                       // so `cd /proc/<pid>` resolves a live pid
    vfs_node_t* n = resolve_path(path);
    return (n && n->type == 1) ? 1 : 0;
}

int vfs_create_from_mem(const char* path, uint8_t* data, uint32_t size) {
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent((char*)path, child_name);
    if (!parent || parent->type != 1) return -1;
    vfs_node_t* ino = alloc_node();
    if (!ino) return -1;
    strncpy(ino->name, child_name, MAX_NAME-1);
    ino->type = 0;
    ino->size = size;
    ino->data = data;
    ino->parent = parent;
    parent->children[parent->child_count++] = ino;
    return (int)(uintptr_t)ino;
}

uint32_t vfs_fsize(int fd) {
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!ino || ino->type != 0) return 0;
    return ino->size;
}

uint8_t* vfs_fdata(int fd) {
    vfs_node_t* ino = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!ino || ino->type != 0) return NULL;
    return ino->data;
}

int vfs_mkdir(const char* path, mode_t mode) {
    (void)mode;
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->mkdir) {
        const char* subpath = path + strlen(me->mount_point);
        return me->mkdir(subpath);
    }
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(path, child_name);
    if (!parent || parent->type != 1) return -1;
    if (find_child(parent, child_name)) return -1;

    vfs_node_t* dir = alloc_node();
    if (!dir) return -1;
    strncpy(dir->name, child_name, MAX_NAME-1);
    dir->type = 1;
    dir->parent = parent;
    parent->children[parent->child_count++] = dir;
    return 0;
}

int vfs_unlink(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->unlink) {
        const char* subpath = path + strlen(me->mount_point);
        return me->unlink(subpath);
    }
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(path, child_name);
    if (!parent || parent->type != 1) return -1;
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, child_name) == 0) {
            if (parent->children[i]->data) kfree(parent->children[i]->data);
            memset_asm(parent->children[i], 0, sizeof(vfs_node_t));
            for (uint32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j+1];
            parent->child_count--;
            return 0;
        }
    }
    return -1;
}

void hide_file(const char* path) {
    (void)path;
}

void vfs_rename(const char* old, const char* new) {
    // If either endpoint is on a mounted FS, the ramdisk tree walk below can't
    // find or place it. Fall back to copy-then-delete, which works for files
    // across any mix of ramdisk and mounted FS (vfs_cp/vfs_unlink are both
    // mount-aware). Directories on a mount aren't moved — that would need a
    // recursive copy — and vfs_cp fails cleanly on them, so `old` is preserved.
    if (vfs_find_mount(old) || vfs_find_mount(new)) {
        if (vfs_cp(old, new) == 0)
            vfs_unlink(old);
        return;
    }
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(old, child_name);
    if (!parent) return;
    vfs_node_t* ino = find_child(parent, child_name);
    if (!ino) return;
    char new_name[MAX_NAME];
    vfs_node_t* new_parent = resolve_parent(new, new_name);
    if (!new_parent) { strncpy(ino->name, new, MAX_NAME-1); return; }
    strncpy(ino->name, new_name, MAX_NAME-1);
    if (new_parent != parent) {
        for (uint32_t i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == ino) {
                for (uint32_t j = i; j < parent->child_count - 1; j++)
                    parent->children[j] = parent->children[j+1];
                parent->child_count--;
                break;
            }
        }
        new_parent->children[new_parent->child_count++] = ino;
        ino->parent = new_parent;
    }
}

dirent_t* vfs_readdir(int fd) {
    vfs_node_t* dir = (vfs_node_t*)(uintptr_t)(uint32_t)fd;
    if (!dir || dir->type != 1) return NULL;
    if (dir->mount_backed) {               // mounted-FS dir: entries loaded at open
        dirent_t* ents = (dirent_t*)dir->data;
        if (!ents || dir->readdir_idx >= dir->size) return NULL;
        return &ents[dir->readdir_idx++];
    }
    if (dir->readdir_idx >= dir->child_count) return NULL;
    vfs_node_t* child = dir->children[dir->readdir_idx];
    dir->readdir_idx++;
    static dirent_t entry;
    strncpy(entry.name, child->name, MAX_FILENAME - 1);
    entry.name[MAX_FILENAME - 1] = '\0';
    entry.type = child->type;
    entry.ino = child->node_id;
    return &entry;
}

// ==================== Mount table ====================

// --- FS re-entrancy guard ---------------------------------------------------
// EXT2 shares ONE global scratch buffer (`block_buf` in ext2.c) across an entire
// operation, and the VFS node pool above isn't reentrant either. User-process FS
// syscalls already run with interrupts masked (atomic), but a kernel-context FS
// caller — a shell command / file-manager action on the compositor thread — runs
// with interrupts on and can be preempted mid-operation; if a scheduled process
// then makes an FS syscall, the two trample the same buffer. We close the gap by
// running each EXT2 operation with preemption disabled (the scheduler keeps the
// current thread — a coarse but correct single-core "FS lock"). These thin
// wrappers sit on the mount function-pointers so ext2.c needs no changes.
static uint32_t fs_resolve(const char* p) {
    preempt_disable(); uint32_t r = ext2_resolve(p);   preempt_enable(); return r;
}
static uint32_t fs_get_size(const char* p) {
    preempt_disable(); uint32_t r = ext2_get_size(p);  preempt_enable(); return r;
}
static int fs_read_file(const char* p, void* b, uint32_t n) {
    preempt_disable(); int r = ext2_read_file(p, b, n); preempt_enable(); return r;
}
static int fs_write_file(const char* p, const void* b, uint32_t n) {
    preempt_disable(); int r = ext2_write_file(p, b, n); preempt_enable(); return r;
}
static int fs_readdir(const char* p, dirent_t* e, uint32_t m) {
    preempt_disable(); int r = ext2_readdir(p, e, m);  preempt_enable(); return r;
}
static int fs_mkdir(const char* p) {
    preempt_disable(); int r = ext2_mkdir(p);          preempt_enable(); return r;
}
static int fs_unlink(const char* p) {
    preempt_disable(); int r = ext2_unlink(p);         preempt_enable(); return r;
}

// Make a mount point visible in the ramdisk tree, so directory listings that
// walk children (the GUI file manager, `ls` of the parent) show it. vfs_open on
// this path still routes to the mounted FS — vfs_find_mount takes precedence —
// so the stub only supplies the parent's entry; it is never opened itself.
static void ensure_mount_stub(const char* mount_point) {
    if (resolve_path(mount_point)) return;      // already present
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(mount_point, child_name);
    if (!parent || parent->type != 1) return;
    if (parent->child_count >= MAX_CHILDREN) return;   // full: mount still works, just not listed
    if (find_child(parent, child_name)) return;
    vfs_node_t* dir = alloc_node();
    if (!dir) return;
    strncpy(dir->name, child_name, MAX_NAME - 1);
    dir->type = 1;
    dir->parent = parent;
    parent->children[parent->child_count++] = dir;
}

int vfs_mount(const char* mount_point, int fs_type, void* fs_data) {
    (void)fs_data;
    if (mount_count >= MAX_MOUNT_POINTS) return -1;
    mount_entry_t* me = &mount_table[mount_count];
    strncpy(me->mount_point, mount_point, MAX_PATH - 1);
    me->type = fs_type;
    me->resolve = NULL;
    me->get_size = NULL;
    me->read_file = NULL;
    me->readdir = NULL;

    if (fs_type == FS_TYPE_EXT2) {
        // Point at the preempt-guarded wrappers, not ext2_* directly (see above).
        me->resolve   = fs_resolve;
        me->get_size  = fs_get_size;
        me->read_file = fs_read_file;
        me->write_file = fs_write_file;
        me->readdir    = fs_readdir;
        me->mkdir      = fs_mkdir;
        me->unlink     = fs_unlink;
    }

    mount_count++;
    ensure_mount_stub(mount_point);   // so the mount shows up when browsing "/"
    return 0;
}

mount_entry_t* vfs_find_mount(const char* path) {
    if (!path) return NULL;
    for (int i = 0; i < mount_count; i++) {
        int len = strlen(mount_table[i].mount_point);
        if (strncmp(path, mount_table[i].mount_point, len) == 0) {
            if (path[len] == '\0' || path[len] == '/')
                return &mount_table[i];
        }
    }
    return NULL;
}

// ==================== New helper functions ====================

const char* vfs_getcwd(void) {
    // Build the full absolute path by walking parent pointers up to the root.
    static char path[256];
    vfs_node_t* d = current_dir;
    if (!d || d == &nodes[0]) return "/";
    const char* parts[32];
    int n = 0;
    while (d && d != &nodes[0] && n < 32) { parts[n++] = d->name; d = d->parent; }
    int pos = 0;
    for (int i = n - 1; i >= 0 && pos < 250; i--) {
        path[pos++] = '/';
        for (int j = 0; parts[i][j] && pos < 255; j++) path[pos++] = parts[i][j];
    }
    path[pos] = '\0';
    return path;
}

// Per-shell CWD: the current directory is no longer a single global — each
// Terminal window owns its own directory node and swaps it in around command
// execution (via these accessors), so `cd` in one shell doesn't move another.
void* vfs_getcwd_node(void)  { return current_dir; }
void  vfs_setcwd_node(void* n) { if (n) current_dir = (vfs_node_t*)n; }
void* vfs_root_node(void)    { return &nodes[0]; }

int vfs_chdir(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me) {
        // Find or create a stub directory for the mount point
        vfs_node_t* dir = resolve_path(path);
        if (!dir) {
            // Auto-create the mount point directory in ramdisk VFS
            char child_name[MAX_NAME];
            vfs_node_t* parent = resolve_parent(path, child_name);
            if (!parent || parent->type != 1) return -1;
            dir = alloc_node();
            if (!dir) return -1;
            strncpy(dir->name, child_name, MAX_NAME - 1);
            dir->type = 1;
            dir->parent = parent;
            parent->children[parent->child_count++] = dir;
        }
        current_dir = dir;
        return 0;
    }

    vfs_node_t* dir = resolve_path(path);
    if (!dir || dir->type != 1) return -1;
    current_dir = dir;
    return 0;
}

void vfs_list_dir(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->readdir) {
        int mlen = strlen(me->mount_point);
        const char* subpath = path + mlen;
        if (subpath[0] == '\0') subpath = "/";
        dirent_t entries[64];
        int n = me->readdir(subpath, entries, 64);
        if (n < 0) {
            printf("ls: %s: error reading directory\n", path ? path : "");
            return;
        }
        for (int i = 0; i < n; i++) {
            if (entries[i].type == 1) {
                set_terminal_color(vga_entry_color(VGA_LIGHT_BLUE, VGA_BLACK));
                printf("%s/\n", entries[i].name);
            } else {
                set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
                printf("%s\n", entries[i].name);
            }
        }
        set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
        return;
    }

    vfs_node_t* dir = path ? resolve_path(path) : current_dir;
    if (!dir || dir->type != 1) {
        printf("ls: %s: No such directory\n", path ? path : "");
        return;
    }
    for (uint32_t i = 0; i < dir->child_count; i++) {
        vfs_node_t* child = dir->children[i];
        if (child->type == 1) {
            set_terminal_color(vga_entry_color(VGA_LIGHT_BLUE, VGA_BLACK));
            printf("%s/\n", child->name);
        } else {
            set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
            printf("%s  (%d bytes)\n", child->name, child->size);
        }
    }
    set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
}

void vfs_cat_file(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->read_file) {
        int mlen = strlen(me->mount_point);
        const char* subpath = path + mlen;
        if (subpath[0] == '\0') subpath = "/";
        char buf[512];
        int n = me->read_file(subpath, buf, 511);
        if (n < 0) {
            printf("cat: %s: error reading file\n", path);
            return;
        }
        buf[n] = '\0';
        printf("%s", buf);
        return;
    }

    vfs_node_t* ino = resolve_path(path);
    if (!ino || ino->type != 0) {
        printf("cat: %s: No such file\n", path);
        return;
    }
    for (uint32_t i = 0; i < ino->size; i++) {
        putchar(ino->data[i]);
    }
}

int vfs_touch(const char* path) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->write_file) {
        int mlen = strlen(me->mount_point);
        const char* subpath = path + mlen;
        if (subpath[0] == '\0') subpath = "/";
        // Create empty file via write with len=0
        return me->write_file(subpath, "", 0) >= 0 ? 0 : -1;
    }
    vfs_node_t* ino = resolve_path(path);
    if (ino) return 0; // exists
    char child_name[MAX_NAME];
    vfs_node_t* parent = resolve_parent(path, child_name);
    if (!parent || parent->type != 1) return -1;
    ino = alloc_node();
    if (!ino) return -1;
    strncpy(ino->name, child_name, MAX_NAME-1);
    ino->type = 0;
    ino->parent = parent;
    parent->children[parent->child_count++] = ino;
    return 0;
}

int vfs_write_file(const char* path, const void* buf, uint32_t len) {
    mount_entry_t* me = vfs_find_mount(path);
    if (me && me->write_file) {
        int mlen = strlen(me->mount_point);
        const char* subpath = path + mlen;
        if (subpath[0] == '\0') subpath = "/";
        return me->write_file(subpath, buf, len);
    }
    // Fallback to RAM VFS — auto-create file if it doesn't exist
    vfs_node_t* ino = resolve_path(path);
    if (!ino) {
        char child_name[MAX_NAME];
        vfs_node_t* parent = resolve_parent(path, child_name);
        if (!parent || parent->type != 1) return -1;
        ino = alloc_node();
        if (!ino) return -1;
        strncpy(ino->name, child_name, MAX_NAME - 1);
        ino->type = 0;
        ino->parent = parent;
        parent->children[parent->child_count++] = ino;
    }
    if (ino->type != 0) return -1;
    if (ino->data) kfree(ino->data);
    ino->data = (uint8_t*)kmalloc(len);
    if (!ino->data && len > 0) return -1;
    if (len > 0) memcpy(ino->data, buf, len);
    ino->size = len;
    return len;
}

int vfs_cp(const char* src, const char* dst) {
    // --- Load the source bytes into a buffer, whether it lives in the ramdisk
    // tree or on a mounted FS (e.g. /mnt). Only regular files are copied. ---
    uint8_t* sbuf = NULL;
    uint32_t ssize = 0;
    mount_entry_t* src_me = vfs_find_mount(src);
    if (src_me && src_me->read_file) {
        const char* sub = src + strlen(src_me->mount_point);
        if (!*sub) sub = "/";
        // Refuse to "copy" a directory (that would need recursion); a mounted-FS
        // readdir succeeding (>=0) means the path is a directory.
        if (src_me->readdir) {
            dirent_t probe;
            if (src_me->readdir(sub, &probe, 1) >= 0) return -1;
        }
        if (src_me->resolve && !src_me->resolve(sub)) return -1;   // missing
        ssize = src_me->get_size ? src_me->get_size(sub) : 0;
        sbuf = (uint8_t*)kmalloc(ssize ? ssize : 1);
        if (!sbuf) return -1;
        if (ssize) {
            int r = src_me->read_file(sub, sbuf, ssize);
            if (r < 0) { kfree(sbuf); return -1; }
            ssize = (r > 0) ? (uint32_t)r : 0;
        }
    } else {
        vfs_node_t* src_ino = resolve_path(src);
        if (!src_ino || src_ino->type != 0) return -1;
        ssize = src_ino->size;
        sbuf = (uint8_t*)kmalloc(ssize ? ssize : 1);
        if (!sbuf) return -1;
        if (ssize && src_ino->data) memcpy(sbuf, src_ino->data, ssize);
    }

    // --- Write to the destination, again ramdisk- or mount-backed. ---
    int rc = -1;
    mount_entry_t* dst_me = vfs_find_mount(dst);
    if (dst_me && dst_me->write_file) {
        const char* sub = dst + strlen(dst_me->mount_point);
        if (!*sub) sub = "/";
        rc = dst_me->write_file(sub, sbuf, ssize) >= 0 ? 0 : -1;
    } else {
        char child_name[MAX_NAME];
        vfs_node_t* dst_parent = resolve_parent(dst, child_name);
        if (dst_parent && dst_parent->type == 1 && !find_child(dst_parent, child_name)) {
            vfs_node_t* dst_ino = alloc_node();
            if (dst_ino) {
                strncpy(dst_ino->name, child_name, MAX_NAME-1);
                dst_ino->type = 0;
                dst_ino->parent = dst_parent;
                dst_ino->size = ssize;
                if (ssize) {
                    dst_ino->data = (uint8_t*)kmalloc(ssize);
                    if (dst_ino->data) memcpy(dst_ino->data, sbuf, ssize);
                    else dst_ino->size = 0;
                }
                dst_parent->children[dst_parent->child_count++] = dst_ino;
                rc = 0;
            }
        }
    }
    kfree(sbuf);
    return rc;
}
