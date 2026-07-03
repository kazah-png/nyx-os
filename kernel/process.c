#include "kernel.h"

extern process_t* process_table[MAX_PROCESSES];
extern int process_count;
static uint64_t next_pid = 1;

int current_idx = 0;

void init_process(void) {
    memset_asm(process_table, 0, sizeof(process_table));
    process_t* init = (process_t*)kmalloc(sizeof(process_t));
    if (init) {
        memset_asm(init, 0, sizeof(process_t));
        init->pid = next_pid++;
        init->ppid = 0;
        init->state = 1;
        strncpy(init->comm, "init", 31);
        // page_directory left NULL -- scheduler uses kernel_pml4_phys (physical addr)
        process_table[process_count++] = init;
    }
}

// Set up a kernel stack for a new process.
// Stack layout (from low to high addr) matches switch_context restore:
//   r15, r14, r13, r12, r11, r10, r9, r8,
//   rbp, rdi, rsi, rdx, rcx, rbx, rax, rip_old
// Then after SAVE_REGS (old iret frame):
//   r15, r14, r13, r12, r11, r10, r9, r8,
//   rbp, rdi, rsi, rdx, rcx, rbx, rax,
//   int_no(32), error(0), rip, cs, rflags, rsp, ss
static int init_task_stack(process_t* proc, void* entry_point) {
    void* stack_mem = kmalloc(4096);
    if (!stack_mem) return -1;
    uint64_t* sp = (uint64_t*)((uintptr_t)stack_mem + 4096);

    // iretq frame for kernel process (ring 0). In long mode iretq ALWAYS pops
    // SS:RSP, even without a privilege change, so SS must be the kernel *data*
    // selector (0x10) — a writable data segment. It used to say 0x08 (the kernel
    // *code* selector); loading a code segment into SS #GP's (error=SS selector).
    // That bug stayed latent until preemptive scheduling actually iretq'd into one
    // of these frames.
    *--sp = KERNEL_DS;         // ss = kernel data (0x10)
    *--sp = (uint64_t)(uintptr_t)stack_mem + 4096; // rsp
    *--sp = 0x202;             // rflags (IF set)
    *--sp = KERNEL_CS;         // cs = kernel code (0x08)
    *--sp = (uint64_t)(uintptr_t)entry_point; // rip

    *--sp = 0;                 // error code
    *--sp = 32;                // int number (irq0)

    // SAVE_REGS: 15 zeros
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;

    proc->stack = (void*)((uintptr_t)sp);
    proc->kernel_stack = (void*)((uintptr_t)stack_mem + 4096);
    return 0;
}

static int init_user_task_stack(process_t* proc, void* entry_point, void* user_stack_top) {
    void* stack_mem = kmalloc(4096);
    if (!stack_mem) return -1;
    uint64_t* sp = (uint64_t*)((uintptr_t)stack_mem + 4096);

    // iretq frame for user process (ring 3)
    // When iretq detects CS.DPL != current CPL, it pops SS:RSP too (5 values)
    *--sp = USER_DS;            // ss = user data (ring 3)
    *--sp = (uint64_t)(uintptr_t)user_stack_top; // rsp = user stack
    *--sp = 0x202;              // rflags (IF set)
    *--sp = USER_CS;            // cs = user code (ring 3, L-bit=1)
    *--sp = (uint64_t)(uintptr_t)entry_point; // rip

    *--sp = 0;                  // error code
    *--sp = 32;                 // int number (irq0)

    // SAVE_REGS: 15 zeros
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;

    // Store the saved kernel-stack RSP as its HIGHER-HALF alias. When we iretq into
    // this process we first switch CR3 to its user page directory, where the only
    // mapping of kernel memory is the PML4[511] higher-half mirror — the low
    // identity address would #PF on the first stack access (RESTORE_REGS). This
    // keeps user procs consistent with the value the scheduler later saves (the
    // TSS RSP0 is also a higher-half alias). Kernel threads stay low (init_task_stack).
    proc->stack = (void*)((uintptr_t)sp + KERNEL_BASE);
    proc->kernel_stack = (void*)((uintptr_t)stack_mem + 4096);
    return 0;
}

process_t* create_process(const char* name, void* entry, uint64_t flags) {
    (void)flags;
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = 0;
    p->state = 1;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';
    if (entry) {
        if (init_task_stack(p, entry) < 0) {
            kfree(p);
            return NULL;
        }
    }
    process_table[process_count++] = p;
    return p;
}

process_t* create_user_process(const char* name, void* entry, void* user_stack, uint64_t* page_dir) {
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->state = 1;
    p->page_directory = page_dir;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';

    if (!user_stack) {
        void* stack_page = alloc_page();
        if (!stack_page) { kfree(p); return NULL; }
        uint64_t stack_virt = 0x00007FFFFFFFE000ULL;
        map_page_dir(page_dir, stack_page, (void*)stack_virt, 0x7 | PAGE_NX);
        user_stack = (void*)(stack_virt + 4096);
    }

    if (init_user_task_stack(p, entry, user_stack) < 0) {
        kfree(p);
        return NULL;
    }
    process_table[process_count++] = p;
    return p;
}

extern void switch_to_user_trampoline(void);
extern uint64_t ku_setjmp(uint64_t* buf);
extern void ku_longjmp(uint64_t* buf, uint64_t val);

// The user process currently executing in ring 3 (launched via switch_to_user_process).
// The cooperative scheduler doesn't advance current_idx for these, so syscall handlers
// use this to resolve getpid()/sbrk()/exit() to the right process.
process_t* g_user_proc = NULL;

// Saved kernel context so a user exit() can unwind back to the caller (the shell's
// `exec`) instead of halting the whole system.
static uint64_t user_return_ctx[8];

// Called from the SYS_EXIT handler to return control to switch_to_user_process's caller.
void return_from_user_process(void) {
    ku_longjmp(user_return_ctx, 1);
    for (;;) __asm__ volatile("hlt");   // unreachable
}

void switch_to_user_process(process_t* proc) {
    if (!proc || !proc->page_directory) return;

    // Save a return point. When the user process exits, the SYS_EXIT handler calls
    // return_from_user_process(), which longjmps back here with a non-zero result.
    if (ku_setjmp(user_return_ctx) != 0) {
        g_user_proc = NULL;
        __asm__ volatile("sti");        // re-enable interrupts for the caller (shell)
        return;
    }

    g_user_proc = proc;
    tss_set_stack((uint64_t)(uintptr_t)proc->kernel_stack + KERNEL_BASE);
    uint64_t tramp = (uint64_t)switch_to_user_trampoline + KERNEL_BASE;
    uint64_t rsp_val = (uint64_t)(uintptr_t)proc->stack;
    uint64_t cr3_val = (uint64_t)proc->page_directory;
    __asm__ volatile(
        "cli \n"
        "mov %0, %%rdi \n"
        "mov %1, %%rsi \n"
        "mov %2, %%rax \n"
        "call *%%rax \n"
        :: "r"(rsp_val), "r"(cr3_val), "r"(tramp)
        : "rdi", "rsi", "rax"
    );
    for (;;) __asm__ volatile("hlt");
}

// Free everything owned by an exited user process: its page directory (all user
// pages + tables), the kernel/context stack (init_user_task_stack kmalloc'd
// kernel_stack-4096 as the base), the process_t, and its process-table slot.
// Only call once the process is no longer executing (e.g. after
// switch_to_user_process returns via the exit longjmp).
void reap_user_process(process_t* proc) {
    if (!proc) return;
    extern void free_page_directory(uint64_t* pml4);
    if (proc == g_user_proc) g_user_proc = NULL;
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] == proc) {
            process_table[i] = process_table[--process_count];
            break;
        }
    }
    if (proc->page_directory) free_page_directory((uint64_t*)proc->page_directory);
    if (proc->kernel_stack) kfree((void*)((uintptr_t)proc->kernel_stack - 4096));
    kfree(proc);
}

void destroy_process(uint64_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            // reap_user_process removes the slot and frees the page directory and
            // the stack via its kmalloc base (proc->stack is a *middle* pointer, so
            // the old kfree(proc->stack) here corrupted the heap).
            reap_user_process(process_table[i]);
            return;
        }
    }
}

process_t* find_process(uint64_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid)
            return process_table[i];
    }
    return NULL;
}

process_t* get_current_process(void) {
    if (process_count > 0 && current_idx < process_count)
        return process_table[current_idx];
    return NULL;
}

// Global variables for assembly-level context switching
uint64_t saved_rsp = 0;
uint64_t next_rsp = 0;
uint64_t next_cr3 = 0;

// Preemptive round-robin scheduling over KERNEL threads (page_directory==NULL).
// Wiring: irq_common (isr_stubs.asm) saves the interrupted context, stores its
// RSP in saved_rsp, calls this, then loads next_rsp/next_cr3 and iretq's into the
// chosen thread. We only switch between kernel threads that share
// kernel_pml4_phys, so next_cr3 is always the kernel PML4 — no ring or
// address-space transition happens here. The kernel is built -mno-sse/-mno-mmx,
// so the 15 GPRs SAVE_REGS pushes are the complete thread state; nothing else to
// save. Enabled at runtime via sched_enable() (see the `mtdemo` command); dormant
// by default, in which case every tick simply keeps the current context — exactly
// the old cooperative behaviour.
static volatile int sched_enabled = 0;
static process_t* idle_proc = NULL;

void sched_enable(void)     { sched_enabled = 1; }
void sched_disable(void)    { sched_enabled = 0; }
int  sched_is_enabled(void) { return sched_enabled; }

// Preemption critical sections. While nonzero, the scheduler keeps the current
// thread — used to make the heap (kmalloc/kfree) safe against being preempted
// mid-update by another thread that also touches it.
static volatile int preempt_count = 0;
void preempt_disable(void) { preempt_count++; }
void preempt_enable(void)  { if (preempt_count > 0) preempt_count--; }

// Point next_rsp/next_cr3 at process p (about to be resumed). Kernel threads run
// in the kernel address space; user processes get their own CR3, and the TSS
// RSP0 must be their kernel stack so their next ring3→ring0 entry lands safely.
static void sched_target(process_t* p) {
    next_rsp = (uint64_t)p->stack;
    if (p->page_directory) {
        next_cr3 = (uint64_t)p->page_directory;
        tss_set_stack((uint64_t)(uintptr_t)p->kernel_stack + KERNEL_BASE);
    } else {
        next_cr3 = (uint64_t)kernel_pml4_phys;
    }
}

void irq_scheduler_tick(void) {
    // A blocking switch_to_user_process owns the CPU in ring 3 (g_user_proc set).
    // It's launched outside the scheduler and isn't tracked by current_idx, so we
    // must resume it in ITS OWN address space — dropping to the kernel CR3 here
    // would iretq back to ring 3 with the user code unmapped (#PF). Handle this
    // before anything else, regardless of whether preemption is otherwise enabled.
    if (g_user_proc != NULL) {
        next_rsp = saved_rsp;
        next_cr3 = (uint64_t)g_user_proc->page_directory;
        return;
    }

    // Default: resume the thread we interrupted, preserving its address space (a
    // ring-3 process keeps its own CR3 if it's the only thing runnable).
    process_t* cur = (current_idx >= 0 && current_idx < process_count)
                         ? process_table[current_idx] : NULL;
    next_rsp = saved_rsp;
    next_cr3 = (cur && cur->page_directory) ? (uint64_t)cur->page_directory
                                            : (uint64_t)kernel_pml4_phys;

    // Dormant, in a heap/critical section, or nothing else to schedule.
    if (!sched_enabled || preempt_count > 0 || process_count == 0) return;
    if (!cur) return;

    // Remember where to resume the outgoing thread.
    cur->stack = (void*)saved_rsp;

    // Round-robin to the next runnable thread. We schedule kernel threads and
    // spawned (sched_managed) user processes; we skip idle unless nothing else
    // runs, NULL-stack placeholders (e.g. `init` — switching to it would iretq off
    // a null RSP), unmanaged user procs (init.elf, the blocking-exec target), and
    // non-RUN states (parked/zombie).
    for (int i = 1; i <= process_count; i++) {
        int idx = (current_idx + i) % process_count;
        process_t* p = process_table[idx];
        if (!p || p->state != PROC_RUN) continue;
        if (p == idle_proc) continue;
        if (p->stack == NULL) continue;
        if (p->page_directory != NULL && !p->sched_managed) continue;
        current_idx = idx;
        sched_target(p);
        return;
    }
    // No other runnable thread — stay on the current one (defaults above).
}

void schedule(void) {
}

// Free every ZOMBIE process (a spawned user proc that exited): its user address
// space, kernel stack, process_t, and table slot. MUST run in a normal kernel
// thread (the compositor's background-task slot), never in the IRQ path — it
// calls kfree/free_page_directory. preempt_disable keeps the scheduler from
// switching mid-free, and a zombie is never the current thread (it yielded on
// exit), so freeing its stack/CR3 is safe.
void reap_zombies(void) {
    extern void free_page_directory(uint64_t* pml4);
    preempt_disable();
    for (int i = 0; i < process_count; i++) {
        process_t* p = process_table[i];
        if (!p || p->state != PROC_ZOMBIE) continue;
        if (i == current_idx) continue;                 // never free the running proc
        if (p->page_directory) free_page_directory((uint64_t*)p->page_directory);
        if (p->kernel_stack) kfree((void*)((uintptr_t)p->kernel_stack - 4096));
        // Swap-remove, keeping current_idx pointing at the same live proc if the
        // one we moved happened to be the current thread.
        int last = --process_count;
        process_table[i] = process_table[last];
        process_table[last] = NULL;
        if (current_idx == last) current_idx = i;
        kfree(p);
        i--;                                            // re-examine the swapped-in slot
    }
    preempt_enable();
}

static void idle_task(void) {
    while (1) {
        __asm__ volatile("hlt");
    }
}

static int idle_created = 0;

void ensure_idle_process(void) {
    if (idle_created) return;
    idle_created = 1;
    idle_proc = create_process("idle", (void*)idle_task, 0);
}

static void task_blink(void) {
    // (was a serial heartbeat that spammed '.' — removed)
}

static int task_uptime_counter = 0;
static void task_uptime(void) {
    task_uptime_counter++;
    if (task_uptime_counter >= 50) {
        task_uptime_counter = 0;
    }
}

typedef struct {
    char name[16];
    void (*func)(void);
    int active;
} background_task_t;

static background_task_t bg_tasks[8];
static int bg_task_count = 0;
static int bg_task_cur = 0;

void register_background_task(const char* name, void (*func)(void)) {
    if (bg_task_count >= 8) return;
    strncpy(bg_tasks[bg_task_count].name, name, 15);
    bg_tasks[bg_task_count].func = func;
    bg_tasks[bg_task_count].active = 1;
    bg_task_count++;
}

void run_background_tasks(void) {
    if (bg_task_count == 0) return;
    bg_task_cur = (bg_task_cur + 1) % bg_task_count;
    if (bg_tasks[bg_task_cur].active)
        bg_tasks[bg_task_cur].func();
}

void init_background_tasks(void) {
    register_background_task("blink", task_blink);
    register_background_task("uptime", task_uptime);
    register_background_task("reap", reap_zombies);   // frees exited spawned procs
}

// --- Multitasking self-test (the `mtdemo` shell command) --------------------
// Two kernel threads that spin forever, each bumping its own counter and emitting
// a ~1 Hz serial heartbeat. Turning them on proves the preemptive scheduler
// time-slices them alongside the GUI compositor (which runs as the main "init"
// thread). Safe by construction: each thread touches only its own counter and the
// serial port, never GUI or shared kernel state.
volatile uint64_t mtdemo_a_count = 0;
volatile uint64_t mtdemo_b_count = 0;
static int mtdemo_started = 0;
static process_t* mtdemo_a_proc = NULL;
static process_t* mtdemo_b_proc = NULL;

// Each demo thread spins for MTDEMO_BEATS ~1 Hz heartbeats, then retires itself by
// setting state=0 so the scheduler stops picking it — this hands the CPU back to
// the compositor and the desktop returns to full speed. A retired thread never
// runs past the hlt loop (the scheduler skips state!=1), so it stays parked.
#define MTDEMO_BEATS 3

static void mtdemo_thread_a(void) {
    uint32_t last = tick_count; int beats = 0;
    for (;;) {
        mtdemo_a_count++;
        uint32_t t = tick_count;
        if (t - last >= 1000) {
            last = t;
            serial_puts("[mtdemo] thread A alive\n");
            if (++beats >= MTDEMO_BEATS) break;
        }
    }
    if (mtdemo_a_proc) mtdemo_a_proc->state = 0;   // retire
    for (;;) __asm__ volatile("hlt");
}

static void mtdemo_thread_b(void) {
    uint32_t last = tick_count; int beats = 0;
    for (;;) {
        mtdemo_b_count++;
        uint32_t t = tick_count;
        if (t - last >= 1000) {
            last = t;
            serial_puts("[mtdemo] thread B alive\n");
            if (++beats >= MTDEMO_BEATS) break;
        }
    }
    if (mtdemo_b_proc) mtdemo_b_proc->state = 0;   // retire
    for (;;) __asm__ volatile("hlt");
}

// Create the two demo threads and switch preemption on. Single-run per boot: the
// threads retire themselves after their heartbeats and park, so there's nothing to
// meaningfully restart. Returns 0 on a fresh start, 1 if already run this session,
// -1 if the threads couldn't be created.
int mtdemo_start(void) {
    if (mtdemo_started) return 1;
    mtdemo_a_proc = create_process("mtdemoA", (void*)mtdemo_thread_a, 0);
    mtdemo_b_proc = create_process("mtdemoB", (void*)mtdemo_thread_b, 0);
    if (!mtdemo_a_proc || !mtdemo_b_proc) return -1;
    mtdemo_started = 1;
    sched_enable();
    return 0;
}
