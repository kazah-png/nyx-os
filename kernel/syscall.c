#include "kernel.h"
#include "elf.h"

extern void* syscall_table[SYS_TABLE_SIZE];

void init_syscalls(void) {
    memset_asm(syscall_table, 0, sizeof(syscall_table));
}

static process_t* get_cur_proc(void) {
    extern process_t* get_current_process(void);
    return get_current_process();
}

uint32_t syscall_handler_c(uint32_t no, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    (void)a4; (void)a5;
    switch (no) {
        case SYS_EXIT: {  // exit(status)
            printf("[USER] exit(%u)\n", a1);
            process_t* cur = get_cur_proc();
            if (cur) cur->state = 0;
            for (;;) __asm__ volatile("hlt");
            return 0;
        }
        case SYS_WRITE: {  // write(fd, buf, len)
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            int len = (int)a3;
            if (fd == 1 || fd == 2) {
                for (int i = 0; i < len; i++) putchar(buf[i]);
            }
            return len;
        }
        case SYS_PRINT: {  // print string
            printf("%s", (const char*)a1);
            return 0;
        }
        case SYS_OPEN: {  // open(path, flags, mode)
            const char* path = (const char*)a1;
            int flags = (int)a2;
            int mode = (int)a3;
            return vfs_open(path, flags, mode);
        }
        case SYS_READ: {  // read(fd, buf, count)
            int fd = (int)a1;
            void* buf = (void*)a2;
            int count = (int)a3;
            return vfs_read(fd, buf, count);
        }
        case SYS_CLOSE: {  // close(fd)
            return vfs_close((int)a1);
        }
        case SYS_GETPID: {  // getpid()
            process_t* cur = get_cur_proc();
            return cur ? cur->pid : 0;
        }
        case SYS_SBRK: {  // sbrk(increment)
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            int inc = (int)a1;
            uint32_t old_brk = cur->program_break;
            if (inc == 0) return old_brk;
            if (inc < 0) return -1; // We don't support shrinking
            uint32_t new_brk = old_brk + inc;
            // Allocate pages as needed (up to stack at 0xD0000000)
            uint32_t start_page = (old_brk + 0xFFF) & ~0xFFF;
            uint32_t end_page = (new_brk + 0xFFF) & ~0xFFF;
            if (end_page > 0xD0000000) return -1;
            uint32_t* pd = (uint32_t*)cur->page_directory;
            for (uint32_t page = start_page; page < end_page; page += 4096) {
                void* phys = alloc_page();
                if (!phys) return -1;
                map_page_dir(pd, phys, (void*)page, 0x7); // rwx user
            }
            cur->program_break = new_brk;
            return old_brk;
        }
        case SYS_FSIZE: {  // fsize(fd)
            return vfs_fsize((int)a1);
        }
        case SYS_EXEC: {  // exec(path)
            // Copy path from userspace
            char path[256];
            int i;
            for (i = 0; i < 255; i++) {
                path[i] = ((const char*)a1)[i];
                if (path[i] == 0) break;
            }
            path[i] = 0;

            process_t* cur = get_cur_proc();
            if (!cur) return -6;

            // Open ELF file via VFS
            int fd = vfs_open(path, 0, 0);
            if (fd < 0) return -1;
            uint32_t size = vfs_fsize(fd);
            uint8_t* data = vfs_fdata(fd);
            if (!data || size == 0) { vfs_close(fd); return -2; }

            uint8_t* copy = (uint8_t*)kmalloc(size);
            if (!copy) { vfs_close(fd); return -3; }
            memcpy_asm(copy, data, size);
            vfs_close(fd);

            if (!elf_validate(copy, size)) { kfree(copy); return -4; }

            elf32_hdr_t* hdr = (elf32_hdr_t*)copy;
            uint32_t entry = hdr->e_entry;

            // Create new user page directory and load PT_LOAD segments
            uint32_t* new_pd = alloc_page_directory();
            if (!new_pd) { kfree(copy); return -5; }

            elf32_phdr_t* phdr = (elf32_phdr_t*)(copy + hdr->e_phoff);
            uint32_t max_addr = 0;

            for (uint32_t si = 0; si < hdr->e_phnum; si++) {
                if (phdr[si].p_type != PT_LOAD) continue;
                uint32_t vaddr = phdr[si].p_vaddr;
                uint32_t memsz = phdr[si].p_memsz;
                uint32_t filesz = phdr[si].p_filesz;
                uint32_t offset = phdr[si].p_offset;

                if (offset + filesz > size) { kfree(copy); return -6; }

                uint32_t start_page = vaddr & ~0xFFF;
                uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;

                for (uint32_t page = start_page; page < end_page; page += 4096) {
                    void* phys = alloc_page();
                    if (!phys) { kfree(copy); return -7; }
                    uint32_t flags = 0x7;
                    if (!(phdr[si].p_flags & PF_W)) flags = 0x5;
                    map_page_dir(new_pd, phys, (void*)page, flags);

                    uint32_t copy_start = (page > vaddr) ? page : vaddr;
                    uint32_t copy_end = (page + 4096 < vaddr + filesz) ? page + 4096 : vaddr + filesz;
                    if (copy_start < copy_end) {
                        uint32_t copy_sz = copy_end - copy_start;
                        uint32_t file_off = offset + (copy_start - vaddr);
                        uint32_t dst_off = copy_start - page;
                        memcpy_asm((uint8_t*)phys + dst_off, copy + file_off, copy_sz);
                    }

                    uint32_t page_end = page + 4096;
                    uint32_t seg_data_end = vaddr + filesz;
                    uint32_t seg_mem_end = vaddr + memsz;
                    if (page_end > seg_data_end && page < seg_mem_end) {
                        uint32_t zero_start = (seg_data_end > page) ? (seg_data_end - page) : 0;
                        uint32_t zero_end = (seg_mem_end < page_end) ? (seg_mem_end - page) : 4096;
                        if (zero_end > zero_start)
                            memset_asm((uint8_t*)phys + zero_start, 0, zero_end - zero_start);
                    }
                }

                uint32_t end = vaddr + memsz;
                if (end > max_addr) max_addr = end;
            }

            kfree(copy);

            // Allocate user stack (one page below 0xD0000000)
            void* stack_phys = alloc_page();
            if (!stack_phys) return -8;
            uint32_t stack_top = 0xD0000000;
            uint32_t stack_virt = stack_top - 4096;
            map_page_dir(new_pd, stack_phys, (void*)stack_virt, 0x7);

            uint32_t program_break = (max_addr + 0xFFF) & ~0xFFF;

            // Create kernel stack frame for syscall return
            void* kstack = kmalloc(4096);
            if (!kstack) return -9;
            uint32_t* sp = (uint32_t*)((uint32_t)kstack + 4096);

            *--sp = 0x23;           // SS (user data, ring 3)
            *--sp = stack_top;      // ESP (user stack top)
            *--sp = 0x200;          // EFLAGS (IF=1)
            *--sp = 0x1B;           // CS (user code, ring 3)
            *--sp = entry;          // EIP

            *--sp = 0;  // edi
            *--sp = 0;  // esi
            *--sp = 0;  // ebp
            *--sp = 0;  // old_esp
            *--sp = 0;  // ebx
            *--sp = 0;  // edx
            *--sp = 0;  // ecx
            *--sp = 0;  // eax

            // Replace current process in-place
            cur->page_directory = new_pd;
            cur->stack = sp;
            cur->kernel_stack = (void*)((uint32_t)kstack + 4096);
            cur->program_break = program_break;
            strncpy(cur->comm, path, 31);

            // Switch to new PD and set TSS
            switch_page_directory(new_pd);
            tss_set_stack((uint32_t)cur->kernel_stack);

            // Jump to ring 3 — never returns
            __asm__ volatile(
                "movl %0, %%esp\n"
                "popa\n"
                "iret\n"
                :
                : "r"(sp)
                : "memory"
            );

            return 0; // never reached
        }
        default:
            printf("[SYSCALL] Unknown syscall %u\n", no);
            return -1;
    }
}

void* get_syscall_table(void) {
    return syscall_table;
}

void register_syscall(uint32_t num, void* handler) {
    if (num < SYS_TABLE_SIZE) syscall_table[num] = handler;
}
