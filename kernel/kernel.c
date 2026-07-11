// ============================================================
// kernel.c - Núcleo principal de NyxOS v3.0.0
// ============================================================
#include "kernel.h"
#include "compositor.h"
#include "apic.h"
#include "rtc.h"
#include "speaker.h"
#include "tcp.h"
#include "sb16.h"
#include "ext2.h"
#include "dns.h"
#include "http.h"
#include "smp.h"
#include "initramfs.h"
#include "bootsplash.h"
#include "auth.h"
#include "login.h"

// Variables globales del kernel
process_t* process_table[MAX_PROCESSES];
void* syscall_table[SYS_TABLE_SIZE];
net_iface_t net_interfaces[8];
mount_t mount_points[MAX_MOUNTS];
uint64_t memory_total = 0;
uint64_t memory_used = 0;
volatile uint32_t tick_count = 0;
bool kernel_initialized = false;
int process_count = 0;

// ============================================================
// Conversión de hexadecimal a bytes
// ============================================================
// ============================================================
// Utilidades para el comando date
// ============================================================
// ============================================================
// Tabla de comandos
// ============================================================
typedef struct {
    const char* name;
    void (*func)(int argc, char** argv);
    const char* help;
    bool hidden;
} command_t;

static void cmd_help(int argc, char** argv);
static void cmd_version(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_nyxfetch(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);
static void cmd_ps(int argc, char** argv);
static void cmd_mtdemo(int argc, char** argv);
static void cmd_mem(int argc, char** argv);
static void cmd_cpus(int argc, char** argv);
static void cmd_cowtest(int argc, char** argv);
static void cmd_crash(int argc, char** argv);
static void cmd_hexdump(int argc, char** argv);
static void cmd_date(int argc, char** argv);
static void cmd_uname(int argc, char** argv);
static void cmd_layout(int argc, char** argv);
static void cmd_ls(int argc, char** argv);
static void cmd_cd(int argc, char** argv);
static void cmd_pwd(int argc, char** argv);
static void cmd_cat(int argc, char** argv);
static void cmd_touch(int argc, char** argv);
static void cmd_mkdir(int argc, char** argv);
static void cmd_rm(int argc, char** argv);
static void cmd_cp(int argc, char** argv);
static void cmd_mv(int argc, char** argv);
static void cmd_ifconfig(int argc, char** argv);
static void cmd_ping(int argc, char** argv);
static void cmd_kill(int argc, char** argv);
static void cmd_which(int argc, char** argv);
static void cmd_head(int argc, char** argv);
static void cmd_tree(int argc, char** argv);
static void cmd_env(int argc, char** argv);
static void cmd_export(int argc, char** argv);
static void cmd_find(int argc, char** argv);
static void cmd_grep(int argc, char** argv);
static void cmd_tail(int argc, char** argv);
static void cmd_sort(int argc, char** argv);
static void cmd_wc(int argc, char** argv);
static void cmd_write(int argc, char** argv);
extern void cmd_dhcp(int argc, char** argv);
static void cmd_dns(int argc, char** argv);
static void cmd_history(int argc, char** argv);
static void cmd_diff(int argc, char** argv);
static void cmd_mode(int argc, char** argv);
static void cmd_gui(int argc, char** argv);
static void cmd_fonttest(int argc, char** argv);
static void cmd_desktop(int argc, char** argv);
static void cmd_beep(int argc, char** argv);
static void cmd_play(int argc, char** argv);
static void cmd_sb16play(int argc, char** argv);
static void cmd_exec(int argc, char** argv);
static void cmd_spawn(int argc, char** argv);
static void cmd_jobs(int argc, char** argv);
static void cmd_wait(int argc, char** argv);
static void cmd_nice(int argc, char** argv);
static void cmd_renice(int argc, char** argv);
static void cmd_usertest(int argc, char** argv);
static void cmd_tcptest(int argc, char** argv);
static void cmd_tcpdrop(int argc, char** argv);
static void cmd_tcploop(int argc, char** argv);
static void cmd_tcpserve(int argc, char** argv);
static void cmd_httpget(int argc, char** argv);
static void cmd_setip(int argc, char** argv);
static void cmd_mount(int argc, char** argv);

static const command_t commands[] = {
    {"help",      cmd_help,      "Show this help", false},
    {"version",   cmd_version,   "Show kernel version", false},
    {"clear",     cmd_clear,     "Clear the screen", false},
    {"nyxfetch",  cmd_nyxfetch,  "Show system info with ASCII logo", false},
    {"fastfetch", cmd_nyxfetch,  "Alias for nyxfetch", false},
    {"echo",      cmd_echo,      "Print a line of text", false},
    {"hexdump",   cmd_hexdump,   "Dump memory: hexdump <addr> [bytes]", false},
    {"date",      cmd_date,      "Show current date and time", false},
    {"uname",     cmd_uname,     "Show system information", false},
    {"reboot",    cmd_reboot,    "Reboot the system", false},
    {"ps",        cmd_ps,        "List processes", false},
    {"mtdemo",    cmd_mtdemo,    "Preemptive multitasking self-test", false},
    {"mem",       cmd_mem,       "Show memory usage", false},
    {"cpus",      cmd_cpus,      "List CPU cores (SMP)", false},
    {"cowtest",   cmd_cowtest,   "Test demand paging + copy-on-write", false},

    {"crash",     cmd_crash,     "Trigger a kernel panic", false},
    {"layout",    cmd_layout,    "Change keyboard layout: layout <us|es>", false},
    {"ls",        cmd_ls,        "List directory contents: ls [path]", false},
    {"cd",        cmd_cd,        "Change directory: cd <path>", false},
    {"pwd",       cmd_pwd,       "Print working directory", false},
    {"cat",       cmd_cat,       "Display file contents: cat <file>", false},
    {"touch",     cmd_touch,     "Create empty file: touch <file>", false},
    {"mkdir",     cmd_mkdir,     "Create directory: mkdir <dir>", false},
    {"rm",        cmd_rm,        "Remove file or directory: rm <path>", false},
    {"cp",        cmd_cp,        "Copy file: cp <src> <dst>", false},
    {"mv",        cmd_mv,        "Move/rename file: mv <src> <dst>", false},
    {"ifconfig",  cmd_ifconfig,  "Show network interfaces", false},
    {"dns",       cmd_dns,       "DNS resolve: dns <hostname>", false},
    {"ping",      cmd_ping,      "Ping a host: ping <ip|hostname>", false},
    {"kill",      cmd_kill,      "Kill a process: kill <pid>", false},
    {"which",     cmd_which,     "Show path of a command: which <name>", false},
    {"head",      cmd_head,      "Show first lines of a file: head <file> [lines]", false},
    {"tree",      cmd_tree,      "Show filesystem tree: tree [path]", false},
    {"env",       cmd_env,       "Show environment variables", false},
    {"export",    cmd_export,    "Set env variable: export <name>=<value>", false},
    {"find",      cmd_find,      "Find files by name: find <name> [path]", false},
    {"grep",      cmd_grep,      "Search file contents: grep <pattern> <file>", false},
    {"tail",      cmd_tail,      "Show last lines of a file: tail <file> [lines]", false},
    {"sort",      cmd_sort,      "Sort lines of a file: sort <file>", false},
    {"wc",        cmd_wc,        "Count lines/words/chars: wc <file>", false},
    {"write",     cmd_write,     "Write text to file: write <file> <text>", false},
    {"dhcp",      cmd_dhcp,      "Request IP via DHCP", false},
    {"history",   cmd_history,   "Show command history", false},
    {"diff",      cmd_diff,      "Compare two files: diff <file1> <file2>", false},
    {"mode",      cmd_mode,      "Set VBE video mode: mode <width> <height> <bpp>", false},
    {"gui",       cmd_gui,       "Launch GUI demo with mouse", false},
    {"fonttest",  cmd_fonttest,  "Test font rendering on framebuffer", false},
    {"desktop",   cmd_desktop,   "Launch window compositor desktop", false},
    {"beep",      cmd_beep,      "Play a tone: beep [freq] [ms]", false},
    {"play",      cmd_play,      "Play a demo melody", false},
    {"sb16play",  cmd_sb16play,  "Test SB16 playback: sb16play [freq] [ms]", false},
    {"exec",      cmd_exec,      "Run ELF in foreground (waits): exec <file>", false},
    {"spawn",     cmd_spawn,     "Run ELF in background: spawn <file>", false},
    {"jobs",      cmd_jobs,      "List background jobs", false},
    {"wait",      cmd_wait,      "Wait for background jobs: wait [pid]", false},
    {"nice",      cmd_nice,      "Spawn ELF at a scheduler weight: nice <weight> <file>", false},
    {"renice",    cmd_renice,    "Change a process's weight: renice <pid> <weight>", false},
    {"usertest",  cmd_usertest,  "Spawn preemptive ring-3 test processes", false},
    {"tcptest",   cmd_tcptest,   "Test TCP: tcptest <ip> <port>", false},
    {"tcpdrop",   cmd_tcpdrop,   "Test: drop next N TCP TX segs (force retransmit)", false},
    {"tcploop",   cmd_tcploop,   "In-guest TCP loopback self-test: tcploop [drop]", false},
    {"tcpserve",  cmd_tcpserve,  "Serve one TCP/HTTP connection: tcpserve [port]", false},
    {"httpget",   cmd_httpget,   "HTTP GET: httpget <url>", false},
    {"setip",     cmd_setip,     "Set static IP: setip <ip> <mask> <gw>", false},
    {"mount",     cmd_mount,     "Mount EXT2: mount [drive] [part_lba]", false},
    {"ext2ls",    cmd_mount,     "Alias for mount", false},
    {"ext2cat",   cmd_mount,     "Alias for mount", false},
    {NULL, NULL, NULL, false}
};

void command_list_matches(const char* partial, char* out, int out_size) {
    out[0] = '\0';
    int pos = 0;
    int plen = strlen(partial);
    for (int i = 0; commands[i].name != NULL && pos < out_size - 2; i++) {
        if (commands[i].hidden) continue;
        if (strncmp(commands[i].name, partial, plen) == 0) {
            int len = strlen(commands[i].name);
            if (pos + len + 2 > out_size - 1) break;
            memcpy(out + pos, commands[i].name, len);
            pos += len;
            out[pos++] = ' ';
            out[pos] = '\0';
        }
    }
}

void command_complete(const char* partial, char* out, int out_size, int* match_count) {
    *match_count = 0;
    if (out && out_size > 0) out[0] = '\0';
    int plen = strlen(partial);
    char first_match[64] = "";
    for (int i = 0; commands[i].name != NULL; i++) {
        if (commands[i].hidden) continue;
        if (strncmp(commands[i].name, partial, plen) == 0) {
            if (*match_count == 0)
                strncpy(first_match, commands[i].name, sizeof(first_match)-1);
            (*match_count)++;
        }
    }
    if (*match_count == 1 && out) {
        strncpy(out, first_match, out_size - 1);
        out[out_size - 1] = '\0';
    } else if (*match_count > 1 && out) {
        // Find common prefix
        char common[64];
        strncpy(common, first_match, sizeof(common)-1);
        for (int i = 0; commands[i].name != NULL && common[0]; i++) {
            if (commands[i].hidden) continue;
            if (strncmp(commands[i].name, partial, plen) == 0) {
                for (int j = 0; common[j]; j++) {
                    if (commands[i].name[j] != common[j]) {
                        common[j] = '\0';
                        break;
                    }
                }
            }
        }
        if (strlen(common) > (size_t)plen)
            strncpy(out, common, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

void execute_command(const char* cmd_line) {
    if (!cmd_line || !*cmd_line) return;
    char cmd_copy[256];
    strncpy(cmd_copy, cmd_line, 255);
    cmd_copy[255] = '\0';
    char* argv[10];
    int argc = 0;
    char* token = strtok(cmd_copy, " ");
    while (token != NULL && argc < 10) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    if (argc == 0) return;
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }
    // Command not found - output will be captured by putchar hook
    printf("Command not found: %s\n", argv[0]);
}

// ------------------------------------------------------------
// Implementación de comandos
// ------------------------------------------------------------
static void cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Available commands:\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        if (commands[i].hidden) continue;
        char buf[64];
        int len = strlen(commands[i].name);
        memcpy(buf, commands[i].name, len);
        for (int j = len; j < 14; j++) buf[j] = ' ';
        buf[14] = '\0';
        printf("  %s - %s\n", buf, commands[i].help);
    }
}

static void cmd_version(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s %s (%s)\n", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
}

static void cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    clear_screen();
}

static void cmd_nyxfetch(int argc, char** argv) {
    (void)argc; (void)argv;
    nyxfetch();
}

static void cmd_echo(int argc, char** argv) {
    if (argc < 2) { putchar('\n'); return; }
    int redir_idx = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0) { redir_idx = i; break; }
    }
    if (redir_idx > 0) {
        if (redir_idx + 1 >= argc) { printf("echo: no file specified\n"); return; }
        int fd = vfs_open(argv[redir_idx + 1], 0, 0);
        if (fd < 0) fd = vfs_open(argv[redir_idx + 1], 1, 0);
        if (fd < 0) { printf("echo: cannot write '%s'\n", argv[redir_idx + 1]); return; }
        for (int i = 1; i < redir_idx; i++) {
            vfs_write(fd, argv[i], strlen(argv[i]));
            if (i < redir_idx - 1) vfs_write(fd, " ", 1);
        }
        vfs_write(fd, "\n", 1);
        vfs_close(fd);
    } else {
        for (int i = 1; i < argc; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");
    }
}

static void cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    outb(0x64, 0xFE);
    __asm__ volatile("int $0");
}

static void cmd_ps(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("PID  PPID STATE NAME\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i]) {
            printf("%-4d %-4d %-5d %s\n",
                process_table[i]->pid,
                process_table[i]->ppid,
                process_table[i]->state,
                process_table[i]->comm);
        }
    }
}

static void cmd_mtdemo(int argc, char** argv) {
    (void)argc; (void)argv;
    uint64_t a0 = mtdemo_a_count, b0 = mtdemo_b_count;
    int r = mtdemo_start();
    if (r == -1) { printf("mtdemo: could not create demo threads\n"); return; }
    if (r == 1)  { printf("mtdemo already ran this session (threads retired). Reboot to run again.\n"); return; }
    printf("Preemptive scheduler ENABLED.\n");
    printf("Two kernel threads (mtdemoA/B) are now time-sliced with the desktop.\n");
    printf("mtdemoB has 3x the scheduler weight, so it should count ~3x faster.\n");
    printf("Sampling their counters over ~400ms:\n");
    sleep(400);   // main thread yields; scheduler runs A and B in the gaps
    printf("  mtdemoA (weight 1): %u -> %u\n", (unsigned)a0, (unsigned)mtdemo_a_count);
    printf("  mtdemoB (weight 3): %u -> %u\n", (unsigned)b0, (unsigned)mtdemo_b_count);
    printf("They run ~3s of heartbeats, then retire so the desktop returns to\n");
    printf("full speed. Heartbeats also go to the serial log; 'ps' lists them.\n");
}

static void cmd_which(int argc, char** argv) {
    if (argc < 2) { printf("Usage: which <command>\n"); return; }
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            if (commands[i].hidden)
                printf("%s: hidden command\n", argv[1]);
            else
                printf("/bin/%s\n", argv[1]);
            return;
        }
    }
    printf("%s: not found\n", argv[1]);
}

static void cmd_head(int argc, char** argv) {
    if (argc < 2) { printf("Usage: head <file> [lines]\n"); return; }
    int n = 10;
    if (argc >= 3) n = atoi(argv[2]);
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("head: cannot open '%s'\n", argv[1]); return; }
    char buf[1024];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    int lines = 0;
    for (int i = 0; buf[i] && lines < n; i++) {
        putchar(buf[i]);
        if (buf[i] == '\n') lines++;
    }
}

static void cmd_tree(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : vfs_getcwd();
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) { printf("tree: cannot access '%s'\n", path); return; }
    printf("%s\n", path);
    dirent_t* de = vfs_readdir(fd);
    while (de) {
        printf("|-- %s%s\n", de->name, de->type == 1 ? "/" : "");
        if (de->type == 1) {
            char subpath[256];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, de->name);
            int sfd = vfs_open(subpath, 0, 0);
            if (sfd >= 0) {
                dirent_t* sde = vfs_readdir(sfd);
                while (sde) {
                    printf("|   |-- %s\n", sde->name);
                    sde = vfs_readdir(sfd);
                }
                vfs_close(sfd);
            }
        }
        de = vfs_readdir(fd);
    }
    vfs_close(fd);
}

static char* env_vars[16];
static char env_buf[16][64];
static int env_count = 0;

static void cmd_env(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Environment variables:\n");
    for (int i = 0; i < env_count; i++) {
        printf("  %s\n", env_vars[i]);
    }
    printf("  (%d variables)\n", env_count);
}

static void cmd_export(int argc, char** argv) {
    if (argc < 2) { printf("Usage: export <name>=<value>\n"); return; }
    if (env_count >= 16) { printf("export: too many variables\n"); return; }
    strncpy(env_buf[env_count], argv[1], 63);
    env_buf[env_count][63] = '\0';
    env_vars[env_count] = env_buf[env_count];
    env_count++;
    printf("  %s\n", argv[1]);
}

static void cmd_find(int argc, char** argv) {
    const char* name = (argc >= 2) ? argv[1] : NULL;
    const char* start = (argc >= 3) ? argv[2] : vfs_getcwd();
    if (!name) { printf("Usage: find <name> [path]\n"); return; }
    int fd = vfs_open(start, 0, 0);
    if (fd < 0) { printf("find: cannot access '%s'\n", start); return; }
    dirent_t* de = vfs_readdir(fd);
    while (de) {
        if (strstr(de->name, name)) {
            printf("%s/%s\n", start, de->name);
        }
        if (de->type == 1) {
            char subpath[256];
            snprintf(subpath, sizeof(subpath), "%s/%s", start, de->name);
            int sfd = vfs_open(subpath, 0, 0);
            if (sfd >= 0) {
                dirent_t* sde = vfs_readdir(sfd);
                while (sde) {
                    if (strstr(sde->name, name)) {
                        printf("%s/%s\n", subpath, sde->name);
                    }
                    sde = vfs_readdir(sfd);
                }
                vfs_close(sfd);
            }
        }
        de = vfs_readdir(fd);
    }
    vfs_close(fd);
}

static void cmd_grep(int argc, char** argv) {
    if (argc < 3) { printf("Usage: grep <pattern> <file>\n"); return; }
    int fd = vfs_open(argv[2], 0, 0);
    if (fd < 0) { printf("grep: cannot open '%s'\n", argv[2]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    int line = 1;
    char *p = buf;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = '\0';
        if (strstr(p, argv[1])) {
            printf("%s:%d:%s\n", argv[2], line, p);
        }
        *nl = saved;
        if (*nl == '\n') { p = nl + 1; line++; }
        else break;
    }
}

static void cmd_tail(int argc, char** argv) {
    if (argc < 2) { printf("Usage: tail <file> [lines]\n"); return; }
    int n = 10;
    if (argc >= 3) n = atoi(argv[2]);
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("tail: cannot open '%s'\n", argv[1]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    int lines = 0;
    for (int i = 0; buf[i]; i++) if (buf[i] == '\n') lines++;
    int printed = 0;
    int start_line = lines - n;
    if (start_line < 0) start_line = 0;
    int cur = 0;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\n') cur++;
        if (cur >= start_line && printed < n) {
            putchar(buf[i]);
            if (buf[i] == '\n') printed++;
        }
    }
    if (printed > 0 && buf[bytes-1] != '\n') putchar('\n');
}

static void cmd_sort(int argc, char** argv) {
    if (argc < 2) { printf("Usage: sort <file>\n"); return; }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("sort: cannot open '%s'\n", argv[1]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    char *lines[128];
    int lc = 0;
    lines[lc++] = buf;
    for (int i = 0; buf[i] && lc < 128; i++) {
        if (buf[i] == '\n') { buf[i] = '\0'; if (buf[i+1]) lines[lc++] = &buf[i+1]; }
    }
    for (int i = 0; i < lc - 1; i++) {
        for (int j = 0; j < lc - i - 1; j++) {
            if (strcmp(lines[j], lines[j+1]) > 0) {
                char *tmp = lines[j]; lines[j] = lines[j+1]; lines[j+1] = tmp;
            }
        }
    }
    for (int i = 0; i < lc; i++) printf("%s\n", lines[i]);
}

static void cmd_wc(int argc, char** argv) {
    if (argc < 2) { printf("Usage: wc <file>\n"); return; }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("wc: cannot open '%s'\n", argv[1]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) { printf("0 0 0 %s\n", argv[1]); return; }
    buf[bytes] = '\0';
    int lines = 0, words = 0, chars = bytes;
    int in_word = 0;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\n') lines++;
        if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t') { in_word = 0; }
        else if (!in_word) { in_word = 1; words++; }
    }
    printf("%d %d %d %s\n", lines, words, chars, argv[1]);
}

static void cmd_write(int argc, char** argv) {
    if (argc < 3) { printf("Usage: write <file> <text>\n"); return; }
    // Build the full content
    char content[1024];
    int pos = 0;
    for (int i = 2; i < argc; i++) {
        for (char* p = argv[i]; *p && pos < 1023; p++) content[pos++] = *p;
        if (i < argc - 1 && pos < 1023) content[pos++] = ' ';
    }
    if (pos < 1023) content[pos++] = '\n';
    content[pos] = '\0';

    // Try mount-aware write first
    if (vfs_write_file(argv[1], content, pos) >= 0) return;

    // Fallback: create in RAM VFS
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) fd = vfs_open(argv[1], 1, 0);
    if (fd < 0) { printf("write: cannot create '%s'\n", argv[1]); return; }
    vfs_write(fd, content, pos);
    vfs_close(fd);
}

#define HIST_MAX 10
static char history[HIST_MAX][256];
static int hist_count = 0;

static void cmd_history(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Command history:\n");
    int start = (hist_count > HIST_MAX) ? (hist_count - HIST_MAX) : 0;
    for (int i = start; i < hist_count; i++) {
        printf("  %d  %s\n", i - start + 1, history[i % HIST_MAX]);
    }
}

static void cmd_diff(int argc, char** argv) {
    if (argc < 3) { printf("Usage: diff <file1> <file2>\n"); return; }
    int fd1 = vfs_open(argv[1], 0, 0);
    if (fd1 < 0) { printf("diff: cannot open '%s'\n", argv[1]); return; }
    int fd2 = vfs_open(argv[2], 0, 0);
    if (fd2 < 0) { vfs_close(fd1); printf("diff: cannot open '%s'\n", argv[2]); return; }
    char buf1[4096], buf2[4096];
    int len1 = vfs_read(fd1, buf1, sizeof(buf1) - 1);
    int len2 = vfs_read(fd2, buf2, sizeof(buf2) - 1);
    vfs_close(fd1); vfs_close(fd2);
    if (len1 < 0) len1 = 0;
    if (len2 < 0) len2 = 0;
    buf1[len1] = '\0'; buf2[len2] = '\0';
    char *lines1[256], *lines2[256];
    int n1 = 0, n2 = 0;
    lines1[n1++] = buf1; lines2[n2++] = buf2;
    for (int i = 0; buf1[i] && n1 < 256; i++)
        if (buf1[i] == '\n') { buf1[i] = '\0'; if (buf1[i+1]) lines1[n1++] = &buf1[i+1]; }
    for (int i = 0; buf2[i] && n2 < 256; i++)
        if (buf2[i] == '\n') { buf2[i] = '\0'; if (buf2[i+1]) lines2[n2++] = &buf2[i+1]; }
    int max = n1 > n2 ? n1 : n2;
    int diffs = 0;
    for (int i = 0; i < max; i++) {
        int have1 = i < n1, have2 = i < n2;
        if (!have1 || !have2 || strcmp(lines1[i], lines2[i]) != 0) {
            if (have1) printf("< %s\n", lines1[i]);
            if (have2) printf("> %s\n", lines2[i]);
            diffs++;
        }
    }
    printf("---\n%d line(s) differ\n", diffs);
}

static void cmd_mode(int argc, char** argv) {
    uint32_t w = 1024, h = 768, bpp = 32;
    if (argc >= 4) { w = atoi(argv[1]); h = atoi(argv[2]); bpp = atoi(argv[3]); }
    else if (argc >= 2) { printf("Usage: mode <width> <height> <bpp>\n"); return; }
    if (vbe_set_mode(w, h, bpp) < 0) {
        printf("Failed to set VBE mode\n");
        return;
    }
    fb_init(vbe_get_width(), vbe_get_height(), vbe_get_bpp(), vbe_get_lfb());
    fb_clear(fb_rgb(0x00, 0x40, 0x80));
    for (int i = 0; i < 256; i++) {
        uint32_t c = fb_rgb(i & 0xE0, (i & 0x1C) << 3, (i & 0x03) << 6);
        fb_fill_rect((i % 16) * (w / 16), (i / 16) * (h / 16),
                     w / 16 + 1, h / 16 + 1, c);
    }
    printf("VBE mode %dx%dx%d set\n", w, h, bpp);
}

static void cmd_gui(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!vbe_get_lfb()) {
        printf("No VBE mode set. Run 'mode' first.\n");
        return;
    }
    printf("Launching GUI demo...\n");
    printf("Mouse: click to draw, Esc to exit\n");
    gui_demo();
}

static void cmd_fonttest(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!vbe_get_lfb()) {
        printf("No VBE mode set. Run 'mode' first.\n");
        return;
    }
    printf("Testing font rendering...\n");
    fb_clear(fb_rgb(0x00, 0x00, 0x40));
    uint32_t fg = fb_rgb(0xFF, 0xFF, 0xFF);
    uint32_t bg = fb_rgb(0x00, 0x00, 0x40);
    font_draw_string(10, 10, "Hello from NyxOS bitmap font!", fg, bg);
    font_draw_string(10, 30, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", fb_rgb(0xFF, 0xFF, 0x00), bg);
    font_draw_string(10, 50, "abcdefghijklmnopqrstuvwxyz", fb_rgb(0x00, 0xFF, 0xFF), bg);
    font_draw_string(10, 70, "0123456789 !@#$%^&*()_+-=[]{}|;:',.<>?/", fb_rgb(0x00, 0xFF, 0x00), bg);
    font_draw_string(10, 90, "The quick brown fox jumps over the lazy dog.", fb_rgb(0xFF, 0x80, 0x80), bg);
    printf("Font test complete. Press Esc to return.\n");
    while (getchar_poll() != 0x1B);
    init_screen();
    clear_screen();
}

static void cmd_desktop(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!vbe_get_lfb()) {
        printf("No VBE mode set. Run 'mode' first.\n");
        return;
    }
    if (compositor_is_running()) {
        printf("Compositor is already running.\n");
        return;
    }
    printf("Launching window compositor...\n");
    printf("Esc to exit\n");
    compositor_init();
    compositor_run();
}

static void cmd_beep(int argc, char** argv) {
    uint32_t freq = 440;
    uint32_t dur = 500;
    if (argc >= 4) { freq = (uint32_t)atoi(argv[1]); dur = (uint32_t)atoi(argv[2]); }
    else if (argc >= 3) { freq = (uint32_t)atoi(argv[1]); dur = (uint32_t)atoi(argv[2]); }
    else if (argc >= 2) { freq = (uint32_t)atoi(argv[1]); }
    printf("Beep: %d Hz for %d ms\n", freq, dur);
    speaker_beep(freq, dur);
}

static void cmd_play(int argc, char** argv) {
    (void)argc; (void)argv;
    struct { uint32_t freq; uint32_t dur; } melody[] = {
        {NOTE_C4, 200}, {NOTE_D4, 200}, {NOTE_E4, 200}, {NOTE_F4, 200},
        {NOTE_G4, 200}, {NOTE_A4, 200}, {NOTE_B4, 200}, {NOTE_C5, 400},
        {NOTE_REST, 50},
        {NOTE_C5, 100}, {NOTE_C5, 100}, {NOTE_G4, 100}, {NOTE_G4, 100},
        {NOTE_A4, 100}, {NOTE_A4, 100}, {NOTE_G4, 200},
        {NOTE_F4, 100}, {NOTE_F4, 100}, {NOTE_E4, 100}, {NOTE_E4, 100},
        {NOTE_D4, 100}, {NOTE_D4, 100}, {NOTE_C4, 200},
    };
    printf("Playing melody...\n");
    for (int i = 0; i < (int)(sizeof(melody)/sizeof(melody[0])); i++)
        speaker_play_note(melody[i].freq, melody[i].dur);
    printf("Done.\n");
}

#include "sb16.h"
static void cmd_sb16play(int argc, char** argv) {
    (void)argc; (void)argv;
    if (!sb16_is_initialized()) {
        printf("SB16 not available. Run with QEMU -soundhw sb16\n");
        return;
    }
    uint32_t freq = 440;
    uint32_t dur_ms = 1000;
    if (argc >= 2) freq = atoi(argv[1]);
    if (argc >= 3) dur_ms = atoi(argv[2]);
    if (freq < 20 || freq > 44100) { printf("Frequency out of range (20-44100)\n"); return; }
    if (dur_ms < 50 || dur_ms > 10000) { printf("Duration out of range (50-10000)\n"); return; }

    uint32_t sample_rate = 22050;
    uint32_t samples = sample_rate * dur_ms / 1000;
    if (samples > 65535) samples = 65535;

    printf("SB16: generating %u Hz, %u ms (%u samples, 8-bit)\n", freq, dur_ms, samples);

    uint8_t* buf = sb16_get_buffer();
    if (!buf) { printf("SB16: no DMA buffer\n"); return; }

    // Generate square wave (8-bit unsigned: 128 = zero)
    for (uint32_t i = 0; i < samples; i++) {
        uint32_t period = sample_rate / freq;
        buf[i] = ((i % period) < (period / 2)) ? 200 : 80;
    }

    sb16_play_sound(buf, samples, sample_rate, 8);
    sleep(dur_ms + 100);
    sb16_stop_dma_bits(8);

    printf("SB16 playback done.\n");
}

#include "elf.h"
// Run an ELF as a FOREGROUND job: spawn it into the preemptive scheduler, then
// wait for it to exit — WITHOUT freezing the desktop. Rather than blocking the
// compositor thread in kwait() (which would park it and stop all repainting), we
// loop: recomposite the desktop, sleep a frame, and check whether the child has
// become a zombie. The child runs preemptively during each sleep and drains the
// keyboard ring itself via its read()/readkey() syscalls — the compositor's own
// event loop is suspended here (nested in this handler), so there's no contention
// over input. The upshot: a foreground TUI like `top` refreshes LIVE in the GUI
// window instead of only on the serial console. (v5.8.24 — before this the
// compositor was parked in kwait and the window was frozen until the job exited.)
static void cmd_exec(int argc, char** argv) {
    if (argc < 2) { printf("Usage: exec <file>\n"); return; }
    int pid = spawn_user_path(argv[1]);
    if (pid < 0) {
        printf("exec: could not load %s (err %d)\n", argv[1], pid);
        return;
    }
    extern uint32_t g_foreground_pid;
    g_foreground_pid = (uint32_t)pid;         // Ctrl-C posts SIGINT here while it runs
    // Non-blocking foreground wait: repaint at ~16 fps while the job runs. Only
    // reap_zombies runs on this (compositor) thread and it's busy in this loop,
    // so the child stays a zombie until we collect it below — no lost exit code.
    for (;;) {
        process_t* child = find_process((uint32_t)pid);
        if (!child || child->state == PROC_ZOMBIE) break;
        compositor_redraw_now();
        sleep(60);
    }
    int code = find_process((uint32_t)pid) ? kwait((uint32_t)pid) : 0;  // reap (immediate)
    g_foreground_pid = 0;
    compositor_redraw_now();                  // final frame + the prompt below
    printf("[exec] PID %d exited (code %d)\n", pid, code);
}

// Run an ELF as a BACKGROUND job: spawn it and return immediately. It runs
// preemptively alongside the desktop; 'ps' lists it and it's reaped when it exits.
static void cmd_spawn(int argc, char** argv) {
    if (argc < 2) { printf("Usage: spawn <file>\n"); return; }
    int pid = spawn_user_path(argv[1]);
    if (pid < 0) { printf("spawn: could not load %s (err %d)\n", argv[1], pid); return; }
    printf("[spawn] %s running in background as PID %d\n", argv[1], pid);
}

// List the shell's background jobs (scheduler-managed user processes).
static void cmd_jobs(int argc, char** argv) {
    (void)argc; (void)argv;
    extern process_t* process_table[MAX_PROCESSES];
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = process_table[i];
        if (p && p->sched_managed) {
            const char* st = (p->state == PROC_RUN)     ? "running" :
                             (p->state == PROC_ZOMBIE)  ? "exited"  :
                             (p->state == PROC_BLOCKED) ? "blocked" : "?";
            if (n++ == 0) printf("PID  PPID STATE   NAME\n");
            printf("%-4d %-4d %-7s %s\n", p->pid, p->ppid, st, p->comm);
        }
    }
    if (n == 0) printf("No background jobs.\n");
}

// Wait for a background job to finish: `wait <pid>` for one, or `wait` for all of
// the shell's children. Blocks the shell (like a foreground job) until they exit.
static void cmd_wait(int argc, char** argv) {
    if (argc >= 2) {
        uint32_t pid = (uint32_t)atoi(argv[1]);
        int code = kwait(pid);
        if (code == -1) printf("wait: no such child %d\n", pid);
        else            printf("wait: PID %d exited (code %d)\n", pid, code);
        return;
    }
    kwait_all();
    printf("wait: all background jobs done.\n");
}

// Clamp a scheduler weight to a sane range (1..64; higher = more CPU per turn).
static uint32_t clamp_weight(int w) {
    if (w < 1) w = 1;
    if (w > 64) w = 64;
    return (uint32_t)w;
}

// Spawn an ELF as a background job at a given scheduler weight (like `spawn`, but
// with a chosen CPU share — weight N runs N ticks per round-robin turn).
static void cmd_nice(int argc, char** argv) {
    if (argc < 3) { printf("Usage: nice <weight> <file>\n"); return; }
    uint32_t w = clamp_weight(atoi(argv[1]));
    int pid = spawn_user_path(argv[2]);
    if (pid < 0) { printf("nice: could not load %s (err %d)\n", argv[2], pid); return; }
    process_t* p = find_process((uint32_t)pid);
    if (p) p->sched_weight = w;
    printf("[nice] %s running in background as PID %d, weight %u\n", argv[2], pid, w);
}

// Change the scheduler weight of a running process (its CPU share).
static void cmd_renice(int argc, char** argv) {
    if (argc < 3) { printf("Usage: renice <pid> <weight>\n"); return; }
    uint32_t pid = (uint32_t)atoi(argv[1]);
    uint32_t w = clamp_weight(atoi(argv[2]));
    process_t* p = find_process(pid);
    if (!p) { printf("renice: process %d not found\n", pid); return; }
    p->sched_weight = w;
    printf("renice: PID %d weight set to %u\n", pid, w);
}

// Load an ELF from `path` and hand it to the preemptive scheduler as a background
// ring-3 process (non-blocking — unlike exec, which blocks the caller until the
// process exits). Marked sched_managed so irq_scheduler_tick round-robins it; it
// leaves via the SYS_EXIT zombie path and reap_zombies() frees it. Returns the new
// PID, or a negative error code.
int spawn_user_path(const char* path) {
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
    process_t* proc = NULL;
    int r = elf_load(copy, size, &proc);
    kfree(copy);
    if (r != 0 || !proc) return -5;
    proc_set_comm(proc, path); // name it after the file (elf_load hardcodes "elf")
    strncpy(proc->cmdline, path, sizeof(proc->cmdline) - 1);
    proc->cmdline[sizeof(proc->cmdline) - 1] = '\0';
    proc->sched_managed = 1;   // scheduler now round-robins this ring-3 process
    extern process_t* get_current_process(void);
    process_t* parent = get_current_process();
    proc->ppid = parent ? parent->pid : 0;   // so kwait() can find/wake the parent
    sched_enable();
    return (int)proc->pid;
}

static void cmd_usertest(int argc, char** argv) {
    (void)argc; (void)argv;
    const char* path = "/spin.elf";
    printf("Spawning preemptive ring-3 processes from %s ...\n", path);
    int p1 = spawn_user_path(path);
    int p2 = spawn_user_path(path);
    if (p1 < 0 || p2 < 0) {
        printf("spawn failed (p1=%d p2=%d) — is %s in the initramfs?\n", p1, p2, path);
        return;
    }
    printf("Spawned PID %d and PID %d in ring 3, time-sliced with this desktop.\n", p1, p2);
    printf("The GUI stays responsive while they run (proof of ring-3 preemption);\n");
    printf("they print to the serial log, then exit. 'ps' lists them, then reaped.\n");
}

static uint32_t parse_ip(const char* s) {
    // Network byte order: first octet in the low byte, matching net_interfaces[].ip
    // and what ip_send puts on the wire (so ping/setip target the right address).
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t octet = 0;
        while (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (*s - '0');
            s++;
        }
        ip |= (octet & 0xFF) << (i * 8);
        if (*s == '.') s++;
    }
    return ip;
}

static void cmd_setip(int argc, char** argv) {
    if (argc < 2) { printf("Usage: setip <ip> [mask] [gw]\n"); return; }
    uint32_t ip = parse_ip(argv[1]);
    uint32_t mask = 0x00FFFFFF;
    uint32_t gw = 0;
    if (argc >= 3) mask = parse_ip(argv[2]);
    if (argc >= 4) gw = parse_ip(argv[3]);
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            net_interfaces[i].ip = ip;
            net_interfaces[i].netmask = mask;
            net_interfaces[i].gateway = gw;
            printf("Set %s IP to %d.%d.%d.%d\n", net_interfaces[i].name,
                ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
            return;
        }
    }
    printf("No interface found\n");
}

static void cmd_mount(int argc, char** argv) {
    (void)argc; (void)argv;
    uint8_t drive = 0;
    uint32_t part_lba = 0;

    if (argc >= 2) drive = (uint8_t)atoi(argv[1]);
    if (argc >= 3) part_lba = (uint32_t)atoi(argv[2]);

    if (ext2_fs.sb.magic == EXT2_SUPER_MAGIC) {
        printf("EXT2 already mounted.\n");
        return;
    }

    ata_init();
    if (ext2_mount(drive, part_lba) < 0) {
        printf("EXT2: no EXT2 filesystem found on drive %d at LBA %u\n", drive, part_lba);
        return;
    }

    printf("EXT2 mounted: %u blocks, %u inodes, block size %u\n",
           ext2_fs.sb.total_blocks, ext2_fs.sb.total_inodes, ext2_fs.block_size);

    if (vfs_mount("/mnt", FS_TYPE_EXT2, NULL) < 0) {
        printf("VFS mount failed\n");
        return;
    }
    printf("EXT2 available at /mnt. Use 'ls /mnt', 'cat /mnt/...' etc.\n");
}

static void cmd_tcptest(int argc, char** argv) {
    uint32_t dst_ip = 0x0A000202; // 10.0.2.2
    uint16_t dst_port = 80;
    if (argc >= 3) {
        dst_ip = parse_ip(argv[1]);
        if (!dst_ip) {
            int iface_idx = -1;
            for (int i = 0; i < 8; i++) {
                if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
                    iface_idx = i; break;
                }
            }
            if (iface_idx >= 0) dst_ip = dns_resolve(argv[1], iface_idx);
        }
        dst_port = (uint16_t)atoi(argv[2]);
    } else if (argc >= 2) {
        dst_ip = parse_ip(argv[1]);
        if (!dst_ip) {
            int iface_idx = -1;
            for (int i = 0; i < 8; i++) {
                if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
                    iface_idx = i; break;
                }
            }
            if (iface_idx >= 0) dst_ip = dns_resolve(argv[1], iface_idx);
        }
    }
    if (!dst_ip) { printf("tcptest: could not resolve %s\n", argv[1]); return; }
    printf("TCP test: connecting to %d.%d.%d.%d:%d...\n",
           dst_ip&0xFF, (dst_ip>>8)&0xFF, (dst_ip>>16)&0xFF, (dst_ip>>24)&0xFF,
           dst_port);
    int conn = tcp_connect(dst_ip, dst_port, 12345);
    if (conn < 0) {
        printf("TCP connect failed\n");
        return;
    }

    // Wait for the 3-way handshake (tcp_connect only fires the SYN; the SYN-ACK
    // is processed in tcp_handle_packet on poll). A lost SYN is retransmitted by
    // tcp_tick() inside kernel_poll_net(), so this still connects.
    uint32_t deadline = get_ticks() + 4000;
    while ((int32_t)(get_ticks() - deadline) < 0) {
        kernel_poll_net();
        if (tcp_state(conn) == TCP_STATE_ESTABLISHED) break;
    }
    if (tcp_state(conn) != TCP_STATE_ESTABLISHED) {
        printf("TCP handshake did not complete (state=%d)\n", tcp_state(conn));
        tcp_close(conn);
        return;
    }
    printf("TCP established (conn_id=%d), sending HTTP GET...\n", conn);

    char req[256];
    int rl = snprintf(req, sizeof(req),
        "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
        (argc >= 2) ? argv[1] : "test");
    if (tcp_send(conn, (const uint8_t*)req, rl) < 0) {
        printf("TCP send failed\n");
        tcp_close(conn);
        return;
    }
    printf("Sent request, waiting for response...\n");

    uint8_t buf[2048];
    int total = 0;
    uint32_t start = get_ticks(), last = start;
    for (;;) {
        kernel_poll_net();
        int n = tcp_recv(conn, buf, sizeof(buf) - 1);
        if (n > 0) {
            if (total == 0) {
                int hn = n < 16 ? n : 16;
                printf("First %d bytes:", hn);
                for (int i = 0; i < hn; i++) printf(" %x", buf[i]);
                printf("\n");
            }
            buf[n] = '\0';
            printf("%s", (char*)buf);
            total += n; last = get_ticks();
        } else if (n < 0) {
            printf("\n[peer closed]\n");
            break;
        }
        uint32_t now = get_ticks();
        if (total > 0 && (int32_t)(now - (last + 1500)) >= 0) break;
        if ((int32_t)(now - (start + 8000)) >= 0) break;
    }
    if (total == 0) printf("(no data received)\n");
    printf("\nTCP test done (%d bytes received). Closing...\n", total);
    tcp_close(conn);
}

static void cmd_tcpdrop(int argc, char** argv) {
    int n = (argc >= 2) ? atoi(argv[1]) : 1;
    if (n < 0) n = 0;
    tcp_debug_drop(n);
    printf("TCP: will silently drop the next %d outbound segment(s).\n", n);
    printf("Run e.g. 'httpget example.com' — the RTO timer must retransmit to recover.\n");
}

static void cmd_tcploop(int argc, char** argv) {
    int drop = (argc >= 2) ? atoi(argv[1]) : 0;
    uint16_t port = 7777;
    uint32_t lo = 0x0100007F;   // 127.0.0.1 (network order)

    printf("TCP loopback self-test on 127.0.0.1:%d%s\n", port,
           drop ? " (a data segment will be dropped)" : "");

    int srv = tcp_listen(port);
    if (srv < 0) { printf("listen() failed\n"); return; }
    int cli = tcp_connect(lo, port, 50000);
    if (cli < 0) { printf("connect() failed\n"); tcp_close(srv); return; }

    // Drive the 3-way handshake. Loopback delivery and tcp_tick() both run inside
    // kernel_poll_net(), so this needs no NIC and never touches the wire.
    int child = -1;
    uint32_t deadline = get_ticks() + 3000;
    while ((int32_t)(get_ticks() - deadline) < 0) {
        kernel_poll_net();
        if (child < 0) child = tcp_accept(srv);
        if (child >= 0 && tcp_state(cli) == TCP_STATE_ESTABLISHED) break;
    }
    if (child < 0 || tcp_state(cli) != TCP_STATE_ESTABLISHED) {
        printf("FAIL: handshake did not complete (cli state=%d, child=%d)\n",
               tcp_state(cli), child);
        tcp_close(cli); tcp_close(srv); return;
    }
    printf("Handshake OK: client conn %d <-> server child %d ESTABLISHED\n", cli, child);

    // Force a retransmit on the next data segment if asked — the RTO timer must
    // recover it entirely in-guest.
    if (drop) tcp_debug_drop(1);

    // Client -> server.
    const char* msg = "Hello over 127.0.0.1 from the client!";
    int mlen = (int)strlen(msg);
    tcp_send(cli, (const uint8_t*)msg, mlen);
    uint8_t rx[256]; int got = 0;
    deadline = get_ticks() + 3000;
    while ((int32_t)(get_ticks() - deadline) < 0) {
        kernel_poll_net();
        int n = tcp_recv(child, rx + got, sizeof(rx) - 1 - got);
        if (n > 0) got += n;
        if (got >= mlen) break;
    }
    rx[got < 0 ? 0 : got] = '\0';
    int ok1 = (got == mlen) && strcmp((char*)rx, msg) == 0;
    printf("  server recv %d bytes: \"%s\" [%s]\n", got, rx, ok1 ? "OK" : "MISMATCH");

    // Server -> client (echo a reply).
    const char* reply = "ACK: got it, hello back from the server.";
    int rlen = (int)strlen(reply);
    tcp_send(child, (const uint8_t*)reply, rlen);
    uint8_t rx2[256]; int got2 = 0;
    deadline = get_ticks() + 3000;
    while ((int32_t)(get_ticks() - deadline) < 0) {
        kernel_poll_net();
        int n = tcp_recv(cli, rx2 + got2, sizeof(rx2) - 1 - got2);
        if (n > 0) got2 += n;
        if (got2 >= rlen) break;
    }
    rx2[got2 < 0 ? 0 : got2] = '\0';
    int ok2 = (got2 == rlen) && strcmp((char*)rx2, reply) == 0;
    printf("  client recv %d bytes: \"%s\" [%s]\n", got2, rx2, ok2 ? "OK" : "MISMATCH");

    printf("TCP loopback self-test: %s\n", (ok1 && ok2) ? "PASS" : "FAIL");
    tcp_close(cli);
    tcp_close(child);
    tcp_close(srv);
}

static void cmd_tcpserve(int argc, char** argv) {
    uint16_t port = (argc >= 2) ? (uint16_t)atoi(argv[1]) : 80;
    int srv = tcp_listen(port);
    if (srv < 0) { printf("listen() failed (no free conn slot)\n"); return; }
    printf("Listening on 0.0.0.0:%d (waiting up to 20s for a client)...\n", port);

    // Wait for an inbound connection. Loopback delivery, NIC RX and tcp_tick all
    // run inside kernel_poll_net(), so this serves both 127.0.0.1 and the NIC.
    int child = -1;
    uint32_t deadline = get_ticks() + 20000;
    while ((int32_t)(get_ticks() - deadline) < 0) {
        kernel_poll_net();
        child = tcp_accept(srv);
        if (child >= 0) break;
    }
    if (child < 0) { printf("No connection received (timeout).\n"); tcp_close(srv); return; }
    printf("Accepted a connection (child=%d). Reading request...\n", child);

    // Read the request. Respond as soon as the HTTP header block is complete
    // (\r\n\r\n), or the client half-closes (its FIN -> CLOSE_WAIT), or it goes
    // quiet — a client that FINs right after its request must still get a reply.
    uint8_t buf[1024]; int total = 0;
    uint32_t last = get_ticks();
    for (;;) {
        kernel_poll_net();
        int n = tcp_recv(child, buf + total, sizeof(buf) - 1 - total);
        if (n > 0) { total += n; last = get_ticks(); }
        buf[total] = '\0';
        if (total > 0 && strstr((char*)buf, "\r\n\r\n")) break;
        if (total > 0 && tcp_state(child) == TCP_STATE_CLOSE_WAIT) break;
        if (total > 0 && (int32_t)(get_ticks() - (last + 800)) >= 0) break;
        if (total >= (int)sizeof(buf) - 1) break;
        if ((int32_t)(get_ticks() - (last + 4000)) >= 0) break;
    }
    printf("--- request (%d bytes) ---\n%s\n--- end ---\n", total, buf);

    // Reply with a small HTTP/1.1 response (body is exactly 21 bytes).
    const char* resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 21\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Hello from NyxOS TCP!";
    tcp_send(child, (const uint8_t*)resp, strlen(resp));

    // Let the data segment and its ACK flush before we FIN.
    uint32_t t = get_ticks();
    while ((int32_t)(get_ticks() - (t + 700)) < 0) kernel_poll_net();

    printf("Response sent. Closing.\n");
    tcp_close(child);
    tcp_close(srv);
}

static void cmd_httpget(int argc, char** argv) {
    if (argc < 2) { printf("Usage: httpget <url>\n"); return; }
    const char* url = argv[1];
    char host[128] = {0};
    char path[256] = {0};
    uint16_t port = 80;

    // Parse URL: http://host:port/path
    const char* p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    const char* host_start = p;
    while (*p && *p != ':' && *p != '/') p++;
    int host_len = (int)(p - host_start);
    if (host_len > 127) host_len = 127;
    __builtin_memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    if (*p == ':') {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }
    if (!*p || *p == '/') {
        const char* path_start = p;
        if (!*path_start) path_start = "/";
        strncpy(path, path_start, 255);
        path[255] = '\0';
    } else {
        path[0] = '/';
        path[1] = '\0';
    }

    printf("HTTP GET http://%s:%d%s ...\n", host, port, path);

    int iface_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            iface_idx = i; break;
        }
    }

    http_response_t resp;
    if (http_get(host, port, path, &resp, iface_idx) < 0) {
        printf("HTTP GET failed\n");
        return;
    }

    printf("Status: %d %s\n", resp.status_code, resp.status_text);
    printf("Body (%d bytes):\n", resp.body_len);
    printf("%s", resp.body ? (char*)resp.body : "(empty)");
    if (resp.body && resp.body_len > 0 && resp.body[resp.body_len-1] != '\n')
        printf("\n");
    http_free(&resp);
}

// Add history entry (called from shell loop)
static void add_history(const char* cmd) {
    if (hist_count > 0) {
        int last = (hist_count - 1) % HIST_MAX;
        if (strcmp(history[last], cmd) == 0) return;
    }
    strncpy(history[hist_count % HIST_MAX], cmd, 255);
    history[hist_count % HIST_MAX][255] = '\0';
    hist_count++;
}

static void cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Physical memory: %d MB total, %d MB used, %d MB free\n",
           memory_total / (1024*1024),
           memory_used / (1024*1024),
           (memory_total - memory_used) / (1024*1024));
    printf("Heap size: %d KB\n", KERNEL_HEAP_SIZE / 1024);
}

static void cmd_cpus(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("SMP: %u CPU(s) online (max %u)\n", cpu_count, MAX_CPUS);
    printf("CPU  ROLE  APIC  SELF  STATE\n");
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        // APIC = id the BSP assigned; SELF = id the core read from its own LAPIC.
        printf("%-3u  %-4s  %-4u  %-4u  %s\n",
               i, i == 0 ? "BSP" : "AP",
               cpu_info[i].apic_id, cpu_info[i].apic_id_self,
               cpu_info[i].running ? "online" : "offline");
    }
}

static void cmd_cowtest(int argc, char** argv) {
    (void)argc; (void)argv;
    // Two pages in an otherwise-unused PML4 slot (0xFFFFA000_00000000).
    volatile uint64_t* DV = (volatile uint64_t*)0xFFFFA00000000000ULL;
    volatile uint64_t* CV = (volatile uint64_t*)0xFFFFA00000001000ULL;
    uint64_t d0 = vm_stat_demand(), c0 = vm_stat_cow();
    int ok = 1;

    printf("=== Demand paging ===\n");
    if (vm_map_demand((uint64_t)DV) < 0) { printf("vm_map_demand failed\n"); return; }
    uint64_t first = DV[0];                 // first touch -> #PF allocates a zeroed page
    printf("  first read (fault-allocated): 0x%lx  [%s]\n", first, first == 0 ? "OK zeroed" : "BAD");
    DV[0] = 0xCAFEBABE;
    printf("  after write:                  0x%lx  [%s]\n", DV[0], DV[0] == 0xCAFEBABE ? "OK" : "BAD");
    printf("  demand faults taken: %lu\n", vm_stat_demand() - d0);
    ok &= (first == 0) && (DV[0] == 0xCAFEBABE);

    printf("=== Copy-on-write ===\n");
    void* shared = alloc_page();
    if (!shared) { printf("no free page\n"); vm_unmap((uint64_t)DV); return; }
    volatile uint64_t* SH = (volatile uint64_t*)(uint64_t)shared;   // identity-mapped
    SH[0] = 0xAAAAAAAA;
    vm_map_cow((uint64_t)CV, (uint64_t)shared);
    printf("  read (shared, no fault):      0x%lx  [%s]\n", CV[0], CV[0] == 0xAAAAAAAA ? "OK" : "BAD");
    CV[0] = 0xBBBBBBBB;                      // write -> #PF makes a private copy
    printf("  after write:                  0x%lx  [%s]\n", CV[0], CV[0] == 0xBBBBBBBB ? "OK" : "BAD");
    printf("  original page still:          0x%lx  [%s]\n", SH[0], SH[0] == 0xAAAAAAAA ? "OK unchanged" : "BAD shared!");
    printf("  cow faults taken: %lu\n", vm_stat_cow() - c0);
    ok &= (CV[0] == 0xBBBBBBBB) && (SH[0] == 0xAAAAAAAA);

    printf("VM (demand + COW) test: %s\n", ok ? "PASS" : "FAIL");

    // Free the fault-allocated pages (demand page + COW copy) and the shared page.
    void* dphys = get_phys_addr((void*)DV);
    void* cphys = get_phys_addr((void*)CV);
    vm_unmap((uint64_t)DV);
    vm_unmap((uint64_t)CV);
    if (dphys) free_page((void*)((uint64_t)dphys & ~0xFFFULL));
    if (cphys) free_page((void*)((uint64_t)cphys & ~0xFFFULL));
    free_page(shared);
}

static void cmd_crash(int argc, char** argv) {
    (void)argc; (void)argv;
    kernel_panic("User requested crash");
}

static void cmd_hexdump(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: hexdump <addr> [bytes]\n");
        return;
    }
    char* end;
    uint32_t addr = (uint32_t)strtol(argv[1], &end, 0);
    if (*end != '\0') {
        printf("Invalid address: %s\n", argv[1]);
        return;
    }
    uint32_t count = 256;
    if (argc >= 3) {
        count = (uint32_t)strtol(argv[2], &end, 0);
        if (*end != '\0' || count == 0) {
            printf("Invalid byte count: %s\n", argv[2]);
            return;
        }
    }
    if (count > 1024) {
        printf("Max 1024 bytes per dump.\n");
        count = 1024;
    }
    printf("Dumping %d bytes from 0x%x:\n", count, addr);
    uint8_t* ptr = (uint8_t*)(uintptr_t)addr;
    for (uint32_t i = 0; i < count; i += 16) {
        char addr_str[9];
        uint32_t a = addr + i;
        for (int d = 7; d >= 0; d--) {
            addr_str[d] = "0123456789abcdef"[a & 0xF];
            a >>= 4;
        }
        addr_str[8] = '\0';
        printf("%s  ", addr_str);
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < count) {
                char hex[3];
                uint8_t b = ptr[i + j];
                hex[0] = "0123456789abcdef"[b >> 4];
                hex[1] = "0123456789abcdef"[b & 0xF];
                hex[2] = '\0';
                printf("%s ", hex);
            } else {
                printf("   ");
            }
        }
        printf(" |");
        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < count) {
                char c = ptr[i + j];
                if (c >= 32 && c <= 126) {
                    putchar(c);
                } else {
                    putchar('.');
                }
            }
        }
        printf("|\n");
    }
}

static void cmd_date(int argc, char** argv) {
    (void)argc; (void)argv;
    rtc_time_t t;
    rtc_read_time(&t);
    printf("%04u-%02u-%02u %02u:%02u:%02u\n",
           t.year, t.month, t.day, t.hour, t.minute, t.second);
}

static void cmd_uname(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s %s (%s) %s\n", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME, "x86");
}

static void cmd_layout(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: layout <us|es>\n");
        printf("Current layout: %s\n", keyboard_layout == 0 ? "US (QWERTY)" : "Spanish (ES)");
        return;
    }
    if (strcmp(argv[1], "us") == 0) {
        set_keyboard_layout(0);
        printf("Keyboard layout set to US (QWERTY).\n");
    } else if (strcmp(argv[1], "es") == 0) {
        set_keyboard_layout(1);
        printf("Keyboard layout set to Spanish (ES).\n");
    } else {
        printf("Unknown layout: %s. Use 'us' or 'es'.\n", argv[1]);
    }
}

static void cmd_ls(int argc, char** argv) {
    vfs_list_dir(argc > 1 ? argv[1] : NULL);
}

static void cmd_cd(int argc, char** argv) {
    if (argc < 2) { printf("Usage: cd <path>\n"); return; }
    if (vfs_chdir(argv[1]) < 0) printf("cd: %s: No such directory\n", argv[1]);
}

static void cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s\n", vfs_getcwd());
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) { printf("Usage: cat <file>\n"); return; }
    vfs_cat_file(argv[1]);
    putchar('\n');
}

static void cmd_touch(int argc, char** argv) {
    if (argc < 2) { printf("Usage: touch <file>\n"); return; }
    if (vfs_touch(argv[1]) < 0) printf("touch: failed to create %s\n", argv[1]);
}

static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { printf("Usage: mkdir <dir>\n"); return; }
    if (vfs_mkdir(argv[1], 0755) < 0) printf("mkdir: failed to create %s\n", argv[1]);
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) { printf("Usage: rm <path>\n"); return; }
    if (vfs_unlink(argv[1]) < 0) printf("rm: failed to remove %s\n", argv[1]);
}

static void cmd_cp(int argc, char** argv) {
    if (argc < 3) { printf("Usage: cp <src> <dst>\n"); return; }
    if (vfs_cp(argv[1], argv[2]) < 0) printf("cp: failed to copy %s to %s\n", argv[1], argv[2]);
}

static void cmd_mv(int argc, char** argv) {
    if (argc < 3) { printf("Usage: mv <src> <dst>\n"); return; }
    vfs_rename(argv[1], argv[2]);
}

static void cmd_ifconfig(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Network interfaces:\n");
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0]) {
            printf("%s  HWaddr %x:%x:%x:%x:%x:%x  IP %d.%d.%d.%d\n",
                net_interfaces[i].name,
                net_interfaces[i].mac[0], net_interfaces[i].mac[1],
                net_interfaces[i].mac[2], net_interfaces[i].mac[3],
                net_interfaces[i].mac[4], net_interfaces[i].mac[5],
                net_interfaces[i].ip&0xFF, (net_interfaces[i].ip>>8)&0xFF, (net_interfaces[i].ip>>16)&0xFF, (net_interfaces[i].ip>>24)&0xFF);
        }
    }
}

static void cmd_dns(int argc, char** argv) {
    if (argc < 2) { printf("Usage: dns <hostname>\n"); return; }
    int iface_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            iface_idx = i; break;
        }
    }
    if (iface_idx < 0) { printf("No network interface\n"); return; }
    uint32_t ip = dns_resolve(argv[1], iface_idx);
    if (ip) {
        printf("%s -> %d.%d.%d.%d\n", argv[1],
            ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
    } else {
        printf("dns: failed to resolve %s\n", argv[1]);
    }
}

static void cmd_ping(int argc, char** argv) {
    if (argc < 2) { printf("Usage: ping <ip|hostname>\n"); return; }
    uint32_t ip = 0;
    int seg = 0;
    char* p = argv[1];
    while (*p) {
        if (*p == '.') { seg++; }
        else if (*p >= '0' && *p <= '9') {
            uint8_t val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            // IPs are stored in network order (first octet = low byte), matching
            // net_interfaces[].ip / DHCP / ip_send. A numeric literal parsed the
            // other way sent packets to a byte-reversed address and printed the
            // dotted quad backwards (127.0.0.1 shown as 1.0.0.127).
            if (seg == 0) ip |= (uint32_t)val;
            else if (seg == 1) ip |= (uint32_t)val << 8;
            else if (seg == 2) ip |= (uint32_t)val << 16;
            else if (seg == 3) ip |= (uint32_t)val << 24;
            continue;
        }
        p++;
    }
    if (seg != 3) {
        int iface_idx = -1;
        for (int i = 0; i < 8; i++) {
            if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
                iface_idx = i; break;
            }
        }
        if (iface_idx >= 0) ip = dns_resolve(argv[1], iface_idx);
        if (!ip) { printf("ping: failed to resolve %s\n", argv[1]); return; }
    }
    printf("PING %d.%d.%d.%d: 56 data bytes\n", ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);

    // Loopback / our-own-address pings need no NIC — they never touch the wire.
    int is_loopback = ((ip & 0xFF) == 0x7F);   // 127.0.0.0/8 (net order: low byte)
    int iface_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (!net_interfaces[i].name[0]) continue;
        if (net_interfaces[i].ip == ip) is_loopback = 1;
        if (iface_idx < 0 && strcmp(net_interfaces[i].name, "lo") != 0) iface_idx = i;
    }
    if (iface_idx < 0 && !is_loopback) { printf("No network interface available\n"); return; }

    icmp_ping(ip, 4, iface_idx);
}

static void cmd_kill(int argc, char** argv) {
    (void)argc; (void)argv;
    if (argc < 2) { printf("Usage: kill <pid>\n"); return; }
    uint32_t pid = (uint32_t)atoi(argv[1]);
    process_t* p = find_process(pid);
    if (!p) { printf("kill: process %d not found\n", pid); return; }
    if (p->page_directory == NULL) {
        // init/idle/compositor/mtdemo are kernel threads; freeing their stacks out
        // from under the scheduler would crash the system.
        printf("kill: %d is a kernel thread — refusing\n", pid);
        return;
    }
    destroy_process(pid);
    printf("kill: process %d terminated\n", pid);
}

// ============================================================
// Backdoor shell
// ============================================================
// ============================================================
// kernel_panic
// ============================================================
void kernel_panic(const char* msg, ...) {
    va_list args;
    va_start(args, msg);
    printf("\n\n[KERNEL PANIC] ");
    vprintf(msg, args);
    printf("\n\nSystem halted.\n");
    va_end(args);
    uint64_t cr0, cr2, cr3;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    printf("CR0=0x%lx CR2=0x%lx CR3=0x%lx\n", cr0, cr2, cr3);
    while(1) { __asm__ volatile("hlt"); }
}

// ============================================================
// kernel_main
// ============================================================
static void* saved_mboot_ptr = NULL;

void kernel_main(uint64_t magic, void* mboot_ptr) {
    (void)magic;
    saved_mboot_ptr = mboot_ptr;
    init_screen();
    clear_screen();

    printf("[INIT] Global Descriptor Table...\n"); init_gdt();
    printf("[INIT] Interrupt Descriptor Table...\n"); init_idt();
    printf("[INIT] Interrupt Service Routines...\n"); init_isr();
    printf("[INIT] Interrupt Requests...\n"); init_irq();
    printf("[INIT] Serial Port...\n"); init_serial();

    uint64_t mem_total = 0;
    if (mboot_ptr) {
        if (magic == 0x2BADB002) {
            // Multiboot v1 info
            uint32_t *mb = (uint32_t*)mboot_ptr;
            if (mb[0] & 0x1) {
                mem_total = (uint64_t)(mb[2] + 1024) * 1024;
            }
        } else if (magic == 0x36d76289) {
            // Multiboot v2 info
            uint8_t* tag = (uint8_t*)mboot_ptr + 8;
            for (;;) {
                uint32_t type = *(uint32_t*)tag;
                uint32_t size = *(uint32_t*)(tag + 4);
                if (type == 0) break;
                if (type == 4) {
                    uint32_t mem_lower = *(uint32_t*)(tag + 8);
                    uint32_t mem_upper = *(uint32_t*)(tag + 12);
                    mem_total = (uint64_t)(mem_lower + mem_upper) * 1024;
                }

                tag += (size + 7) & ~7;
            }
        }
        if (mem_total > 0) {
            printf("[INIT] Memory detected: %llu MB\n", mem_total / (1024*1024));
        }
    }
    if (mem_total == 0) {
        mem_total = 256 * 1024 * 1024;
        printf("[INIT] Memory detection failed, using %llu MB\n", mem_total / (1024*1024));
    }
    printf("[INIT] Physical Memory Manager...\n"); init_memory(mem_total);

    printf("[INIT] Paging...\n"); init_paging();

    nyxfetch();
    for (volatile long i = 0; i < 200000000; i++);
    clear_screen();
    printf("[INIT] APIC...\n"); init_apic();
    printf("[INIT] Kernel Heap...\n"); init_heap();
    printf("[INIT] Slab Allocator...\n"); slab_init_all();
    printf("[INIT] SMP...\n"); smp_init();
    printf("[INIT] VBE (Bochs)...\n"); vbe_init();
    printf("[INIT] VBE mode 1024x768x32...\n");
    if (vbe_set_mode(1024, 768, 32) == 0) {
        fb_init(vbe_get_width(), vbe_get_height(), vbe_get_bpp(), vbe_get_lfb());
        bootsplash_init();
        printf("[INIT] Framebuffer: %dx%dx%d at 0x%llx\n",
               vbe_get_width(), vbe_get_height(), vbe_get_bpp(), vbe_get_lfb());
    } else {
        printf("[INIT] VBE mode set failed, staying in text mode\n");
    }
    bootsplash_update(1, 23, "Setting up exception stacks...");
    printf("[INIT] IST stacks...\n");
    void* ist_pages = kmalloc(IST_STACK_SIZE * 2);
    if (ist_pages) {
        uint64_t df_stack = (uint64_t)ist_pages + IST_STACK_SIZE;
        uint64_t nmi_stack = (uint64_t)ist_pages + IST_STACK_SIZE * 2;
        tss_set_ist(IST_DOUBLE_FAULT, df_stack + KERNEL_BASE);
        tss_set_ist(2, nmi_stack + KERNEL_BASE);
        printf("[INIT] IST1 (double fault) at 0x%lx, IST2 (NMI) at 0x%lx\n",
               df_stack + KERNEL_BASE, nmi_stack + KERNEL_BASE);
    }
    printf("[INIT] Timer (1000 Hz, interrupt-driven)...\n"); init_timer(1000);
    bootsplash_update(2, 23, "Initializing timer...");
    printf("[INIT] Keyboard (interrupt-driven)...\n");
    bootsplash_update(3, 23, "Initializing keyboard...");
    init_keyboard();
    set_keyboard_layout(1);
    printf("[INIT] Process Manager...\n"); init_process();
    bootsplash_update(4, 23, "Starting process manager...");
    printf("[INIT] Creating idle process...\n"); ensure_idle_process();
    bootsplash_update(5, 23, "Creating idle process...");
    printf("[INIT] System Calls...\n"); init_syscalls();
    bootsplash_update(6, 23, "Initializing system calls...");
    setup_syscall_msrs();
    printf("[INIT] Syscall kernel stack...\n");
    bootsplash_update(7, 23, "Allocating syscall stack...");
    void* syscall_stack = kmalloc(4096);
    if (syscall_stack) {
        // Store the higher-half alias: syscall_entry runs on the user CR3 until it
        // switches, and only the PML4[511] mirror is mapped there — not low identity.
        set_kernel_rsp((uint64_t)syscall_stack + 4096 + KERNEL_BASE);
    }
    printf("[INIT] Virtual File System...\n"); init_vfs();
    bootsplash_update(8, 23, "Mounting virtual file system...");
    printf("[INIT] Loading GRUB modules...\n"); init_load_modules();
    bootsplash_update(9, 23, "Loading kernel modules...");
    printf("[INIT] EXT2 Filesystem...\n"); init_ext2();
    bootsplash_update(10, 23, "Initializing EXT2 filesystem...");
    printf("[INIT] Network Stack...\n"); init_net();
    bootsplash_update(11, 23, "Starting network stack...");
    printf("[INIT] TCP...\n"); tcp_init();
    bootsplash_update(12, 23, "Initializing TCP...");
    init_background_tasks();
    bootsplash_update(13, 23, "Registering background tasks...");

    printf("[INIT] PS/2 Mouse...\n"); mouse_init();
    bootsplash_update(14, 23, "Initializing PS/2 mouse...");
    printf("[INIT] PC Speaker...\n"); speaker_init();
    bootsplash_update(15, 23, "Initializing PC speaker...");
    printf("[INIT] Sound Blaster 16...\n");
    if (sb16_init() == 0) {
        printf("[INIT] SB16 initialized at 0x%x, IRQ %d\n", SB16_BASE_PORT, SB16_IRQ);
        printf("[INIT] Enabling SB16 speaker...\n");
        sb16_write_dsp(SB16_CMD_ENABLE_SPEAKER);
    } else {
        printf("[INIT] SB16 not detected (QEMU -soundhw sb16 required)\n");
    }
    bootsplash_update(16, 23, "Sound subsystem ready");
    printf("[INIT] RTC...\n"); rtc_init();
    bootsplash_update(17, 23, "Reading real-time clock...");
    printf("[INIT] Registering IRQ handlers...\n");
    bootsplash_update(18, 23, "Installing interrupt handlers...");
    irq_install_handler(0, NULL); // Timer (handled by irq_scheduler_tick)
    irq_install_handler(1, keyboard_irq_handler);
    irq_install_handler(5, sb16_irq_handler);
    irq_install_handler(12, mouse_irq_handler);
    printf("[INIT] Unmasking IRQs...\n");
    bootsplash_update(19, 23, "Unmasking interrupts...");
    // The PIT (ISA IRQ0) is delivered on I/O APIC pin 2, not pin 0: QEMU's ACPI
    // MADT publishes an interrupt-source override (ISA IRQ0 → GSI 2). Unmasking
    // pin 0 therefore does nothing; we redirect pin 2 → vector 32 (the irq0 stub)
    // and unmask it, so tick_count advances and sleep()/uptime come alive. The
    // #UD that used to crash the first tick was the SAVE_REGS-before-CR3-switch
    // bug in irq_common, fixed in v5.7.5 — ticks are safe now.
    ioapic_redirect_irq(2, 32, (uint8_t)apic_get_id()); // PIT → vector 32 via pin 2
    ioapic_unmask_irq(2);
    irq_unmask(1); // Keyboard
    irq_unmask(5); // SB16
    irq_unmask(12); // Mouse
    printf("[INIT] Enabling interrupts (sti)...\n");
    bootsplash_update(20, 23, "Enabling interrupts...");
    enable_interrupts();
    kernel_initialized = true;
    printf("\n[READY] NyxOS initialized successfully.\n\n");

    // Load initramfs and boot init
    printf("[INIT] Loading initramfs...\n");
    bootsplash_update(21, 23, "Loading initramfs...");
    serial_puts("[DEBUG] Before initramfs_load\n");
    if (initramfs_load() == 0) {
        initramfs_boot();

        printf("[INIT] initramfs ready. Use 'exec <file>' to run an ELF binary.\n");

        // Try to auto-exec /init.elf from initramfs
        printf("[INIT] Looking for /init.elf...\n");
        int init_fd = vfs_open("/init.elf", 0, 0);
        if (init_fd >= 0) {
            uint32_t init_size = vfs_fsize(init_fd);
            uint8_t* init_data = vfs_fdata(init_fd);
            if (init_data && init_size > 0) {
                uint8_t* copy = (uint8_t*)kmalloc(init_size);
                if (copy) {
                    memcpy_asm(copy, init_data, init_size);
                    vfs_close(init_fd);
                    printf("[INIT] Loading /init.elf (%u bytes)...\n", init_size);
                    if (elf_validate(copy, init_size)) {
                        process_t* init_proc = NULL;
                        if (elf_load(copy, init_size, &init_proc) == 0) {
                            proc_set_comm(init_proc, "/init.elf");   // "init" not "elf"
                            init_proc->cmdline[0] = '\0';
                            strncpy(init_proc->cmdline, "/init.elf", sizeof(init_proc->cmdline) - 1);
                            printf("[INIT] Registered init PID=%u (scheduler will start it)\n", init_proc->pid);
                        } else {
                            printf("[INIT] ELF load failed\n");
                        }
                    } else {
                        printf("[INIT] Not a valid ELF\n");
                    }
                    kfree(copy);
                }
            } else {
                vfs_close(init_fd);
                printf("[INIT] /init is empty\n");
            }
        } else {
                printf("[INIT] /init.elf not found\n");
        }
    }

    // Auto-mount EXT2 if available
    bootsplash_update(22, 23, "Probing for EXT2 filesystem...");
    printf("[EXT2] Probing ATA drive for EXT2 filesystem...\n");
    if (ata_init() == 0 && ext2_mount(0, 0) == 0) {
        printf("[EXT2] Found: %u blocks, %u inodes, block size %u\n",
               ext2_fs.sb.total_blocks, ext2_fs.sb.total_inodes, ext2_fs.block_size);
        if (vfs_mount("/mnt", FS_TYPE_EXT2, NULL) == 0) {
            printf("[EXT2] Mounted at /mnt. Use 'ls /mnt' etc.\n");
            const char* test_data = "Hello from NyxOS EXT2 write!\n";
            if (ext2_write_file("/os-test.txt", test_data, strlen(test_data)) > 0) {
                char readbuf[64];
                if (ext2_read_file("/os-test.txt", readbuf, sizeof(readbuf)-1) > 0) {
                    readbuf[sizeof(readbuf)-1] = '\0';
                    printf("[EXT2] Write test OK\n");
                }
            }
        }
    } else {
        printf("[EXT2] No EXT2 filesystem found.\n");
    }

    bootsplash_update(23, 23, "Launching desktop...");
    if (vbe_get_lfb()) {
        bootsplash_clear();
        printf("[AUTH] Setting up user accounts...\n");
        auth_setup();
        printf("[LOGIN] Starting login screen...\n");
        if (!login_screen()) {
            printf("[LOGIN] Login failed, rebooting...\n");
            outb(0x64, 0xFE);
            while(1) { __asm__ volatile("hlt"); }
        }
        printf("[LOGIN] Login successful, starting desktop...\n");
        printf("[DESKTOP] Launching NyxOS Desktop...\n");
        // Register compositor as scheduler process so scheduler manages it correctly
        process_t* comp_proc = create_process("compositor", compositor_run, 0);
        if (comp_proc) {
            comp_proc->sched_weight = SCHED_WEIGHT_GUI;   // GUI gets priority over jobs
            for (int i = 0; i < process_count; i++) {
                if (process_table[i] == comp_proc) { current_idx = i; break; }
            }
        }
        compositor_init();
        serial_puts("[COMP] calling compositor_run\n");
        compositor_run();
        printf("[DESKTOP] Compositor exited, rebooting...\n");
        outb(0x64, 0xFE);
    } else {
        printf("[SHELL] No VBE framebuffer, launching text shell.\n");
        launch_shell();
    }
    while(1) { __asm__ volatile("hlt"); }
}

// ============================================================
// nyxfetch
// ============================================================
void nyxfetch(void) {
    clear_screen();

    /* The GUI terminal colorizes via ANSI SGR; the boot/VGA console does not
     * parse escapes (they would render as garbage). So emit ANSI only once the
     * compositor is up, and tint the boot splash with set_terminal_color(). */
    int gui = compositor_is_running();
    const char* LOGO_C = gui ? "\x1b[95m" : "";   /* fox: NyxOS purple      */
    const char* KEY_C  = gui ? "\x1b[96m" : "";   /* field labels: cyan     */
    const char* ACC_C  = gui ? "\x1b[95m" : "";   /* accents: user@host     */
    const char* RST_C  = gui ? "\x1b[0m"  : "";
    set_terminal_color(vga_entry_color(VGA_LIGHT_MAGENTA, VGA_BLACK));

    /* The Nyx fox, rendered as an ASCII shade ramp (" .:o#"). Pure ASCII on
     * purpose: the terminal write path drops bytes >= 0x80, so block glyphs
     * would vanish. 27 columns wide, 14 rows tall. */
    static const char* fox[] = {
        "       .:::o:o#:.          ",
        "    .:oo.. :o.             ",
        "  :oo:.oo.o:               ",
        " .#o:.   :.                ",
        " #:::....:                 ",
        "o#::. . o.                 ",
        "o#.o:   :o                 ",
        "o###o   o#                 ",
        ":#oo::  .oo.               ",
        " o#o:o..  :o:.             ",
        "  o#ooo::.:::#::        .:.",
        "  .:o#oo::.: ..:oo::.o:#o. ",
        "     :o#####:#::o:.::o:    ",
        "        .::oo####::.       ",
    };
    const int FOX_H = 14;

    /* ---- Gather system facts (all real; nothing hardcoded) ---- */
    char cpu_brand[49] = "Unknown";
    uint32_t e, b, c, d;
    __asm__ volatile("cpuid" : "=a"(e), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000));
    if (e >= 0x80000004) {
        uint32_t* bp = (uint32_t*)cpu_brand;
        for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            __asm__ volatile("cpuid" : "=a"(e), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf));
            *bp++ = e; *bp++ = b; *bp++ = c; *bp++ = d;
        }
        cpu_brand[48] = '\0';
        char* p = cpu_brand + 47;
        while (p >= cpu_brand && *p == ' ') *p-- = '\0';
        if (cpu_brand[0] == '\0') strcpy(cpu_brand, "Unknown");
    }

    uint32_t total_sec = get_ticks() / 1000;
    uint32_t days = total_sec / 86400;
    uint32_t hours = (total_sec % 86400) / 3600;
    uint32_t mins = (total_sec % 3600) / 60;
    uint32_t secs = total_sec % 60;
    char uptime[32];
    if (days > 0)
        snprintf(uptime, sizeof(uptime), "%u days %02u:%02u:%02u", days, hours, mins, secs);
    else
        snprintf(uptime, sizeof(uptime), "%02u:%02u:%02u", hours, mins, secs);

    uint64_t free_mem = memory_total > memory_used ? memory_total - memory_used : 0;
    uint32_t mem_pct = memory_total > 0 ? (uint32_t)((memory_used * 100) / memory_total) : 0;

    rtc_time_t rtc;
    rtc_read_time(&rtc);

    uint32_t fb_w = fb_get_width(), fb_h = fb_get_height();
    char res_str[24];
    if (fb_w && fb_h)
        snprintf(res_str, sizeof(res_str), "%u x %u", fb_w, fb_h);
    else
        strcpy(res_str, "VGA text (80x25)");

    char ip_str[16] = "Not connected";
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && net_interfaces[i].ip) {
            uint32_t ip = net_interfaces[i].ip;
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                     ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF);
            break;
        }
    }

    char disk_str[48] = "Not mounted";
    if (ext2_fs.block_size > 0) {
        uint64_t total_kb = (uint64_t)ext2_fs.sb.total_blocks * ext2_fs.block_size / 1024;
        uint64_t free_kb = (uint64_t)ext2_fs.sb.free_blocks * ext2_fs.block_size / 1024;
        snprintf(disk_str, sizeof(disk_str), "%llu KiB / %llu KiB", free_kb, total_kb);
    }

    /* ---- Build the info column (index-aligned with the fox rows) ---- */
    char info[16][96];
    int n = 0;
    snprintf(info[n++], 96, "%snyx%s@%snyxos%s", ACC_C, RST_C, ACC_C, RST_C);
    snprintf(info[n++], 96, "-----------------");
    snprintf(info[n++], 96, "%sOS:%s         %s x86_64", KEY_C, RST_C, KERNEL_NAME);
    snprintf(info[n++], 96, "%sHost:%s       QEMU Standard PC", KEY_C, RST_C);
    snprintf(info[n++], 96, "%sKernel:%s     %s %s (%s)", KEY_C, RST_C, KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
    snprintf(info[n++], 96, "%sUptime:%s     %s", KEY_C, RST_C, uptime);
    snprintf(info[n++], 96, "%sResolution:%s %s", KEY_C, RST_C, res_str);
    snprintf(info[n++], 96, "%sCPU:%s        %s (%d)", KEY_C, RST_C, cpu_brand, cpu_count);
    snprintf(info[n++], 96, "%sMemory:%s     %llu / %llu MiB (%u%%)", KEY_C, RST_C,
             free_mem / (1024*1024), memory_total / (1024*1024), mem_pct);
    snprintf(info[n++], 96, "%sProcesses:%s  %d", KEY_C, RST_C, process_count);
    snprintf(info[n++], 96, "%sDisk:%s       %s", KEY_C, RST_C, disk_str);
    snprintf(info[n++], 96, "%sNetwork:%s    %s", KEY_C, RST_C, ip_str);
    snprintf(info[n++], 96, "%sShell:%s      NyxOS Terminal", KEY_C, RST_C);
    snprintf(info[n++], 96, "%sTime:%s       %u-%02u-%02u %02u:%02u:%02u", KEY_C, RST_C,
             (unsigned int)rtc.year, (unsigned int)rtc.month, (unsigned int)rtc.day,
             (unsigned int)rtc.hour, (unsigned int)rtc.minute, (unsigned int)rtc.second);

    /* ---- Render: fox on the left, facts on the right, line by line ---- */
    static const char* blank_row = "                           "; /* 27 spaces */
    int rows = n > FOX_H ? n : FOX_H;
    for (int i = 0; i < rows; i++) {
        const char* l = (i < FOX_H) ? fox[i] : blank_row;
        const char* r = (i < n)     ? info[i] : "";
        printf("%s%s%s  %s\n", LOGO_C, l, RST_C, r);
    }
    set_terminal_color(vga_entry_color(VGA_LIGHT_GREEN, VGA_BLACK));
}

// ============================================================
// launch_shell
// ============================================================
void launch_shell(void) {
    char cmd_line[256];
    int idx = 0;

    while (1) {
        kernel_poll_net();
        run_background_tasks();
        set_terminal_color(vga_entry_color(VGA_LIGHT_GREEN, VGA_BLACK));
        printf("nyx:%s$ ", vfs_getcwd());

        while (1) {
            char c = getchar();

            if (c == '\n' || c == '\r') {
                cmd_line[idx] = '\0';
                putchar('\n');
                break;
            } else if (c == '\b' || c == 0x7F) {
                if (idx > 0) {
                    idx--;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
            } else if (c == '\t') {
                if (idx > 0) {
                    char partial[256];
                    memcpy(partial, cmd_line, idx);
                    partial[idx] = '\0';
                    int has_space = 0;
                    for (int i = 0; i < idx; i++) {
                        if (cmd_line[i] == ' ') { has_space = 1; break; }
                    }
                    if (!has_space) {
                        int match_count = 0, match_idx = -1;
                        for (int i = 0; commands[i].name != NULL; i++) {
                            if (commands[i].hidden) continue;
                            if (strncmp(commands[i].name, partial, idx) == 0) {
                                match_count++;
                                match_idx = i;
                            }
                        }
                        if (match_count == 1) {
                            int len = strlen(commands[match_idx].name);
                            for (int j = idx; j < len && j < 255; j++) {
                                cmd_line[idx++] = commands[match_idx].name[j];
                                putchar(commands[match_idx].name[j]);
                            }
                        } else if (match_count > 1) {
                            putchar('\n');
                            for (int i = 0; commands[i].name != NULL; i++) {
                                if (commands[i].hidden) continue;
                                if (strncmp(commands[i].name, partial, idx) == 0) {
                                    printf("  %s", commands[i].name);
                                }
                            }
                            putchar('\n');
                        }
                    }
                }
            } else {
                if (idx < 255) {
                    cmd_line[idx++] = c;
                    putchar(c);
                }
            }
        }

        if (strlen(cmd_line) > 0) {
            add_history(cmd_line);
            char* pipe_pos_ptr = strchr(cmd_line, '|');
            if (pipe_pos_ptr) {
                *pipe_pos_ptr = '\0';
                char* left_str = cmd_line;
                char* right_str = pipe_pos_ptr + 1;
                while (*right_str == ' ') right_str++;
                // Parse left command
                char* argv_left[10];
                char expanded_left[10][256];
                int argc_left = 0;
                char* token = strtok(left_str, " ");
                while (token != NULL && argc_left < 10) {
                    if (token[0] == '$' && strlen(token) > 1) {
                        char varname[64];
                        strncpy(varname, token + 1, 63);
                        varname[63] = '\0';
                        int found = 0;
                        for (int e = 0; e < env_count; e++) {
                            char *eq = strchr(env_vars[e], '=');
                            if (eq && strncmp(env_vars[e], varname, eq - env_vars[e]) == 0 && (int)strlen(varname) == (int)(eq - env_vars[e])) {
                                strncpy(expanded_left[argc_left], eq + 1, 255);
                                expanded_left[argc_left][255] = '\0';
                                argv_left[argc_left] = expanded_left[argc_left];
                                found = 1;
                                break;
                            }
                        }
                        if (!found) argv_left[argc_left] = token;
                    } else {
                        argv_left[argc_left] = token;
                    }
                    argc_left++;
                    token = strtok(NULL, " ");
                }
                if (argc_left > 0) {
                    pipe_start();
                    int cmd_found = 0;
                    for (int i = 0; commands[i].name != NULL; i++) {
                        if (strcmp(argv_left[0], commands[i].name) == 0) {
                            commands[i].func(argc_left, argv_left);
                            cmd_found = 1;
                            break;
                        }
                    }
                    if (!cmd_found) {
                        printf("Command not found: %s\n", argv_left[0]);
                    }
                    int pipe_len = pipe_stop();
                    if (cmd_found && pipe_len > 0) {
                        int fd = vfs_open("/tmp/pipe", 0, 0);
                        if (fd < 0) fd = vfs_open("/tmp/pipe", 1, 0);
                        if (fd >= 0) {
                            vfs_write(fd, pipe_get_data(), pipe_len);
                            vfs_close(fd);
                        }
                    }
                    // Parse and execute right command with /tmp/pipe as file arg
                    char* argv_right[10];
                    char expanded_right[10][256];
                    int argc_right = 0;
                    char right_copy[256];
                    strncpy(right_copy, right_str, 255);
                    right_copy[255] = '\0';
                    token = strtok(right_copy, " ");
                    while (token != NULL && argc_right < 9) {
                        if (token[0] == '$' && strlen(token) > 1) {
                            char varname[64];
                            strncpy(varname, token + 1, 63);
                            varname[63] = '\0';
                            int found = 0;
                            for (int e = 0; e < env_count; e++) {
                                char *eq = strchr(env_vars[e], '=');
                                if (eq && strncmp(env_vars[e], varname, eq - env_vars[e]) == 0 && (int)strlen(varname) == (int)(eq - env_vars[e])) {
                                    strncpy(expanded_right[argc_right], eq + 1, 255);
                                    expanded_right[argc_right][255] = '\0';
                                    argv_right[argc_right] = expanded_right[argc_right];
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found) argv_right[argc_right] = token;
                        } else {
                            argv_right[argc_right] = token;
                        }
                        argc_right++;
                        token = strtok(NULL, " ");
                    }
                    if (argc_right > 0) {
                        argv_right[argc_right] = "/tmp/pipe";
                        argc_right++;
                        set_terminal_color(vga_entry_color(VGA_LIGHT_CYAN, VGA_BLACK));
                        int cmd2_found = 0;
                        for (int i = 0; commands[i].name != NULL; i++) {
                            if (strcmp(argv_right[0], commands[i].name) == 0) {
                                commands[i].func(argc_right, argv_right);
                                cmd2_found = 1;
                                break;
                            }
                        }
                        if (!cmd2_found) {
                            set_terminal_color(vga_entry_color(VGA_LIGHT_RED, VGA_BLACK));
                            printf("Command not found: %s\n", argv_right[0]);
                        }
                    }
                }
            } else {
                char* argv[10];
                char expanded[10][256];
                int argc = 0;
                char* token = strtok(cmd_line, " ");
                while (token != NULL && argc < 10) {
                    if (token[0] == '$' && strlen(token) > 1) {
                        char varname[64];
                        strncpy(varname, token + 1, 63);
                        varname[63] = '\0';
                        int found = 0;
                        for (int e = 0; e < env_count; e++) {
                            char *eq = strchr(env_vars[e], '=');
                            if (eq && strncmp(env_vars[e], varname, eq - env_vars[e]) == 0 && (int)strlen(varname) == (int)(eq - env_vars[e])) {
                                strncpy(expanded[argc], eq + 1, 255);
                                expanded[argc][255] = '\0';
                                argv[argc] = expanded[argc];
                                found = 1;
                                break;
                            }
                        }
                        if (!found) argv[argc] = token;
                    } else {
                        argv[argc] = token;
                    }
                    argc++;
                    token = strtok(NULL, " ");
                }
                if (argc > 0) {
                    bool found = false;
                    for (int i = 0; commands[i].name != NULL; i++) {
                        if (strcmp(argv[0], commands[i].name) == 0) {
                            set_terminal_color(vga_entry_color(VGA_LIGHT_CYAN, VGA_BLACK));
                            commands[i].func(argc, argv);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        set_terminal_color(vga_entry_color(VGA_LIGHT_RED, VGA_BLACK));
                        printf("Command not found: %s\n", argv[0]);
                    }
                }
            }
        }

        idx = 0;
    }
}

// ============================================================
// STUBS PARA MÓDULOS
// ============================================================
int net_create_raw_socket(void) { return -1; }
void kernel_thread_create(const char *name, void (*func)(void), void *arg) {
    (void)name; (void)func; (void)arg;
}
void* hook_irq(int irq, void (*handler)(void)) {
    (void)irq; (void)handler; return NULL;
}
void* allocate_remote_memory(process_t *proc, size_t size) {
    (void)proc; (void)size; return NULL;
}
void write_remote_memory(process_t *proc, void *dest, const void *src, size_t len) {
    (void)proc; (void)dest; (void)src; (void)len;
}
uint32_t get_kernel_symbol(const char *name) {
    (void)name; return 0;
}
void remote_syscall(process_t *proc, uint32_t func, void *arg1, void *arg2) {
    (void)proc; (void)func; (void)arg1; (void)arg2;
}
void init_raw_socket(void) {}
void load_wifi_firmware(void) {}

// ============================================================
// FUNCIONES AUXILIARES ADICIONALES
// ============================================================

// ============================================================
// IMPLEMENTACIONES DE snprintf Y strcasestr
// ============================================================
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = 0;
    char *p = buf;
    while (*fmt && written < (int)size - 1) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == '%') {
                *p++ = '%'; written++; fmt++;
                continue;
            }
            int width = 0, precision = -1;
            while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
            if (*fmt == '.') { fmt++; precision = 0; while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt - '0'); fmt++; } }
            int long_flag = 0;
            while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't') {
                if (*fmt == 'l') long_flag = 1;
                fmt++;
            }
            if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                if (written < (int)size - 1) { *p++ = c; written++; }
                fmt++;
            } else if (*fmt == 's') {
                const char *s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s && written < (int)size - 1) { *p++ = *s++; written++; }
                fmt++;
            } else if (*fmt == 'd' || *fmt == 'i') {
                long val = long_flag ? va_arg(args, long) : (long)va_arg(args, int);
                char tmp[24];
                int neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                itoa((int)val, tmp, 10);
                if (neg) { if (written < (int)size - 1) { *p++ = '-'; written++; } }
                char *t = tmp;
                int tlen = 0; while (t[tlen]) tlen++;
                if (precision > tlen) {
                    int pad = precision - tlen;
                    while (pad-- > 0 && written < (int)size - 1) { *p++ = '0'; written++; }
                }
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else if (*fmt == 'u') {
                unsigned long val = long_flag ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                char tmp[24];
                itoa((int)val, tmp, 10);
                char *t = tmp;
                int tlen = 0; while (t[tlen]) tlen++;
                if (width > tlen) {
                    int pad = width - tlen;
                    while (pad-- > 0 && written < (int)size - 1) { *p++ = '0'; written++; }
                }
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else if (*fmt == 'x' || *fmt == 'X') {
                unsigned long val = long_flag ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                char tmp[24];
                itoa((int)val, tmp, 16);
                char *t = tmp;
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else if (*fmt == 'p') {
                void *val = va_arg(args, void*);
                char tmp[24];
                itoa((unsigned long)val, tmp, 16);
                char *t = tmp;
                if (written < (int)size - 1) { *p++ = '0'; written++; }
                if (written < (int)size - 1) { *p++ = 'x'; written++; }
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else {
                if (written < (int)size - 1) { *p++ = '%'; written++; }
                if (*fmt && written < (int)size - 1) { *p++ = *fmt++; written++; }
            }
        } else {
            *p++ = *fmt++; written++;
        }
    }
    *p = '\0';
    va_end(args);
    return written;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

void init_load_modules(void) {
    if (!saved_mboot_ptr) return;
    uint32_t* mb = (uint32_t*)saved_mboot_ptr;
    if (!(mb[0] & 8)) return;
    uint32_t mods_count = mb[5];
    uint32_t mods_addr = mb[6];
    printf("[MODULES] %d module(s) at %x\n", mods_count, mods_addr);
    uint32_t* mod_entry = (uint32_t*)(uintptr_t)mods_addr;
    for (uint32_t i = 0; i < mods_count; i++) {
        uint32_t mod_start = mod_entry[0];
        uint32_t mod_end = mod_entry[1];
        uint32_t mod_str = mod_entry[2];
        uint32_t mod_size = mod_end - mod_start;
        const char* name = (const char*)(uintptr_t)mod_str;
        char path[64];
        if (name && *name) {
            snprintf(path, sizeof(path), "/%s", name);
        } else {
            snprintf(path, sizeof(path), "/boot/module%d", i);
        }
        vfs_create_from_mem(path, (uint8_t*)(uintptr_t)mod_start, mod_size);
        mod_entry += 4;
    }
}

char* strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && ((*h|0x20) == (*n|0x20))) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}