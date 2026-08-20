#include "../core/kernel.h"
#include "elf.h"

int elf_validate(const uint8_t* data, uint32_t size) {
    if (!data || size < sizeof(elf64_hdr_t)) return 0;
    elf64_hdr_t* hdr = (elf64_hdr_t*)data;
    if (*(uint32_t*)hdr->e_ident != ELF_MAGIC) return 0;
    if (hdr->e_ident[4] != ELF_64BIT) return 0;
    if (hdr->e_ident[5] != ELF_LITTLE_ENDIAN) return 0;
    if (hdr->e_machine != EM_X86_64) return 0;
    if (hdr->e_type != ELF_EXEC) return 0;
    if (hdr->e_phnum == 0) return 0;
    // The program-header table must be well-formed and lie inside the image. These
    // checks are PURE (side-effect-free), so they belong here — every elf_validate
    // caller (not just elf_load_image) then rejects a truncated or overflowing table.
    // e_phentsize must EXACTLY equal our struct: the size check below strides by it
    // while the phdr[i] indexing in elf_load_image strides by sizeof(elf64_phdr_t), so
    // any mismatch lets a small check guard a large read. The math is 64-bit and
    // subtraction-first, so hostile e_phnum/e_phoff (up to 2^16 * 2^16, or an e_phoff
    // near 2^64) cannot wrap past it.
    if (hdr->e_phentsize != sizeof(elf64_phdr_t)) return 0;
    if (hdr->e_phoff > size) return 0;
    if ((uint64_t)hdr->e_phnum * sizeof(elf64_phdr_t) > size - hdr->e_phoff) return 0;
    return 1;
}

// Load an ELF into a FRESH address space: allocate a page directory, map every
// PT_LOAD segment (with W^X page flags), and set up a one-page user stack. On
// success returns 0 and hands back the page directory, entry point, initial user
// stack top, and program break; on failure frees the partial page directory and
// returns -1. Shared by elf_load (spawn a new process_t) and do_execve (replace
// the caller's image in place).
int elf_load_image(const uint8_t* data, uint32_t size, uint64_t** out_pd,
                   uint64_t* out_entry, uint64_t* out_stack_top, uint64_t* out_brk) {
    if (!elf_validate(data, size)) return -1;

    elf64_hdr_t* hdr = (elf64_hdr_t*)data;
    // The program-header table's well-formedness (e_phentsize == our struct,
    // e_phoff + e_phnum*phentsize within the image, all 64-bit / overflow-safe) is
    // now enforced by elf_validate() above, so the phdr[i] indexing below is bounded.

    uint64_t* pd = alloc_page_directory();
    if (!pd) return -1;

    // The user stack. `stack_virt` is the base of the TOP page (which holds the SysV
    // entry frame); the stack grows DOWN from there. Commit only USER_STACK_INIT_PAGES
    // up front — vm_handle_fault demand-grows the rest on touch, down to the
    // USER_STACK_LOW floor (kernel.h), with a guard page below it. So a deep call chain
    // gets far more than the old fixed 64 KB while a plain program commits almost nothing.
    void* stack_phys = alloc_page();     // top page: holds the argc/argv entry frame
    if (!stack_phys) { free_page_directory(pd); return -1; }
    uint64_t stack_virt = USER_STACK_TOP;
    map_page_dir(pd, stack_phys, (void*)stack_virt, 0x7 | PAGE_NX);
    for (int sp = 1; sp < USER_STACK_INIT_PAGES; sp++) {   // rest are demand-grown
        void* pg = alloc_page();
        if (!pg) { free_page_directory(pd); return -1; }
        map_page_dir(pd, pg, (void*)(stack_virt - (uint64_t)sp * 4096), 0x7 | PAGE_NX);
    }

    // Empty SysV entry frame at the stack top: [argc=0][argv NULL][envp NULL][pad].
    // crt0 reads argc/argv from [rsp] on EVERY launch, so a plain spawn needs a
    // valid (empty) frame too; execve() overwrites this with the real argv. The
    // 32-byte size keeps the entry RSP 16-byte aligned. Written through the
    // identity-mapped physical page (the new pd isn't the active CR3 here).
    uint64_t* stk = (uint64_t*)((uint8_t*)stack_phys + 4096 - 32);
    stk[0] = 0;                  // argc = 0
    stk[1] = 0;                  // argv[0] = NULL (terminator)
    stk[2] = 0;                  // envp terminator
    stk[3] = 0;                  // padding (16-byte alignment)

    // The entry point ends up in an iretq frame. A non-canonical or higher-half
    // e_entry therefore does not kill the process — it makes the RETURN TO USER
    // fault in ring 0, i.e. a kernel panic triggered by running a crafted file.
    uint64_t entry = hdr->e_entry;
    if (entry < USER_SPACE_MIN || entry >= USER_SPACE_END) { free_page_directory(pd); return -1; }

    elf64_phdr_t* phdr = (elf64_phdr_t*)(data + hdr->e_phoff);

    for (uint32_t i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t offset = phdr[i].p_offset;

        // Overflow-safe file-extent check: `offset + filesz` alone wraps, so a
        // crafted p_offset near 2^64 passed it and then copied from far outside
        // the image buffer.
        if (offset > size || filesz > size - offset) { free_page_directory(pd); return -1; }

        // The segment must land in USER space. Without this, a PT_LOAD with a
        // higher-half p_vaddr walked into PML4[511] — which alloc_page_directory
        // copies BY VALUE from the kernel's own PML4 — and installed mappings
        // into the page tables the kernel itself runs on: either grafting a
        // ring-3-accessible page at a fixed kernel VA in every address space
        // (never reclaimed, since free_page_directory stops below PML4[511]), or
        // writing a PTE straight into identity-mapped kernel RAM. A user program
        // only had to write a file and execve it.
        if (memsz > USER_SPACE_END ||
            vaddr < USER_SPACE_MIN || vaddr > USER_SPACE_END - memsz) {
            free_page_directory(pd);
            return -1;
        }

        // A practical ceiling on one segment. The user-space bound above still
        // permits a p_memsz of terabytes, and the loop below allocates a frame
        // per page with no way out — so a single crafted PT_LOAD would walk off
        // with every free page in the system before failing. 256 MB is far more
        // than any NyxOS binary needs and bounds the loop at 65536 iterations.
        if (memsz > ELF_MAX_SEGMENT) { free_page_directory(pd); return -1; }

        uint64_t start_page = vaddr & ~0xFFFULL;
        uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

        for (uint64_t page = start_page; page < end_page; page += 4096) {
            void* phys = alloc_page();
            if (!phys) { free_page_directory(pd); return -1; }
            uint64_t flags = 0x7;
            if (!(phdr[i].p_flags & PF_W)) flags = 0x5;
            if (!(phdr[i].p_flags & PF_X)) flags |= PAGE_NX;
            map_page_dir(pd, phys, (void*)page, flags);

            uint64_t copy_start = (page > vaddr) ? page : vaddr;
            uint64_t copy_end = (page + 4096 < vaddr + filesz) ? page + 4096 : vaddr + filesz;
            if (copy_start < copy_end) {
                uint64_t copy_sz = copy_end - copy_start;
                uint64_t file_off = offset + (copy_start - vaddr);
                uint64_t dst_off = copy_start - page;
                memcpy_asm((uint8_t*)phys + dst_off, (uint8_t*)(data + file_off), copy_sz);
            }

            uint64_t page_end = page + 4096;
            uint64_t segment_data_end = vaddr + filesz;
            uint64_t segment_mem_end = vaddr + memsz;
            if (page_end > segment_data_end && page < segment_mem_end) {
                uint64_t zero_start = (segment_data_end > page) ? (segment_data_end - page) : 0;
                uint64_t zero_end = (segment_mem_end < page_end) ? (segment_mem_end - page) : 4096;
                if (zero_end > zero_start) {
                    memset_asm((uint8_t*)phys + zero_start, 0, (size_t)(zero_end - zero_start));
                }
            }
        }
    }

    uint64_t max_addr = 0;
    for (uint32_t i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint64_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (end > max_addr) max_addr = end;
    }

    // Map the shared libc (read-only code + private .bss) into this address space
    // so the program's `--just-symbols` calls to printf/malloc/... resolve. fork
    // inherits it via clone_page_directory_cow; a no-op if libc didn't load.
    shared_libc_map(pd);

    *out_pd = pd;
    *out_entry = entry;
    *out_stack_top = stack_virt + 4096 - 32;   // entry RSP points at the argc frame
    *out_brk = (max_addr + 0xFFF) & ~0xFFFULL;
    return 0;
}

// Load an ELF into a new ring-3 process, optionally seeding its stack with an argv
// frame (argc==0 / argv==NULL → the classic no-argument launch). Shared by elf_load
// (no args) and spawn_user_path_args (kernel shell `exec <file> a b c`).
int elf_load_args(const uint8_t* data, uint32_t size, char* const* argv, int argc,
                  process_t** out_proc) {
    uint64_t* pd; uint64_t entry, stack_top, brk;
    if (elf_load_image(data, size, &pd, &entry, &stack_top, &brk) != 0) return -1;

    // Seed argv on the fresh stack (a no-op that returns stack_top when argc==0).
    uint64_t rsp = build_argv_stack(pd, stack_top, argv, argc);

    process_t* proc = create_user_process("elf", (void*)entry, (void*)rsp, pd);
    if (!proc) { free_page_directory(pd); return -1; }
    // heap_start before program_break. These are two plain stores to a struct
    // another CPU can read at any instant — vm_handle_fault reaches this process
    // through tg_leader() -> find_process(), which walks the process table with no
    // lock at all — and a fresh process_t is zeroed. Publishing program_break
    // first therefore exposes the window [0, brk) for an instant, and brk for a
    // small binary is around 0x11000, so that window spans the program's own
    // .text at 0x10000. This order makes the intermediate window [brk, 0), empty.
    //
    // HONEST SCOPE: this is hardening, NOT a fix for the intermittent [fault]
    // hunted at v5.9.0. That fault (`pid N (threads): Page Fault #14 at RIP
    // 0x102b3 err 0x15`, an NX violation on the process's own text) REPRODUCED
    // with this ordering and with the paging.c window guard both in place —
    // 1 hit in 23 rounds, statistically identical to the 1 in 19 before them.
    // Whatever maps a non-executable page over live user text, it is not this.
    proc->heap_start = brk;      // heap grows lazily from here (see vm_handle_fault)
    proc->program_break = brk;

    *out_proc = proc;
    return 0;
}

int elf_load(const uint8_t* data, uint32_t size, process_t** out_proc) {
    return elf_load_args(data, size, (char* const*)0, 0, out_proc);
}

// KAT: elf_validate is the gate every ELF (from disk, initramfs, or a user execve)
// passes before the loader touches it, so its header + program-header-table checks are
// a security boundary. Build one well-formed image, confirm it passes, then mutate each
// field a hostile file could set and confirm each is REJECTED — locking the header
// hardening against regressions. Pure (no allocation / paging), so it runs in the
// offline self-test battery.
int elf_selftest(void) {
    uint8_t buf[sizeof(elf64_hdr_t) + sizeof(elf64_phdr_t)];
    for (unsigned i = 0; i < sizeof(buf); i++) buf[i] = 0;
    elf64_hdr_t* h = (elf64_hdr_t*)buf;
    h->e_ident[0] = 0x7F; h->e_ident[1] = 'E'; h->e_ident[2] = 'L'; h->e_ident[3] = 'F';
    h->e_ident[4] = ELF_64BIT; h->e_ident[5] = ELF_LITTLE_ENDIAN;
    h->e_type = ELF_EXEC; h->e_machine = EM_X86_64;
    h->e_phoff = sizeof(elf64_hdr_t); h->e_phentsize = sizeof(elf64_phdr_t); h->e_phnum = 1;
    uint32_t sz = (uint32_t)sizeof(buf);

    if (!elf_validate(buf, sz)) return 1;                          // the valid image must pass
    { uint8_t s = buf[0];        buf[0] = 0;               if (elf_validate(buf, sz)) return 2;  buf[0] = s; }        // bad magic
    { uint8_t s = h->e_ident[4]; h->e_ident[4] = 1;        if (elf_validate(buf, sz)) return 3;  h->e_ident[4] = s; } // 32-bit class
    { uint8_t s = h->e_ident[5]; h->e_ident[5] = 2;        if (elf_validate(buf, sz)) return 4;  h->e_ident[5] = s; } // big-endian
    { uint16_t s = h->e_machine; h->e_machine = 40;        if (elf_validate(buf, sz)) return 5;  h->e_machine = s; }  // not x86-64 (ARM)
    { uint16_t s = h->e_type;    h->e_type = 1;            if (elf_validate(buf, sz)) return 6;  h->e_type = s; }     // ET_REL, not ET_EXEC
    { uint16_t s = h->e_phnum;   h->e_phnum = 0;           if (elf_validate(buf, sz)) return 7;  h->e_phnum = s; }    // no program headers
    { uint16_t s = h->e_phentsize; h->e_phentsize = 1;     if (elf_validate(buf, sz)) return 8;  h->e_phentsize = s; }// wrong phentsize
    { uint64_t s = h->e_phoff;   h->e_phoff = sz + 1;      if (elf_validate(buf, sz)) return 9;  h->e_phoff = s; }    // table starts past EOF
    { uint16_t s = h->e_phnum;   h->e_phnum = 1000;        if (elf_validate(buf, sz)) return 10; h->e_phnum = s; }    // phnum*56 runs off the end
    { uint64_t s = h->e_phoff;   h->e_phoff = 0xFFFFFFFFFFFFFFF0ull; if (elf_validate(buf, sz)) return 11; h->e_phoff = s; } // e_phoff near 2^64 (wrap probe)
    if (elf_validate(buf, sizeof(elf64_hdr_t) - 1)) return 12;      // smaller than a header
    if (elf_validate((const uint8_t*)0, sz))        return 13;      // NULL data
    if (!elf_validate(buf, sz)) return 14;                          // restored image still valid (no mutation leaked)
    return 0;
}
