#include "libc.h"

/* ps — report the live processes. Enumeration lives entirely in the kernel: one
 * getprocs() call snapshots the whole process table into an array of
 * nyx_procinfo_t (the process analogue of ls's getdents()).
 *
 * The record buffer is a .bss static, not malloc()'d: with the lazy-sbrk heap a
 * fresh malloc only faults in the page holding the block header, and the
 * kernel's copy_to_user cannot fault the rest in. A .bss array is resident from
 * load, so every page the kernel writes is already backed. */

#define MAX_PROCS 128

static nyx_procinfo_t procs[MAX_PROCS];

/* PROC_* states (kernel.h): 0 parked, 1 run, 2 zombie, 3 blocked. */
static char state_char(unsigned int st) {
    switch (st) {
        case 0: return 'P';   /* parked (retired kernel thread) */
        case 1: return 'R';   /* runnable / running            */
        case 2: return 'Z';   /* zombie (exited, awaiting reap) */
        case 3: return 'S';   /* sleeping / blocked            */
        default: return '?';
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    long n = getprocs(procs, MAX_PROCS);
    if (n < 0) { printf("ps: cannot read process table\n"); return 1; }

    /* Right-justified numeric columns; CMD last so it needs no left-justify
     * (which the user printf lacks). Header widths match the data rows. */
    printf("%5s %5s %s %6s %s\n", "PID", "PPID", "S", "TIME", "CMD");
    for (long i = 0; i < n; i++) {
        printf("%5u %5u %c %6u %s\n",
               procs[i].pid, procs[i].ppid,
               state_char(procs[i].state), procs[i].cpu_time,
               procs[i].comm);
    }
    return 0;
}
