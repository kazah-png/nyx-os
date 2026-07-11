#include "libc.h"

/* kill — send a signal to a process. Usage:
 *     kill pid...            (default SIGTERM)
 *     kill -9 pid...         (numeric signal)
 *     kill -KILL pid...      (name, with or without the SIG prefix)
 * The signal itself is delivered by the kernel when the target next returns to
 * ring 3 (see SYS_KILL / signal_dispatch). This is just an argv front-end over
 * the kill() syscall wrapper. */

static int name_to_sig(const char* s) {
    if (s[0] >= '0' && s[0] <= '9') return atoi(s);   /* numeric: -9 */
    if (!strncmp(s, "SIG", 3)) s += 3;                 /* allow SIGKILL */
    struct { const char* n; int s; } tbl[] = {
        {"HUP", SIGHUP},   {"INT",  SIGINT},  {"QUIT", SIGQUIT},
        {"KILL", SIGKILL}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2},
        {"TERM", SIGTERM}, {"ALRM", SIGALRM}, {"CONT", SIGCONT},
        {"STOP", SIGSTOP},
    };
    for (unsigned i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (!strcmp(s, tbl[i].n)) return tbl[i].s;
    return -1;
}

int main(int argc, char** argv) {
    int sig = SIGTERM;
    int i = 1;

    if (i < argc && argv[i][0] == '-') {
        sig = name_to_sig(argv[i] + 1);
        if (sig < 0) { printf("kill: invalid signal '%s'\n", argv[i]); return 1; }
        i++;
    }
    if (i >= argc) { printf("usage: kill [-SIG] pid...\n"); return 1; }

    int rc = 0;
    for (; i < argc; i++) {
        int pid = atoi(argv[i]);
        if (kill(pid, sig) != 0) {
            printf("kill: (%d) - no such process\n", pid);
            rc = 1;
        }
    }
    return rc;
}
