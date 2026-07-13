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
#define MAX_ARGS   12

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

/* Pull I/O redirections out of an argv (in place): the tokens `<`, `>`, `>>`
 * (each followed by a filename) and their attached forms `<f` / `>f` / `>>f`.
 * Sets *infile / *outfile / *append and compacts argv to the command words only. */
static void parse_redir(char** av, int* pac, char** infile, char** outfile, int* append) {
    *infile = 0; *outfile = 0; *append = 0;
    int n = *pac, w = 0;
    for (int i = 0; i < n; i++) {
        char* t = av[i];
        if (t[0] == '<') {
            *infile = t[1] ? t + 1 : (i + 1 < n ? av[++i] : 0);
        } else if (t[0] == '>' && t[1] == '>') {
            *append = 1;
            *outfile = t[2] ? t + 2 : (i + 1 < n ? av[++i] : 0);
        } else if (t[0] == '>') {
            *append = 0;
            *outfile = t[1] ? t + 1 : (i + 1 < n ? av[++i] : 0);
        } else {
            av[w++] = t;
        }
    }
    av[w] = 0;
    *pac = w;
}

static const char* env_get(const char* name);   /* forward: defined with the env table below */

/* True if `path` names an openable file — used to probe $PATH candidates. */
static int file_exists(const char* path) {
    long fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    close((int)fd);
    return 1;
}

/* Resolve a command name to a runnable path. A name containing '/' is an explicit
 * path (absolute or cwd-relative) and used verbatim — never $PATH-searched. Otherwise
 * each ':'-separated directory in $PATH is tried in order, "<dir>/<name>" then
 * "<dir>/<name>.elf" (NyxOS executables carry the .elf suffix), and the first that
 * exists wins — so $PATH is authoritative (a command not on it is "not found",
 * signalled by an empty `out`). Only if $PATH is unset/empty does it fall back to the
 * legacy "/<name>.elf" so the shell still works with no environment. */
static void resolve(const char* name, char* out) {
    if (strchr(name, '/')) { strcpy(out, name); return; }   /* explicit path: no search */
    const char* path = env_get("PATH");
    if (path && *path) {
        for (const char* p = path; *p; ) {
            char dir[96]; int di = 0;
            while (*p && *p != ':' && di < (int)sizeof(dir) - 1) dir[di++] = *p++;
            dir[di] = '\0';
            while (*p == ':') p++;                           /* skip separator(s) */
            if (di == 0) continue;                           /* empty entry */
            const char* sep = (dir[di - 1] == '/') ? "" : "/";
            char cand[160];
            snprintf(cand, sizeof(cand), "%s%s%s", dir, sep, name);
            if (file_exists(cand)) { strcpy(out, cand); return; }
            snprintf(cand, sizeof(cand), "%s%s%s.elf", dir, sep, name);
            if (file_exists(cand)) { strcpy(out, cand); return; }
        }
        out[0] = '\0';                                       /* searched $PATH, not found */
        return;
    }
    out[0] = '/'; strcpy(out + 1, name); strcat(out, ".elf");   /* no $PATH: legacy fallback */
}

/* Shell-local environment (execve doesn't carry envp yet, so `$VAR` lives here) and
 * the last command's exit status (`$?`). */
#define MAX_ENV 16
static char env_name[MAX_ENV][32];
static char env_val[MAX_ENV][96];
static int  env_count;
static int  last_status;

static const char* env_get(const char* name) {
    for (int i = 0; i < env_count; i++)
        if (strcmp(env_name[i], name) == 0) return env_val[i];
    return 0;
}
static void env_set(const char* name, const char* val) {
    for (int i = 0; i < env_count; i++)
        if (strcmp(env_name[i], name) == 0) {
            strncpy(env_val[i], val, sizeof(env_val[i]) - 1);
            env_val[i][sizeof(env_val[i]) - 1] = '\0';
            return;
        }
    if (env_count >= MAX_ENV) return;
    strncpy(env_name[env_count], name, 31); env_name[env_count][31] = '\0';
    strncpy(env_val[env_count], val, 95);   env_val[env_count][95] = '\0';
    env_count++;
}

/* Flatten the shell env into a NULL-terminated "NAME=VALUE" vector for execve, so
 * children inherit it (and `env` can print it). Static storage: the caller is a
 * forked child about to execve, and the kernel copies the strings before the new
 * image is built. */
static char** env_build(void) {
    static char store[MAX_ENV][160];
    static char* envp[MAX_ENV + 1];
    int n = 0;
    for (int i = 0; i < env_count && n < MAX_ENV; i++) {
        snprintf(store[n], sizeof(store[n]), "%s=%s", env_name[i], env_val[i]);
        envp[n] = store[n];
        n++;
    }
    envp[n] = 0;
    return envp;
}

static int is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* Substitute `$NAME` (from the env) and `$?` (last exit status) in `in` -> `out`.
 * An unknown variable expands to nothing, like a real shell. */
static void expand_vars(const char* in, char* out, int outsz) {
    int o = 0;
    for (int i = 0; in[i] && o < outsz - 1; ) {
        if (in[i] == '$' && in[i + 1] == '?') {
            char num[16]; snprintf(num, sizeof(num), "%d", last_status);
            for (int k = 0; num[k] && o < outsz - 1; k++) out[o++] = num[k];
            i += 2;
        } else if (in[i] == '$' && is_name_char(in[i + 1])) {
            char name[32]; int ni = 0;
            i++;
            while (is_name_char(in[i]) && ni < 31) name[ni++] = in[i++];
            name[ni] = '\0';
            const char* v = env_get(name);
            if (v) for (int k = 0; v[k] && o < outsz - 1; k++) out[o++] = v[k];
        } else {
            out[o++] = in[i++];
        }
    }
    out[o] = '\0';
}

/* Builtins that must run IN the shell process (they change its cwd/env — a forked
 * child couldn't). cd/pwd use the chdir/getcwd syscalls; export updates the env. */
static void builtin_cd(char* arg) {
    const char* dir = (arg && *arg) ? arg : "/home/user";
    if (chdir(dir) != 0) printf("cd: %s: no such directory\n", dir);
}
static void builtin_pwd(void) {
    char buf[128];
    if (getcwd(buf, sizeof(buf)) >= 0) { write(1, buf, strlen(buf)); write(1, "\n", 1); }
}
static void builtin_export(char* rest) {
    char* eq = strchr(rest, '=');
    if (!eq) { printf("usage: export NAME=value\n"); return; }
    *eq = '\0';
    env_set(trim(rest), trim(eq + 1));
}

/* Job-control builtins (defined with the jobs table below). */
static void builtin_jobs(void);
static void builtin_fg(char* arg);
static void builtin_bg(char* arg);

/* Run a cd/pwd/export/jobs/fg/bg builtin if `line` is one; returns 1 if handled. */
static int try_builtin(char* line) {
    if (strcmp(line, "pwd") == 0) { builtin_pwd(); return 1; }
    if (strcmp(line, "jobs") == 0) { builtin_jobs(); return 1; }
    if (strncmp(line, "cd", 2) == 0 && (line[2] == ' ' || line[2] == '\0')) {
        builtin_cd(line[2] ? trim(line + 3) : "");
        return 1;
    }
    if (strncmp(line, "fg", 2) == 0 && (line[2] == ' ' || line[2] == '\0')) {
        builtin_fg(line[2] ? trim(line + 3) : ""); return 1;
    }
    if (strncmp(line, "bg", 2) == 0 && (line[2] == ' ' || line[2] == '\0')) {
        builtin_bg(line[2] ? trim(line + 3) : ""); return 1;
    }
    if (strncmp(line, "export ", 7) == 0) { builtin_export(line + 7); return 1; }
    return 0;
}

/* Job control. Each `&` background pipeline and each Ctrl-Z-stopped foreground job
 * gets a slot here; `jobs` lists them, `fg`/`bg` resume them. The shell reaps its own
 * children — the kernel's reap_zombies only frees procs whose parent has already gone,
 * so an alive shell owns its zombies. */
#define MAX_JOBS 16
typedef struct {
    long pid;          /* the job's (last-stage) pid; 0 = free slot */
    int  stopped;      /* 1 = stopped (Ctrl-Z), 0 = running in the background */
    char cmd[64];      /* the command line, for the `jobs` listing */
} job_t;
static job_t jobs[MAX_JOBS];

/* Record a job; returns its 1-based number, or -1 if the table is full. */
static int job_add(long pid, int stopped, const char* cmd) {
    if (pid <= 0) return -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid; jobs[i].stopped = stopped;
            strncpy(jobs[i].cmd, cmd, sizeof(jobs[i].cmd) - 1);
            jobs[i].cmd[sizeof(jobs[i].cmd) - 1] = '\0';
            return i + 1;
        }
    }
    return -1;
}

/* Look up a job by 1-based number, or the most recent one when n <= 0. NULL if none. */
static job_t* job_find(int n) {
    if (n > 0 && n <= MAX_JOBS && jobs[n - 1].pid) return &jobs[n - 1];
    if (n <= 0)
        for (int i = MAX_JOBS - 1; i >= 0; i--) if (jobs[i].pid) return &jobs[i];
    return 0;
}

/* Non-blocking sweep of finished background jobs. Called before each prompt. Stopped
 * jobs aren't running, so they're skipped (they only resume via fg/bg). A final
 * waitpid(-1) mop-up reaps any other finished children (non-leader pipeline stages). */
static void reap_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0 || jobs[i].stopped) continue;
        int st = 0;
        long r = waitpid3((int)jobs[i].pid, &st, WNOHANG);
        if (r == jobs[i].pid) { printf("[%d]  Done       %s\n", i + 1, jobs[i].cmd); jobs[i].pid = 0; }
        else if (r < 0)       { jobs[i].pid = 0; }
    }
    int st;
    while (waitpid3(-1, &st, WNOHANG) > 0) { }   /* mop up other finished children */
}

/* Parse a job spec: "%2"/"2" -> 2, empty -> 0 (most recent). */
static int job_num(const char* arg) {
    if (!arg || !*arg) return 0;
    if (arg[0] == '%') arg++;
    return atoi(arg);
}

static void builtin_jobs(void) {
    int any = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) continue;
        printf("[%d]  %s  %s\n", i + 1, jobs[i].stopped ? "Stopped" : "Running", jobs[i].cmd);
        any = 1;
    }
    if (!any) printf("jobs: no active jobs\n");
}

/* fg [%n] — resume job n (or the most recent) in the FOREGROUND: SIGCONT it if
 * stopped, claim the terminal, and wait (WUNTRACED, so another Ctrl-Z re-stops it). */
static void builtin_fg(char* arg) {
    job_t* j = job_find(job_num(arg));
    if (!j) { printf("fg: no such job\n"); return; }
    long pid = j->pid;
    printf("%s\n", j->cmd);                       /* real shells echo the resumed cmd */
    if (j->stopped) { kill((int)pid, SIGCONT); j->stopped = 0; }
    setfg(pid);
    int st = 0;
    waitpid3((int)pid, &st, WUNTRACED);
    setfg((int)getpid());
    if (WIFSTOPPED(st)) { j->stopped = 1; printf("\nStopped    %s\n", j->cmd); }
    else { last_status = st; j->pid = 0; }        /* exited -> free the slot */
}

/* bg [%n] — resume a stopped job in the BACKGROUND (SIGCONT, keep running). */
static void builtin_bg(char* arg) {
    job_t* j = job_find(job_num(arg));
    if (!j) { printf("bg: no such job\n"); return; }
    if (!j->stopped) { printf("bg: job already running\n"); return; }
    kill((int)j->pid, SIGCONT);
    j->stopped = 0;
    printf("[bg] %s &\n", j->cmd);
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

    char jobcmd[64];                             /* snapshot the command for the jobs list */
    strncpy(jobcmd, line, sizeof(jobcmd) - 1);
    jobcmd[sizeof(jobcmd) - 1] = '\0';

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
            char *infile, *outfile; int append;
            parse_redir(av, &ac, &infile, &outfile, &append);
            if (ac == 0) exit(0);
            /* File redirections override the pipe wiring for the affected fd. */
            if (infile) {
                long fd = open(infile, O_RDONLY, 0);
                if (fd < 0) { printf("sh: %s: cannot open\n", infile); exit(1); }
                dup2((int)fd, 0); close((int)fd);
            }
            if (outfile) {
                long fd = open(outfile, O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
                if (fd < 0) { printf("sh: %s: cannot create\n", outfile); exit(1); }
                dup2((int)fd, 1); close((int)fd);
            }
            char path[160];
            resolve(av[0], path);
            if (!path[0]) { printf("sh: %s: not found\n", av[0]); exit(127); }  /* not on $PATH */
            execve(path, av, env_build());   /* child inherits the shell environment */
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

    if (background) {                        /* don't wait — record a running job */
        int jn = job_add(pids[nstages - 1], 0, jobcmd);
        printf("[%d]  %ld  %s\n", jn, pids[nstages - 1], jobcmd);
        return;
    }

    /* Foreground: claim the terminal for the job (so Ctrl-C / Ctrl-Z reach IT, not the
     * shell), wait with WUNTRACED so a Ctrl-Z stop parks the job instead of killing it,
     * then reclaim the terminal for the shell. */
    setfg(pids[nstages - 1]);
    for (int s = 0; s < nstages; s++) {
        int st = 0;
        waitpid3((int)pids[s], &st, WUNTRACED);
        if (WIFSTOPPED(st)) {               /* Ctrl-Z: park it as a stopped job */
            int jn = job_add(pids[s], 1, jobcmd);
            printf("\n[%d]  Stopped    %s\n", jn, jobcmd);
            break;                          /* leave any other pipeline stages running */
        }
        last_status = st;                   /* pipeline status = last stage ($?) */
        if (st != 0)
            printf("sh: [pid %d] exited with status %d\n", (int)pids[s], st);
    }
    setfg((int)getpid());                   /* shell reclaims the terminal */
}

/* The canned demo — one plain command, one two-stage pipeline, and one
 * argv+status check. Every line exercises fork/execve/waitpid; the pipeline
 * adds pipe+dup2. Run via the `demo` builtin. */
static void run_demo(void) {
    static const char* script[] = {
        "pwd",
        "cd /home/user",
        "pwd",
        "cat welcome.txt",                 /* relative path -> resolves against cwd */
        "export NAME=NyxOS",
        "echo hello $NAME",                /* $VAR expansion */
        "ls / | grep elf",
        "echo redirected > /tmp/sh.txt",
        "cat /tmp/sh.txt | wc",
        "mkdir /tmp/proj",                 /* file tools: mkdir + touch + find */
        "touch /tmp/proj/b.txt /tmp/proj/a.txt",
        "find /tmp",
        "ls /tmp/proj | sort",             /* insertion order b,a -> sorted a,b */
        "rm /tmp/proj/a.txt /tmp/proj/b.txt",
        "ls /dev",                         /* device nodes */
        "echo swallowed by the void > /dev/null",
        "echo running in the background | upper &",
    };
    for (unsigned i = 0; i < sizeof(script) / sizeof(script[0]); i++) {
        char buf[160];
        expand_vars(script[i], buf, sizeof(buf));
        printf("sh$ %s\n", script[i]);
        if (!try_builtin(buf)) run_line(buf);
    }
    printf("sh: demo done.\n");
}

/* SIGINT (Ctrl-C): abandon the current input line and drop to a fresh prompt, like
 * a real shell. We print ^C here; the interrupted read(0) returns EINTR (<0), so the
 * REPL loop just continues and reprints the prompt. */
static void on_sigint(int sig) {
    (void)sig;
    write(1, "^C\n", 3);
}

/* ---- line editor: raw-mode readline with history + cursor editing ---------- */
/* Runs on TTY_RAW (no kernel echo): every keystroke arrives as a byte, arrows as
 * ESC [ A/B/C/D (H/F = home/end), and we render the line ourselves. Redraws use
 * '\r' + full reprint + trailing-erase + backspaces to park the cursor — both the
 * GUI terminal capture (which rebuilds its pending line on '\r'/'\b') and a real
 * serial terminal render that correctly. */
#define HIST_MAX 16
static char hist[HIST_MAX][128];
static int  hist_count;

static void redraw(const char* prompt, const char* buf, int len, int pos, int drawn) {
    write(1, "\r", 1);
    write(1, prompt, strlen(prompt));
    if (len > 0) write(1, buf, len);
    for (int i = len; i < drawn; i++) write(1, " ", 1);   /* erase leftovers */
    int end = (drawn > len) ? drawn : len;
    for (int i = end; i > pos; i--) write(1, "\b", 1);    /* park cursor at pos */
}

/* ---- tab completion (pure userspace, over getdents) ------------------------ */
static nyx_dirent_t cmp_ents[64];      /* .bss: getdents can't fault lazy pages   */
static char    cmp_cand[48][64];       /* candidate names                         */
static unsigned char cmp_isdir[48];

static int str_prefix(const char* s, const char* pfx) {
    while (*pfx) if (*s++ != *pfx++) return 0;
    return 1;
}

/* Insert `s` into buf at *ppos, shifting the tail right; caps at buf[128]. */
static void insert_at(char* buf, int* plen, int* ppos, const char* s) {
    int len = *plen, pos = *ppos, sl = (int)strlen(s);
    if (len + sl > 126) sl = 126 - len;
    if (sl <= 0) return;
    for (int j = len - 1; j >= pos; j--) buf[j + sl] = buf[j];
    for (int j = 0; j < sl; j++) buf[pos + j] = s[j];
    *plen = len + sl; *ppos = pos + sl;
}

/* Complete the token ending at the cursor. Command position (line start, or right
 * after '|') completes shell builtins + /*.elf names; otherwise a filesystem path
 * via getdents on the token's directory. A single match is inserted (+ ' ' or '/');
 * several share their longest common prefix, or are listed. Returns 1 if it printed
 * a candidate list (so the caller redraws on a fresh line), else 0. */
static int do_complete(char* buf, int* plen, int* ppos) {
    int pos = *ppos, ts = pos;
    while (ts > 0 && buf[ts - 1] != ' ') ts--;
    int k = ts - 1;                              /* command position? */
    while (k >= 0 && buf[k] == ' ') k--;
    int is_cmd = (k < 0) || buf[k] == '|';

    char token[128]; int tlen = pos - ts;
    if (tlen > 127) tlen = 127;
    for (int i = 0; i < tlen; i++) token[i] = buf[ts + i];
    token[tlen] = '\0';

    const char* base = token;
    int nc = 0;

    if (is_cmd) {
        static const char* bi[] = { "cd", "pwd", "export", "exit", "demo" };
        for (int i = 0; i < 5 && nc < 48; i++)
            if (str_prefix(bi[i], token)) {
                strncpy(cmp_cand[nc], bi[i], 63); cmp_cand[nc][63] = 0; cmp_isdir[nc] = 0; nc++;
            }
        long n = getdents("/", cmp_ents, 64);
        for (long i = 0; i < n && nc < 48; i++) {
            char* nm = cmp_ents[i].name;
            int l = (int)strlen(nm);
            if (l > 4 && strcmp(nm + l - 4, ".elf") == 0) {
                char bare[64]; int bl = l - 4; if (bl > 63) bl = 63;
                for (int j = 0; j < bl; j++) bare[j] = nm[j];
                bare[bl] = 0;
                if (str_prefix(bare, token)) {
                    strncpy(cmp_cand[nc], bare, 63); cmp_cand[nc][63] = 0; cmp_isdir[nc] = 0; nc++;
                }
            }
        }
    } else {
        char dir[128]; int sl = -1;
        for (int i = 0; i < tlen; i++) if (token[i] == '/') sl = i;
        if (sl < 0) { strcpy(dir, "."); base = token; }
        else {
            int dl = (sl == 0) ? 1 : sl;         /* "/x" -> dir "/" */
            for (int i = 0; i < dl; i++) dir[i] = token[i];
            dir[dl] = 0; base = token + sl + 1;
        }
        long n = getdents(dir, cmp_ents, 64);
        for (long i = 0; i < n && nc < 48; i++)
            if (str_prefix(cmp_ents[i].name, base)) {
                strncpy(cmp_cand[nc], cmp_ents[i].name, 63); cmp_cand[nc][63] = 0;
                cmp_isdir[nc] = (cmp_ents[i].type == 1);
                nc++;
            }
    }

    int blen = (int)strlen(base);
    if (nc == 0) return 0;
    if (nc == 1) {                               /* unique: complete + suffix */
        char ins[80]; int il = 0;
        for (const char* r = cmp_cand[0] + blen; *r && il < 70; ) ins[il++] = *r++;
        ins[il++] = cmp_isdir[0] ? '/' : ' ';
        ins[il] = 0;
        insert_at(buf, plen, ppos, ins);
        return 0;
    }
    int lcp = (int)strlen(cmp_cand[0]);          /* longest common prefix */
    for (int i = 1; i < nc; i++) {
        int j = 0;
        while (j < lcp && cmp_cand[i][j] == cmp_cand[0][j]) j++;
        lcp = j;
    }
    if (lcp > blen) {                            /* extend to the shared prefix */
        char ins[80]; int il = 0;
        for (int j = blen; j < lcp && il < 70; j++) ins[il++] = cmp_cand[0][j];
        ins[il] = 0;
        insert_at(buf, plen, ppos, ins);
        return 0;
    }
    write(1, "\n", 1);                           /* ambiguous: list candidates */
    for (int i = 0; i < nc; i++) {
        write(1, cmp_cand[i], strlen(cmp_cand[i]));
        if (cmp_isdir[i]) write(1, "/", 1);
        write(1, "  ", 2);
    }
    write(1, "\n", 1);
    return 1;
}

/* Read one edited line into out (NUL-terminated, no '\n'). Returns its length,
 * or -1 if the read was interrupted (Ctrl-C). Restores canonical mode on exit. */
static int readline(const char* prompt, char* out, int outsz) {
    char buf[128], saved[128];
    int len = 0, pos = 0, drawn = 0, saved_len = 0;
    int hview = hist_count;                  /* one past the newest entry */
    ttymode(TTY_RAW);
    write(1, prompt, strlen(prompt));
    for (;;) {
        char kb[16];
        long n = read(0, kb, sizeof(kb));
        if (n < 0) { ttymode(TTY_CANON); return -1; }    /* EINTR: Ctrl-C */
        for (long i = 0; i < n; i++) {
            char c = kb[i];
            if (c == 0x1B) {                 /* ESC [ x — emitted atomically */
                if (i + 2 >= n || kb[i + 1] != '[') continue;   /* lone ESC: ignore */
                char x = kb[i + 2];
                i += 2;
                if (x == 'A' || x == 'B') {  /* history up / down */
                    if (x == 'A') {
                        if (hview == 0) continue;
                        if (hview == hist_count) {       /* stash the in-progress line */
                            for (int j = 0; j < len; j++) saved[j] = buf[j];
                            saved_len = len;
                        }
                        hview--;
                        strncpy(buf, hist[hview], sizeof(buf) - 1);
                    } else {
                        if (hview >= hist_count) continue;
                        hview++;
                        if (hview == hist_count) {       /* back to the stashed line */
                            for (int j = 0; j < saved_len; j++) buf[j] = saved[j];
                            buf[saved_len] = '\0';
                        } else {
                            strncpy(buf, hist[hview], sizeof(buf) - 1);
                        }
                    }
                    buf[sizeof(buf) - 1] = '\0';
                    len = pos = (int)strlen(buf);
                } else if (x == 'D') { if (pos > 0) pos--; }         /* left  */
                else if (x == 'C') { if (pos < len) pos++; }         /* right */
                else if (x == 'H') { pos = 0; }                      /* home  */
                else if (x == 'F') { pos = len; }                    /* end   */
                redraw(prompt, buf, len, pos, drawn); drawn = len;
                continue;
            }
            if (c == '\n') {                 /* finish: cursor to end, real newline */
                redraw(prompt, buf, len, len, drawn);
                write(1, "\n", 1);
                buf[len] = '\0';
                int cn = (len < outsz - 1) ? len : outsz - 1;
                for (int j = 0; j < cn; j++) out[j] = buf[j];
                out[cn] = '\0';
                if (cn > 0 && (hist_count == 0 || strcmp(hist[hist_count - 1], out) != 0)) {
                    if (hist_count == HIST_MAX) {        /* drop the oldest */
                        for (int j = 0; j < HIST_MAX - 1; j++) strcpy(hist[j], hist[j + 1]);
                        hist_count--;
                    }
                    strncpy(hist[hist_count], out, sizeof(hist[0]) - 1);
                    hist[hist_count][sizeof(hist[0]) - 1] = '\0';
                    hist_count++;
                }
                ttymode(TTY_CANON);
                return cn;
            }
            if (c == '\b' || c == 0x7F) {    /* delete before the cursor */
                if (pos > 0) {
                    for (int j = pos - 1; j < len - 1; j++) buf[j] = buf[j + 1];
                    pos--; len--;
                    redraw(prompt, buf, len, pos, drawn); drawn = len;
                }
                continue;
            }
            if (c == '\t') {                 /* tab completion */
                do_complete(buf, &len, &pos);
                redraw(prompt, buf, len, pos, len); drawn = len;
                continue;
            }
            if (c >= 0x20 && c < 0x7F && len < (int)sizeof(buf) - 1) {  /* insert */
                for (int j = len; j > pos; j--) buf[j] = buf[j - 1];
                buf[pos] = c;
                len++; pos++;
                redraw(prompt, buf, len, pos, drawn); drawn = len;
            }
        }
    }
}

int main(int argc, char** argv) {
    /* Seed a default environment so children (and `env`) see something out of the
     * box; `export NAME=value` adds to it, and every command inherits it. */
    env_set("USER", "nyx");
    env_set("HOME", "/home/user");
    env_set("SHELL", "/sh.elf");
    env_set("PATH", "/bin:/usr/bin:/");
    env_set("TERM", "nyx");

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
    signal(SIGINT, on_sigint);           /* Ctrl-C -> fresh prompt instead of dying */
    signal(SIGTSTP, SIG_IGN);            /* Ctrl-Z at the prompt is a no-op (only jobs stop) */
    printf("NyxOS sh v0.8 — history/edit, Tab, '&' + job control (jobs/fg/bg, Ctrl-Z), 'demo', 'exit'\n");
    for (;;) {
        reap_jobs();                     /* report finished background jobs */
        char line[128];
        int n = readline("sh$ ", line, sizeof(line));
        if (n < 0) continue;             /* interrupted (Ctrl-C -> SIGINT): fresh prompt */
        char* t = trim(line);
        if (!*t) continue;
        char xbuf[192];                  /* $VAR / $? expansion happens before parsing */
        expand_vars(t, xbuf, sizeof(xbuf));
        char* x = trim(xbuf);
        if (!*x) continue;
        if (strcmp(x, "exit") == 0) break;
        if (strcmp(x, "demo") == 0) { run_demo(); continue; }
        if (try_builtin(x)) continue;    /* cd / pwd / export run in-process */
        run_line(x);
    }
    printf("sh: bye\n");
    return 0;
}
