#include "kernel.h"
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
    // e_phnum and e_phentsize are uint16_t, so they promote to int and the
    // program-header table size is computed in 32-bit — for hostile values
    // (up to 65535 * 65535) that overflows int (signed UB) before it widens to
    // the 64-bit comparison, which could sneak a too-large table past this bound
    // check (CodeQL cpp/integer-multiplication-cast-to-long). Do the math in
    // 64-bit so the size is exact.
    if (hdr->e_phoff + (uint64_t)hdr->e_phnum * hdr->e_phentsize > size) return -1;

    uint64_t* pd = alloc_page_directory();
    if (!pd) return -1;

    void* stack_phys = alloc_page();
    if (!stack_phys) { free_page_directory(pd); return -1; }
    uint64_t stack_virt = 0x00007FFFFFFFE000ULL;
    map_page_dir(pd, stack_phys, (void*)stack_virt, 0x7 | PAGE_NX);

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

    uint64_t entry = hdr->e_entry;
    elf64_phdr_t* phdr = (elf64_phdr_t*)(data + hdr->e_phoff);

    for (uint32_t i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t offset = phdr[i].p_offset;

        if (offset + filesz > size) { free_page_directory(pd); return -1; }
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

    *out_pd = pd;
    *out_entry = entry;
    *out_stack_top = stack_virt + 4096 - 32;   // entry RSP points at the argc frame
    *out_brk = (max_addr + 0xFFF) & ~0xFFFULL;
    return 0;
}

int elf_load(const uint8_t* data, uint32_t size, process_t** out_proc) {
    uint64_t* pd; uint64_t entry, stack_top, brk;
    if (elf_load_image(data, size, &pd, &entry, &stack_top, &brk) != 0) return -1;

    process_t* proc = create_user_process("elf", (void*)entry, (void*)stack_top, pd);
    if (!proc) { free_page_directory(pd); return -1; }
    proc->program_break = brk;
    proc->heap_start = brk;      // heap grows lazily from here (see vm_handle_fault)

    *out_proc = proc;
    return 0;
}
