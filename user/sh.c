#include "libc.h"

/* sh — the NyxOS userspace shell. Runs entirely in ring 3 on the Unix process
 * toolkit the kernel grew in v5.7.25..v5.8.6: fork() + execve(argv) + waitpid()
 * + pipe() + dup2(). A command line is up to MAX_STAGES programs joined by '|';
 * each stage runs as a fork+execve'd child with its stdin/stdout wired to the
 * neighbouring pipes, and the shell waitpid()s them all.
 *
 * Modes: `sh -c "line"` runs one line; with no arguments it is INTERACTIVE —
 * read(0, …) is a blocking, echoing line read from the keyboard (kernel
 * canonical mode, v5.8.8), so you type commands at a live `sh$ ` prompt.
 * Builtins: `exit` leaves the shell, `demo` runs the canned script. */

#define MAX_STAGES 4
#define MAX_ARGS   8

/* Trim leading/trailing spaces in place. */
static char* trim(char* s) {
    while (*s == ' ') s++;
    char* e = s + strlen(s);
    while (e > s && e[-1] == ' ') *--e = '\0';
    return s;
}

/* Split a stage ("prog arg1 arg2") into an argv[]; modifies s. Returns argc. */
static int split_args(char* s, char** av, int max) {
    int n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ') *s++ = '\0';
        if (!*s) break;
        av[n++] = s;
        while (*s && *s != ' ') s++;
    }
    av[n] = 0;
    return n;
}

/* Resolve a command name: "echo" -> "/echo.elf"; anything already containing a
 * '/' or '.' is taken as an explicit path. */
static void resolve(const char* name, char* out) {
    if (strchr(name, '/') || strchr(name, '.')) { strcpy(out, name); return; }
    out[0] = '/';
    strcpy(out + 1, name);
    strcat(out, ".elf");
}

/* Background jobs. A pipeline ending in '&' is not waited on; its pids are parked
 * here and reaped without blocking at each prompt via waitpid3(.., WNOHANG). The
 * shell must reap its own background children: reap_zombies() in the kernel only
 * frees procs whose parent has already gone, so an alive shell owns its zombies. */
#define MAX_BG 16
static long bg_pids[MAX_BG];
static int  bg_count = 0;

static void bg_add(long pid) {
    if (pid > 0 && bg_count < MAX_BG) bg_pids[bg_count++] = pid;
}

/* Non-blocking sweep of finished background jobs. Called before each prompt. */
static void reap_bg(void) {
    for (int i = 0; i < bg_count; ) {
        int st = 0;
        long r = waitpid3((int)bg_pids[i], &st, WNOHANG);
        if (r == bg_pids[i]) {                      /* exited: report and drop */
            printf("[done] pid %ld (status %d)\n", bg_pids[i], st);
            bg_pids[i] = bg_pids[--bg_count];       /* swap-remove */
        } else if (r < 0) {                         /* already gone: drop quietly */
            bg_pids[i] = bg_pids[--bg_count];
        } else {
            i++;                                    /* r == 0: still running */
        }
    }
}

/* Run one command line (destroys it). Stages split on '|', each fork+execve'd
 * with pipes wired via dup2. A trailing '&' backgrounds the whole pipeline (the
 * shell parks the pids and returns immediately); otherwise it reaps every stage
 * with waitpid. */
static void run_line(char* line) {
    /* Trailing '&' (ignoring spaces) -> background the pipeline. */
    int background = 0;
    {
        char* e = line + strlen(line);
        while (e > line && e[-1] == ' ') e--;
        if (e > line && e[-1] == '&') { background = 1; *--e = '\0'; }
    }

    char* stages[MAX_STAGES];
    int nstages = 0;
    char* p = line;
    stages[nstages++] = p;
    while ((p = strchr(p, '|')) != 0 && nstages < MAX_STAGES) {
        *p++ = '\0';
        stages[nstages++] = p;
    }

    long pids[MAX_STAGES];
    int prev_rd = -1;                          /* read end of the previous pipe */
    for (int s = 0; s < nstages; s++) {
        int has_next = (s < nstages - 1);
        int pfd[2] = { -1, -1 };
        if (has_next && pipe(pfd) != 0) { printf("sh: pipe failed\n"); return; }

        long pid = fork();
        if (pid == 0) {
            /* Child: stdin from the previous pipe, stdout into the next one,
             * then drop the raw fds (the dup took a reference) and become the
             * program. If execve returns, the program wasn't found. */
            if (prev_rd >= 0) { dup2(prev_rd, 0); close(prev_rd); }
            if (has_next)     { close(pfd[0]); dup2(pfd[1], 1); close(pfd[1]); }
            char* av[MAX_ARGS];
            int ac = split_args(trim(stages[s]), av, MAX_ARGS);
            if (ac == 0) exit(0);
            char path[64];
            resolve(av[0], path);
            execve(path, av, 0);
            printf("sh: %s: not found\n", path);
            exit(127);
        } else if (pid < 0) {
            printf("sh: fork failed\n");
            return;
        }
        pids[s] = pid;
        /* Parent: close the ends now owned by the children, keep the new pipe's
         * read end for the next stage. Closing promptly is what makes EOF fire
         * once the last writer exits. */
        if (prev_rd >= 0) close(prev_rd);
        if (has_next) { close(pfd[1]); prev_rd = pfd[0]; }
    }

    if (background) {                        /* don't wait — park the pids and go */
        for (int s = 0; s < nstages; s++) bg_add(pids[s]);
        printf("[bg] pid %ld\n", pids[nstages - 1]);
        return;
    }

    for (int s = 0; s < nstages; s++) {
        int st = 0;
        waitpid((int)pids[s], &st);
        if (st != 0)
            printf("sh: [pid %d] exited with status %d\n", (int)pids[s], st);
    }
}

/* The canned demo — one plain command, one two-stage pipeline, and one
 * argv+status check. Every line exercises fork/execve/waitpid; the pipeline
 * adds pipe+dup2. Run via the `demo` builtin. */
static void run_demo(void) {
    static const char* script[] = {
        "echo hello from the NyxOS userspace shell",
        "echo pipelines are working on nyxos | upper",
        "ls /",
        "cat /home/user/welcome.txt",
        "cat /home/user/welcome.txt | wc",
        "echo running in the background | upper &",
        "args one two three",
    };
    for (unsigned i = 0; i < sizeof(script) / sizeof(script[0]); i++) {
        char buf[128];
        strncpy(buf, script[i], sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        printf("sh$ %s\n", script[i]);
        run_line(buf);
    }
    printf("sh: demo done.\n");
}

int main(int argc, char** argv) {
    /* sh -c "one command line" */
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        char buf[128];
        strncpy(buf, argv[2], sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        run_line(buf);
        return 0;
    }

    /* Interactive REPL: read(0) blocks in the kernel's canonical line
     * discipline (echo + backspace handled there), so this is a live shell. */
    printf("NyxOS sh v0.3 — interactive; try 'demo', 'ls /', 'cat FILE | wc', 'CMD &', 'exit'\n");
    for (;;) {
        reap_bg();                       /* report finished background jobs */
        write(1, "sh$ ", 4);
        char line[128];
        long n = read(0, line, sizeof(line) - 1);
        if (n <= 0) {                    /* no stdin (EOF/error) — leave */
            printf("sh: no stdin, bye\n");
            break;
        }
        if (line[n - 1] == '\n') n--;    /* strip the newline */
        line[n] = '\0';
        char* t = trim(line);
        if (!*t) continue;
        if (strcmp(t, "exit") == 0) break;
        if (strcmp(t, "demo") == 0) { run_demo(); continue; }
        run_line(t);
    }
    printf("sh: bye\n");
    return 0;
}
