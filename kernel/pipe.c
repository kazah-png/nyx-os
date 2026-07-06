#include "kernel.h"

// ============================================================
// Anonymous pipes — a unidirectional byte stream between processes.
// Each pipe is a fixed ring buffer with a read end and a write end, each
// reference-counted by the number of fds pointing at it (fork() bumps them,
// close()/exit drop them). A read on an empty pipe BLOCKS until a writer writes
// (or all writers close → EOF); a writer wakes a blocked reader. Blocking is safe
// mid-syscall thanks to per-process syscall stacks (v5.8.2): see pipe_read.
// ============================================================

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES     16

typedef struct {
    int used;
    uint8_t buf[PIPE_BUF_SIZE];
    uint32_t head;               // read cursor
    uint32_t count;              // bytes currently buffered
    int read_refs;               // # of fds on the read end
    int write_refs;              // # of fds on the write end
    process_t* reader_waiter;    // a process parked in pipe_read (woken by a writer/close)
} pipe_t;

static pipe_t pipes[MAX_PIPES];

// Allocate a fresh pipe (one read ref + one write ref). Returns its id or -1.
int pipe_new(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].used) {
            memset_asm(&pipes[i], 0, sizeof(pipe_t));
            pipes[i].used = 1;
            pipes[i].read_refs = 1;
            pipes[i].write_refs = 1;
            return i;
        }
    }
    return -1;
}

// Add a reference to one end of a pipe (fork() inheriting a pipe fd).
void pipe_incref(int id, int is_write) {
    if (id < 0 || id >= MAX_PIPES || !pipes[id].used) return;
    if (is_write) pipes[id].write_refs++;
    else          pipes[id].read_refs++;
}

// Wake the reader parked on this pipe, if any (data arrived, or EOF on last-writer
// close). Just marks it runnable; the scheduler resumes it on the kernel CR3
// (blocked_in_kernel), and it re-checks the buffer.
static void pipe_wake_reader(pipe_t* p) {
    if (p->reader_waiter) {
        p->reader_waiter->state = PROC_RUN;
        p->reader_waiter = NULL;
    }
}

// Drop one reference from an end. Closing the last writer wakes a blocked reader
// so it observes EOF; when both ends reach zero the pipe is freed.
void pipe_close_end(int id, int is_write) {
    if (id < 0 || id >= MAX_PIPES || !pipes[id].used) return;
    pipe_t* p = &pipes[id];
    if (is_write) {
        if (p->write_refs > 0) p->write_refs--;
        if (p->write_refs == 0) pipe_wake_reader(p);
    } else {
        if (p->read_refs > 0) p->read_refs--;
    }
    if (p->read_refs == 0 && p->write_refs == 0) p->used = 0;
}

// Non-blocking write: copy as much of src (up to n) as fits, wake a blocked
// reader. Returns bytes written, or -1 if the read end is gone (broken pipe).
// Runs in a syscall (interrupts masked), so it's atomic against readers.
int pipe_write(int id, const char* src, int n) {
    if (id < 0 || id >= MAX_PIPES || !pipes[id].used || n < 0) return -1;
    pipe_t* p = &pipes[id];
    if (p->read_refs == 0) return -1;                 // EPIPE: nobody will read
    int written = 0;
    while (written < n && p->count < PIPE_BUF_SIZE) {
        uint32_t tail = (p->head + p->count) % PIPE_BUF_SIZE;
        p->buf[tail] = (uint8_t)src[written++];
        p->count++;
    }
    if (written > 0) pipe_wake_reader(p);
    return written;
}

// Blocking read into kbuf (up to n). Blocks while the pipe is empty and at least
// one writer is still open; returns 0 (EOF) once it's empty with no writers, or
// the number of bytes drained. Mirrors do_waitpid's mid-syscall blocking rules:
//  - blocked_in_kernel makes the scheduler resume us on the KERNEL CR3 (we sleep
//    in ring 0, whose -mcmodel=large code is only mapped there);
//  - other processes' syscalls clobber the shared user_cr3/user_rsp globals while
//    we sleep, so we save them on entry and restore them before returning so the
//    caller's copy_to_user + the asm return path target this process.
int pipe_read(int id, char* kbuf, int n) {
    extern uint64_t user_cr3, user_rsp;
    if (id < 0 || id >= MAX_PIPES || !pipes[id].used || n <= 0) return -1;
    pipe_t* p = &pipes[id];
    process_t* self = get_current_process();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    int got = 0;
    for (;;) {
        __asm__ volatile("cli");
        if (self) self->blocked_in_kernel = 0;        // past any block (interrupts off)
        if (p->count > 0) {                           // data available — drain up to n
            while (got < n && p->count > 0) {
                kbuf[got++] = (char)p->buf[p->head];
                p->head = (p->head + 1) % PIPE_BUF_SIZE;
                p->count--;
            }
            break;
        }
        if (p->write_refs == 0) { got = 0; break; }   // empty + no writers => EOF
        if (!self) break;                             // no process context: don't block
        // Empty but writers remain — park until one writes (or closes).
        self->blocked_in_kernel = 1;                  // resume us on the kernel CR3
        self->state = PROC_BLOCKED;
        p->reader_waiter = self;
        __asm__ volatile("sti; hlt");
        // Resumed on the kernel CR3 — loop, clear the flag, re-check (interrupts off).
    }
    user_cr3 = saved_cr3;
    user_rsp = saved_ursp;
    return got;
}
