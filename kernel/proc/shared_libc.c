#include "../core/kernel.h"
#include "elf.h"

/* Shared / dynamically-loaded ELF libraries (v5.8.28 shared libc, v5.8.29 dlopen).
 *
 * A library is a prelinked ELF at a fixed VA (libc.so @ SHARED_LIBC_BASE, other
 * .so's above it). Its read-only .text/.rodata are loaded ONCE into master
 * physical frames and mapped SHARED (same frames, RO) into each process that uses
 * it; its writable page (.bss/.data) is a private per-process copy. There is no
 * PLT/GOT and no runtime relocation — everything is at its link-time address.
 *
 *  - libc.so is mapped into EVERY process at load (shared_libc_map, from
 *    elf_load_image); programs link it via `ld --just-symbols`.
 *  - dlopen() maps an additional .so into the CALLING process on demand and
 *    returns a handle; dlsym() resolves a name to its VA via the .so's symtab. */

#define MAX_SEG_PAGES  32
#define MAX_SEGS       8
#define MAX_DYNLIBS    4

typedef struct {
    uint64_t vaddr;                  /* page-aligned base VA                     */
    uint32_t npages;
    int      writable;               /* 1 = private per-process copy (.bss/.data)*/
    int      exec;
    void*    frames[MAX_SEG_PAGES];  /* master frames, content loaded at boot    */
} libseg_t;

/* Parse an ELF's PT_LOADs into `segs`, loading each into fresh master frames.
 * Returns 0 on success. */
static int libseg_load(const uint8_t* data, uint32_t size, libseg_t* segs, int* out_nsegs) {
    if (!elf_validate(data, size)) return -1;
    elf64_hdr_t* hdr = (elf64_hdr_t*)data;

    /* This runs BEFORE do_dlopen and do_dlsym — it is the first thing to touch
     * the image, and dlopen() will happily be pointed at a .so the calling
     * process wrote itself. Nothing here was bounded, and the damage is not
     * subtle: the memcpy below reads from `data + offset` with p_offset never
     * compared against `size`, so a crafted header copies arbitrary kernel
     * memory into a frame that is then MAPPED INTO THE PROCESS — an arbitrary
     * kernel-memory disclosure, readable straight from ring 3.
     *
     * All arithmetic is written subtraction-first so it cannot wrap. */
    uint64_t phend;
    if (hdr->e_phentsize != sizeof(elf64_phdr_t)) return -1;
    if (hdr->e_phoff > size) return -1;
    phend = (uint64_t)hdr->e_phnum * sizeof(elf64_phdr_t);
    if (phend > size - hdr->e_phoff) return -1;

    elf64_phdr_t* ph = (elf64_phdr_t*)(data + hdr->e_phoff);
    int n = 0;
    for (uint32_t i = 0; i < hdr->e_phnum && n < MAX_SEGS; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint64_t vaddr = ph[i].p_vaddr, memsz = ph[i].p_memsz;
        uint64_t filesz = ph[i].p_filesz, offset = ph[i].p_offset;

        /* The bytes we copy must lie inside the image... */
        if (filesz > memsz) return -1;
        if (offset > size || filesz > size - offset) return -1;
        /* ...and the segment must land in USER space: libseg_map installs these
         * frames into the process PML4, whose higher half is the kernel's own
         * tables, mirrored by value. */
        if (memsz > USER_SPACE_END ||
            vaddr < USER_SPACE_MIN || vaddr > USER_SPACE_END - memsz) return -1;

        uint64_t start = vaddr & ~0xFFFULL;
        uint64_t end   = (vaddr + memsz + 0xFFF) & ~0xFFFULL;
        uint32_t np    = (uint32_t)((end - start) / 4096);
        if (np > MAX_SEG_PAGES) np = MAX_SEG_PAGES;

        libseg_t* s = &segs[n];
        s->vaddr = start; s->npages = np;
        s->writable = (ph[i].p_flags & PF_W) ? 1 : 0;
        s->exec     = (ph[i].p_flags & PF_X) ? 1 : 0;
        for (uint32_t p = 0; p < np; p++) {
            void* frame = alloc_page();
            if (!frame) return -1;
            page_pin(frame);          // master frame: must never return to the allocator
            memset_asm(frame, 0, 4096);
            uint64_t page = start + (uint64_t)p * 4096;
            uint64_t cs = (page > vaddr) ? page : vaddr;
            uint64_t ce = (page + 4096 < vaddr + filesz) ? page + 4096 : vaddr + filesz;
            if (cs < ce)
                memcpy_asm((uint8_t*)frame + (cs - page), data + offset + (cs - vaddr), ce - cs);
            s->frames[p] = frame;
        }
        n++;
    }
    *out_nsegs = n;
    return 0;
}

/* Map loaded segments into a process: RO shared (incref'd so teardown balances),
 * writable segments a private per-process copy. */
static void libseg_map(uint64_t* pml4, libseg_t* segs, int nsegs) {
    for (int i = 0; i < nsegs; i++) {
        libseg_t* s = &segs[i];
        for (uint32_t p = 0; p < s->npages; p++) {
            uint64_t va = s->vaddr + (uint64_t)p * 4096;
            if (s->writable) {
                void* fresh = alloc_page();
                if (!fresh) return;
                memcpy_asm(fresh, s->frames[p], 4096);
                map_page_dir(pml4, fresh, (void*)va, 0x7 | PAGE_NX);
            } else {
                map_page_ro(pml4, s->frames[p], (void*)va, s->exec);
                page_incref(s->frames[p]);
            }
        }
    }
}

/* ---- the shared libc (mapped into every process) ------------------------- */

static libseg_t libc_segs[MAX_SEGS];
static int      libc_nsegs = 0;
static int      libc_ready = 0;

int shared_libc_is_ready(void) { return libc_ready; }

void shared_libc_load(void) {
    int fd = vfs_open("/libc.so", 0, 0);
    if (fd < 0) { serial_puts("[LIBC] /libc.so not found\n"); return; }
    uint32_t size = vfs_fsize(fd);
    uint8_t* data = vfs_fdata(fd);
    if (!data || size == 0 || libseg_load(data, size, libc_segs, &libc_nsegs) != 0) {
        vfs_close(fd); serial_puts("[LIBC] /libc.so invalid\n"); return;
    }
    vfs_close(fd);
    libc_ready = 1;
    serial_puts("[LIBC] shared libc mapped at 0x30000000\n");
}

void shared_libc_map(uint64_t* pml4) {
    if (libc_ready && pml4) libseg_map(pml4, libc_segs, libc_nsegs);
}

/* ---- dlopen / dlsym (on-demand libraries) -------------------------------- */

typedef struct {
    int       used;
    char      path[64];
    libseg_t  segs[MAX_SEGS];
    int       nsegs;
    uint8_t*  elf;         /* file image, kept for dlsym symtab lookups */
    uint32_t  elf_size;
} dynlib_t;

static dynlib_t dynlibs[MAX_DYNLIBS];

/* dlopen(path): load `path` (if not already) and map it into the CURRENT process.
 * Returns a handle (1-based index), or -1. */
long do_dlopen(const char* path) {
    process_t* self = get_current_process();
    if (!self || !self->page_directory) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_DYNLIBS; i++) {           /* already loaded? */
        if (dynlibs[i].used && strcmp(dynlibs[i].path, path) == 0) { slot = i; break; }
    }
    if (slot < 0) {                                    /* load it into master frames */
        for (int i = 0; i < MAX_DYNLIBS; i++) if (!dynlibs[i].used) { slot = i; break; }
        if (slot < 0) return -1;                       /* registry full */
        int fd = vfs_open(path, 0, 0);
        if (fd < 0) return -1;
        uint32_t size = vfs_fsize(fd);
        uint8_t* data = vfs_fdata(fd);
        if (!data || size == 0 || libseg_load(data, size, dynlibs[slot].segs, &dynlibs[slot].nsegs) != 0) {
            vfs_close(fd); return -1;
        }
        /* Take our OWN copy of the image. This used to keep `data` — the VFS
         * node's buffer — on the assumption that ramdisk data persists. That
         * holds for initramfs files and NOT for anything else: dlopen("/tmp/x.so")
         * followed by unlink(), a write(), or an open(O_TRUNC) frees or reallocs
         * that buffer, and do_dlsym below then walks freed memory on behalf of
         * whoever calls it. The registry outlives the file, so it must own the
         * bytes. Bounded by MAX_DYNLIBS. */
        uint8_t* own = (uint8_t*)kmalloc(size);
        if (!own) { vfs_close(fd); return -1; }
        memcpy_asm(own, data, size);
        vfs_close(fd);
        dynlibs[slot].used = 1;
        dynlibs[slot].elf = own;
        dynlibs[slot].elf_size = size;
        strncpy(dynlibs[slot].path, path, sizeof(dynlibs[slot].path) - 1);
        dynlibs[slot].path[sizeof(dynlibs[slot].path) - 1] = '\0';
    }
    libseg_map((uint64_t*)self->page_directory, dynlibs[slot].segs, dynlibs[slot].nsegs);
    return slot + 1;
}

/* dlsym(handle, name): resolve `name` to its VA via the library's symbol table. */
uint64_t do_dlsym(long handle, const char* name) {
    if (handle < 1 || handle > MAX_DYNLIBS) return 0;
    dynlib_t* lib = &dynlibs[handle - 1];
    if (!lib->used || !lib->elf) return 0;

    elf64_hdr_t* h = (elf64_hdr_t*)lib->elf;
    if (h->e_shoff + (uint64_t)h->e_shnum * sizeof(elf64_shdr_t) > lib->elf_size) return 0;
    elf64_shdr_t* sh = (elf64_shdr_t*)(lib->elf + h->e_shoff);
    for (int i = 0; i < h->e_shnum; i++) {
        if (sh[i].sh_type != SHT_SYMTAB || sh[i].sh_entsize == 0) continue;
        if (sh[i].sh_link >= h->e_shnum) continue;

        /* Everything below comes from a file a user can write, so bound it all.
         * Only e_shoff/e_shnum and sh_link were checked before: sh_offset,
         * sh_size and st_name were taken on trust, so a crafted .so made this
         * loop read wherever it liked and strcmp walk off the end of the image. */
        elf64_shdr_t* strsh = &sh[sh[i].sh_link];
        if (strsh->sh_offset > lib->elf_size ||
            strsh->sh_size   > lib->elf_size - strsh->sh_offset) continue;
        if (sh[i].sh_offset  > lib->elf_size ||
            sh[i].sh_size    > lib->elf_size - sh[i].sh_offset) continue;

        const char* str = (const char*)(lib->elf + strsh->sh_offset);
        uint64_t strsz = strsh->sh_size;
        /* With the table NUL-terminated at its end, any in-range st_name yields a
         * string that terminates inside it — which is what makes strcmp safe. */
        if (strsz == 0 || str[strsz - 1] != '\0') continue;

        elf64_sym_t* syms = (elf64_sym_t*)(lib->elf + sh[i].sh_offset);
        int nsyms = (int)(sh[i].sh_size / sh[i].sh_entsize);
        for (int s = 0; s < nsyms; s++) {
            if (!syms[s].st_value) continue;
            if (syms[s].st_name >= strsz) continue;
            if (strcmp(str + syms[s].st_name, name) == 0) return syms[s].st_value;
        }
    }
    return 0;
}
