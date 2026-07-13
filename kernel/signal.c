#include "kernel.h"

/* ============================================================
 * signal.c — POSIX-style signals for ring-3 processes (v5.8.11)
 * ============================================================
 * Model: a signal is POSTED (do_kill / keyboard Ctrl-C) by setting a bit in the
 * target's `sig_pending`, and DELIVERED at return-to-ring-3 by `signal_dispatch`,
 * which the syscall entry path (isr_stubs.asm) calls with the process's saved
 * register frame. Delivery either runs the default action (terminate, or ignore
 * for SIGCHLD/SIGCONT), drops the signal (SIG_IGN), or diverts the process into a
 * user handler by rewriting the frame (RIP=handler, RDI=signo) and pushing a
 * return trampoline onto the user stack; the handler returns through that
 * trampoline into SYS_SIGRETURN, which restores the interrupted context.
 *
 * Because delivery happens on the syscall return path, a blocked syscall must be
 * signal-aware: do_kill wakes a PROC_BLOCKED target so its wait returns (EINTR)
 * and delivery runs. Pure compute loops that never syscall won't see a signal
 * until their next timer-driven syscall return — IRQ-path delivery is future work. */

/* get_current_process / find_process / wake_waiters are declared in kernel.h. */

/* The foreground process for terminal-generated signals (Ctrl-C). cmd_exec sets
 * this to the pid it kwait()s on; 0 = none. */
uint32_t g_foreground_pid = 0;

/* SIGKILL and SIGSTOP can never be caught or ignored. */
static int uncatchable(int sig) { return sig == SIGKILL || sig == SIGSTOP; }

/* Signals whose SIG_DFL action is "ignore" instead of "terminate". */
static int default_is_ignore(int sig) { return sig == SIGCHLD || sig == SIGCONT; }

/* Post `sig` to process p: set the pending bit, and if p is parked in a blocking
 * syscall (waitpid / pipe / stdin), wake it so the syscall returns and delivery
 * runs on the way out. Safe from IRQ context (just field writes). */
void signal_raise(process_t* p, int sig) {
    if (!p || sig <= 0 || sig >= NSIG) return;
    /* SIGCONT resumes a job-control-stopped process: flip it back to RUN so the
     * scheduler runs it again (it's parked in signal_dispatch waiting for exactly
     * this). SIGCONT's default action is "ignore", so no handler runs on delivery. */
    if (sig == SIGCONT && p->state == PROC_STOPPED) {
        p->stop_sig = 0;
        p->state = PROC_RUN;
    }
    p->sig_pending |= (1u << sig);
    if (p->state == PROC_BLOCKED) {          /* unblock so its wait returns EINTR */
        p->waiting_for = 0;
        p->wake_tick = 0;
        p->state = PROC_RUN;
    }
}

/* 1 if p has a pending, unblocked signal ready to deliver. Used by the blocking
 * syscalls to decide whether to bail out with EINTR. */
int signal_pending(process_t* p) {
    return p && (p->sig_pending & ~p->sig_mask) != 0;
}

/* If a job-control STOP signal (SIGTSTP / SIGSTOP) is pending, PARK the process here
 * until SIGCONT resumes it, and return 1 — so the interrupted blocking syscall can
 * RESTART its wait rather than returning -EINTR (stop/continue is then transparent to
 * the operation, e.g. a `sleep` keeps sleeping after fg/bg). Returns 0 if no stop is
 * pending. Called with interrupts off from a blocking syscall's wait loop; the caller
 * already saves/restores user_cr3/user_rsp around that loop. Parks like do_waitpid
 * (blocked_in_kernel -> resume on the kernel CR3, sti/hlt until PROC_RUN). */
int signal_check_stop(process_t* p) {
    if (!p) return 0;
    uint32_t stops = p->sig_pending & ((1u << SIGTSTP) | (1u << SIGSTOP));
    if (!stops) return 0;
    int sig = (stops & (1u << SIGTSTP)) ? SIGTSTP : SIGSTOP;
    p->sig_pending &= ~(1u << sig);
    p->stop_sig = (uint32_t)sig;
    p->stopped_reported = 0;
    p->state = PROC_STOPPED;
    wake_waiters(p);                         /* parent's waitpid(WUNTRACED) reports the stop */
    p->blocked_in_kernel = 1;
    while (p->state == PROC_STOPPED) __asm__ volatile("sti; hlt");
    __asm__ volatile("cli");
    p->blocked_in_kernel = 0;
    p->sig_pending &= ~(1u << SIGCONT);      /* consume the SIGCONT that resumed us (default: ignore),
                                              * else the caller's next signal_pending() bails -EINTR */
    return 1;                                /* resumed (SIGCONT): caller restarts its wait */
}

/* SYS_KILL: post `sig` to `pid`. sig 0 is the existence probe (returns 0 if the
 * process exists, -1 otherwise) and sends nothing. */
int do_kill(int pid, int sig) {
    if (sig < 0 || sig >= NSIG) return -1;
    process_t* target = find_process((uint32_t)pid);
    if (!target) return -1;                  /* ESRCH */
    if (sig == 0) return 0;                   /* existence check only */
    signal_raise(target, sig);
    return 0;
}

/* SYS_SIGNAL: set the disposition of `sig` (SIG_DFL / SIG_IGN / a handler VA) and
 * record the ring-3 sigreturn trampoline. Returns the previous disposition, or -1
 * for an invalid or uncatchable signal. */
long do_signal(int sig, uint64_t handler, uint64_t trampoline) {
    if (sig <= 0 || sig >= NSIG || uncatchable(sig)) return -1;
    process_t* p = get_current_process();
    if (!p) return -1;
    long prev = (long)p->sig_handlers[sig];
    p->sig_handlers[sig] = handler;
    if (trampoline) p->sig_trampoline = trampoline;
    if (handler == SIG_IGN) p->sig_pending &= ~(1u << sig);   /* discard pending */
    return prev;
}

/* Terminate the current process from an uncaught default-terminate signal. Mirrors
 * SYS_EXIT: become a zombie, wake a parent blocked in waitpid, then yield forever
 * (the scheduler switches away and reap_zombies frees us — we can't free our own
 * stack). exit_code uses the shell convention 128+signo so waitpid can report that
 * the child was killed by a signal. Never returns. */
static void signal_terminate(process_t* p, int sig) {
    p->exit_code = 128 + sig;
    p->state = PROC_ZOMBIE;
    wake_waiters(p);
    __asm__ volatile("sti");
    for (;;) __asm__ volatile("hlt");
}

/* Deliver at most one pending, unblocked signal to the process about to return to
 * ring 3. `frame` is its saved register frame on the kernel stack (isr_stubs.asm
 * SAVE_REGS order): frame[0..14] = GPRs r15..rax, frame[15] = RFLAGS, frame[16] =
 * RIP; the user RSP lives in the `user_rsp` global. Called on the kernel CR3 with
 * user_cr3 still pointing at the caller's address space (copy_to_user works). */
void signal_dispatch(uint64_t* frame) {
    extern uint64_t user_rsp;
    process_t* p = get_current_process();
    if (!p || !p->page_directory) return;

    uint32_t deliverable = p->sig_pending & ~p->sig_mask;
    if (!deliverable) return;

    int sig = 0;                              /* lowest-numbered pending signal */
    for (int s = 1; s < NSIG; s++)
        if (deliverable & (1u << s)) { sig = s; break; }
    if (!sig) return;

    uint64_t disp = uncatchable(sig) ? SIG_DFL : p->sig_handlers[sig];

    if (disp == SIG_IGN) { p->sig_pending &= ~(1u << sig); return; }
    if (disp == SIG_DFL) {
        p->sig_pending &= ~(1u << sig);
        if (default_is_ignore(sig)) return;
        /* Job-control stop (SIGTSTP/SIGSTOP): PARK here instead of terminating, so the
         * process never returns to ring 3 while stopped. We mark PROC_STOPPED, wake a
         * parent blocked in waitpid(WUNTRACED) to report the stop, then sti/hlt until
         * SIGCONT flips us back to PROC_RUN (the scheduler skips non-RUN states). This
         * is the do_waitpid mid-syscall discipline: blocked_in_kernel resumes us on the
         * kernel CR3, and user_cr3/user_rsp are saved/restored across the park because
         * other processes' syscalls clobber those globals while we sleep. */
        if (sig == SIGTSTP || sig == SIGSTOP) {
            extern uint64_t user_cr3;
            uint64_t s_cr3 = user_cr3, s_rsp = user_rsp;
            p->stop_sig = (uint32_t)sig;
            p->stopped_reported = 0;
            p->state = PROC_STOPPED;
            wake_waiters(p);                 /* parent's WUNTRACED waitpid reports the stop */
            p->blocked_in_kernel = 1;        /* resume on the kernel CR3 after SIGCONT */
            while (p->state == PROC_STOPPED) __asm__ volatile("sti; hlt");
            __asm__ volatile("cli");
            p->blocked_in_kernel = 0;
            p->sig_pending &= ~(1u << SIGCONT);  /* consume the SIGCONT that resumed us */
            user_cr3 = s_cr3;                /* restore OUR globals for the asm return path */
            user_rsp = s_rsp;
            return;                          /* resumed (SIGCONT): finish the syscall return */
        }
        signal_terminate(p, sig);            /* never returns */
        return;
    }

    /* User handler. Serialize handlers: a single saved-context slot means we can't
     * nest, so if one is already running, leave this signal pending — it delivers
     * after the current handler's sigreturn (which clears sig_active). */
    if (p->sig_active) return;
    p->sig_pending &= ~(1u << sig);          /* commit to delivering it */

    /* Save the interrupted ring-3 context for SYS_SIGRETURN. */
    for (int i = 0; i < 15; i++) p->sig_saved[i] = frame[i];   /* GPRs r15..rax */
    p->sig_saved[15] = frame[15];            /* RFLAGS */
    p->sig_saved[16] = frame[16];            /* RIP    */
    p->sig_saved[17] = user_rsp;             /* user RSP */

    /* Build the handler's ring-3 stack: a small slack gap below RSP (user code is
     * -mno-red-zone, but stay clear anyway), 16-byte aligned, then push the
     * trampoline return address so RSP%16 == 8 at handler entry (SysV: as if
     * reached by a `call`). The handler `ret`s into the trampoline -> SYS_SIGRETURN. */
    uint64_t sp = user_rsp;
    sp -= 128;
    sp &= ~15ULL;
    sp -= 8;
    uint64_t tramp = p->sig_trampoline;
    if (copy_to_user(sp, &tramp, 8) != 0) {  /* bad user stack -> can't deliver */
        signal_terminate(p, sig);            /* never returns */
        return;
    }

    frame[9]  = (uint64_t)sig;               /* RDI = signo (SysV arg1) */
    frame[16] = disp;                        /* RIP = handler entry */
    user_rsp  = sp;                          /* handler runs on the adjusted stack */
    p->sig_active |= (1u << sig);
    p->sig_mask   |= (1u << sig);            /* block this signal inside its handler */
}

/* SYS_SIGRETURN: the handler returned through the trampoline. Restore the context
 * saved by signal_dispatch into THIS syscall's frame + user_rsp and unblock the
 * handled signal, so the syscall return path iretq's back to where we interrupted. */
void do_sigreturn(void) {
    extern uint64_t syscall_frame_ptr, user_rsp;
    process_t* p = get_current_process();
    if (!p || !p->sig_active) return;        /* ignore a spurious sigreturn */
    uint64_t* frame = (uint64_t*)syscall_frame_ptr;
    if (!frame) return;
    for (int i = 0; i < 15; i++) frame[i] = p->sig_saved[i];   /* GPRs */
    frame[15] = p->sig_saved[15];            /* RFLAGS */
    frame[16] = p->sig_saved[16];            /* RIP    */
    user_rsp  = p->sig_saved[17];            /* user RSP */
    p->sig_mask  &= ~p->sig_active;          /* unblock the handled signal */
    p->sig_active = 0;
}

/* Keyboard Ctrl-C -> post SIGINT to the foreground process. */
void signal_send_foreground(int sig) {
    if (!g_foreground_pid) return;
    process_t* p = find_process(g_foreground_pid);
    if (p) signal_raise(p, sig);
}
