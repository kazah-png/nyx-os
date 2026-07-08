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
} vfs_node_t;

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
}

int vfs_open(const char* path, int flags, mode_t mode) {
    (void)mode;

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
    // Check if destination is on a mount point
    mount_entry_t* dst_me = vfs_find_mount(dst);
    if (dst_me && dst_me->write_file) {
        // Read source from RAM VFS
        vfs_node_t* src_ino = resolve_path(src);
        if (!src_ino || src_ino->type != 0) return -1;
        return dst_me->write_file(dst + strlen(dst_me->mount_point),
                                 src_ino->data, src_ino->size);
    }

    vfs_node_t* src_ino = resolve_path(src);
    if (!src_ino || src_ino->type != 0) return -1;
    char child_name[MAX_NAME];
    vfs_node_t* dst_parent = resolve_parent(dst, child_name);
    if (!dst_parent || dst_parent->type != 1) return -1;
    if (find_child(dst_parent, child_name)) return -1;
    vfs_node_t* dst_ino = alloc_node();
    if (!dst_ino) return -1;
    strncpy(dst_ino->name, child_name, MAX_NAME-1);
    dst_ino->type = 0;
    dst_ino->parent = dst_parent;
    dst_ino->size = src_ino->size;
    if (src_ino->size > 0 && src_ino->data) {
        dst_ino->data = (uint8_t*)kmalloc(src_ino->size);
        if (dst_ino->data) memcpy(dst_ino->data, src_ino->data, src_ino->size);
    }
    dst_parent->children[dst_parent->child_count++] = dst_ino;
    return 0;
}
