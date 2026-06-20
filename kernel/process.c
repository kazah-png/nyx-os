#include "kernel.h"

extern process_t* process_table[MAX_PROCESSES];
extern int process_count;
static uint32_t next_pid = 1;

void init_process(void) {
    memset_asm(process_table, 0, sizeof(process_table));
    process_t* init = (process_t*)kmalloc(sizeof(process_t));
    if (init) {
        memset_asm(init, 0, sizeof(process_t));
        init->pid = next_pid++;
        init->ppid = 0;
        init->state = 1;
        strncpy(init->comm, "init", 31);
        process_table[process_count++] = init;
    }
}

process_t* create_process(const char* name, void* entry, uint32_t flags) {
    (void)entry;
    if (process_count >= MAX_PROCESSES) return NULL;
    process_t* p = (process_t*)kmalloc(sizeof(process_t));
    if (!p) return NULL;
    memset_asm(p, 0, sizeof(process_t));
    p->pid = next_pid++;
    p->ppid = 0;
    p->state = 1;
    p->stealth_level = (flags & 0x1) ? 5 : 0;
    strncpy(p->comm, name, 31);
    p->comm[31] = '\0';
    process_table[process_count++] = p;
    return p;
}

void destroy_process(uint32_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            kfree(process_table[i]);
            process_table[i] = NULL;
            return;
        }
    }
}

process_t* find_process(uint32_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i] && process_table[i]->pid == pid)
            return process_table[i];
    }
    return NULL;
}

process_t* get_current_process(void) {
    if (process_count > 0) return process_table[0];
    return NULL;
}

void hide_process(uint32_t pid) {
    process_t* p = find_process(pid);
    if (p) p->stealth_level = 5;
}

void unhide_process(uint32_t pid) {
    process_t* p = find_process(pid);
    if (p) p->stealth_level = 0;
}

void schedule(void) {
    update_timer();
}

// Background task: blink on serial port
static void task_blink(void) {
    outb(0x3F8, '.');
}

// Background task: print uptime every ~5 seconds
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
}


