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

// Set up syscall MSRs for the syscall/sysret mechanism
void setup_syscall_msrs(void) {
    // STAR:  [63:48] = sysret CS for ring 3, [47:32] = syscall CS (ring 0)
    //        [31:0]  = not used (legacy SYSCALL EIP)
    uint64_t star = ((uint64_t)USER_CS << 48) | ((uint64_t)KERNEL_CS << 32);
    write_msr(MSR_STAR, star);

    // LSTAR: RIP of syscall entry point
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    // SF_MASK: clear IF (bit 9) and DF (bit 10) during syscall
    write_msr(MSR_SF_MASK, (1 << 9) | (1 << 10));
}

// Provide kernel stack to user processes via the syscall_entry global variable
extern uint64_t user_rsp;
extern uint64_t kernel_rsp;

void set_kernel_rsp(uint64_t rsp) {
    kernel_rsp = rsp;
}

uint64_t syscall_handler(uint64_t no, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    switch (no) {
        case SYS_EXIT: {
            printf("[USER] exit(%lu)\n", a1);
            process_t* cur = get_cur_proc();
            if (cur) cur->state = 0;
            for (;;) __asm__ volatile("hlt");
            return 0;
        }
        case SYS_WRITE: {
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            int len = (int)a3;
            if (fd == 1 || fd == 2) {
                for (int i = 0; i < len; i++) putchar(buf[i]);
            }
            return len;
        }
        case SYS_PRINT: {
            printf("%s", (const char*)a1);
            return 0;
        }
        case SYS_OPEN: {
            const char* path = (const char*)a1;
            int flags = (int)a2;
            int mode = (int)a3;
            return vfs_open(path, flags, mode);
        }
        case SYS_READ: {
            int fd = (int)a1;
            void* buf = (void*)a2;
            int count = (int)a3;
            return vfs_read(fd, buf, count);
        }
        case SYS_CLOSE: {
            return vfs_close((int)a1);
        }
        case SYS_GETPID: {
            process_t* cur = get_cur_proc();
            return cur ? cur->pid : 0;
        }
        case SYS_SBRK: {
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            int64_t inc = (int64_t)a1;
            uint64_t old_brk = cur->program_break;
            if (inc == 0) return old_brk;
            if (inc < 0) return -1;
            uint64_t new_brk = old_brk + inc;
            uint64_t start_page = (old_brk + 0xFFF) & ~0xFFFULL;
            uint64_t end_page = (new_brk + 0xFFF) & ~0xFFFULL;
            if (end_page > 0x100000000ULL) return -1;
            uint64_t* pml4 = (uint64_t*)cur->page_directory;
            for (uint64_t page = start_page; page < end_page; page += 4096) {
                void* phys = alloc_page();
                if (!phys) return -1;
                map_page_dir(pml4, phys, (void*)page, 0x7 | PAGE_NX);
            }
            cur->program_break = new_brk;
            return old_brk;
        }
        case SYS_FSIZE: {
            return vfs_fsize((int)a1);
        }
        case SYS_EXEC: {
            const char* path = (const char*)a1;
            int fd = vfs_open(path, 0, 0);
            if (fd < 0) return -1;
            uint32_t size = vfs_fsize(fd);
            uint8_t* data = vfs_fdata(fd);
            if (!data || size == 0) { vfs_close(fd); return -1; }
            uint8_t* copy = (uint8_t*)kmalloc(size);
            if (!copy) { vfs_close(fd); return -1; }
            memcpy_asm(copy, data, size);
            vfs_close(fd);
            if (!elf_validate(copy, size)) { kfree(copy); return -2; }
            process_t* new_proc = NULL;
            int ret = elf_load(copy, size, &new_proc);
            kfree(copy);
            if (ret != 0 || !new_proc) return -3;
            printf("[EXEC] Loaded %s as PID %lu\n", path, new_proc->pid);
            return (uint64_t)new_proc->pid;
        }
        default:
            printf("[SYSCALL] Unknown syscall %lu\n", no);
            return -1;
    }
}

void* get_syscall_table(void) {
    return syscall_table;
}

void register_syscall(uint32_t num, void* handler) {
    if (num < SYS_TABLE_SIZE) syscall_table[num] = handler;
}
