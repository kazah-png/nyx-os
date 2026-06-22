// ============================================================
// kernel.c - Núcleo principal de NyxOS v2.0.0
// ============================================================
#include "kernel.h"

// Variables globales del kernel
process_t* process_table[MAX_PROCESSES];
void* syscall_table[SYS_TABLE_SIZE];
net_iface_t net_interfaces[8];
mount_t mount_points[MAX_MOUNTS];
uint32_t memory_total = 0;
uint32_t memory_used = 0;
uint32_t tick_count = 0;
bool kernel_initialized = false;
int process_count = 0;

// DOOM WAD direct memory access
uint8_t* doom_wad_data = NULL;
uint32_t doom_wad_size = 0;

// ============================================================
// Conversión de hexadecimal a bytes
// ============================================================
// ============================================================
// Utilidades para el comando date
// ============================================================
static const int month_days[] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
};

static inline int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static void ticks_to_date(uint32_t seconds, int* year, int* month, int* day,
                          int* hour, int* minute, int* second) {
    const int epoch_year = 2024;
    uint32_t remaining = seconds;
    *year = epoch_year;
    while (1) {
        int days_in_year = is_leap_year(*year) ? 366 : 365;
        if (remaining < (uint32_t)(days_in_year * 86400)) break;
        remaining -= days_in_year * 86400;
        (*year)++;
    }
    int leap = is_leap_year(*year);
    for (*month = 0; *month < 12; (*month)++) {
        int mdays = month_days[*month];
        if (*month >= 2 && leap) mdays += 1;
        if (remaining < (uint32_t)(mdays * 86400)) break;
        remaining -= mdays * 86400;
    }
    *month += 1;
    *day = remaining / 86400 + 1;
    remaining %= 86400;
    *hour = remaining / 3600;
    remaining %= 3600;
    *minute = remaining / 60;
    *second = remaining % 60;
}

static void print_d2(int n) {
    putchar('0' + (n / 10) % 10);
    putchar('0' + n % 10);
}

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
static void cmd_mem(int argc, char** argv);
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
static void cmd_history(int argc, char** argv);
static void cmd_diff(int argc, char** argv);
static void cmd_doom(int argc, char** argv);
void cmd_doomtest(int argc, char** argv);
static void cmd_mode(int argc, char** argv);
static void cmd_gui(int argc, char** argv);
static void cmd_fonttest(int argc, char** argv);

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
    {"mem",       cmd_mem,       "Show memory usage", false},

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
    {"ping",      cmd_ping,      "Ping a host: ping <ip>", false},
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
    {"doom",      cmd_doom,      "Launch DOOM game", false},
    {"doomtest",  cmd_doomtest,  "Test DOOM WAD loading", false},
    {"mode",      cmd_mode,      "Set VBE video mode: mode <width> <height> <bpp>", false},
    {"gui",       cmd_gui,       "Launch GUI demo with mouse", false},
    {"fonttest",  cmd_fonttest,  "Test font rendering on framebuffer", false},
    {NULL, NULL, NULL, false}
};

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
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) fd = vfs_open(argv[1], 1, 0);
    if (fd < 0) { printf("write: cannot create '%s'\n", argv[1]); return; }
    for (int i = 2; i < argc; i++) {
        vfs_write(fd, argv[i], strlen(argv[i]));
        if (i < argc - 1) vfs_write(fd, " ", 1);
    }
    vfs_write(fd, "\n", 1);
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

static void cmd_doom(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Launching DOOM...\n");
    printf("Controls: WASD=move, Space=fire, Enter=menu, Esc=quit\n");
    run_doom();
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
    uint8_t* ptr = (uint8_t*)addr;
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
    uint32_t seconds = tick_count / 1000;
    int year, month, day, hour, minute, second;
    ticks_to_date(seconds, &year, &month, &day, &hour, &minute, &second);
    printf("20");
    print_d2(year % 100);
    putchar('-');
    print_d2(month);
    putchar('-');
    print_d2(day);
    putchar(' ');
    print_d2(hour);
    putchar(':');
    print_d2(minute);
    putchar(':');
    print_d2(second);
    putchar('\n');
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
            printf("%s  HWaddr %02x:%02x:%02x:%02x:%02x:%02x  IP %d.%d.%d.%d\n",
                net_interfaces[i].name,
                net_interfaces[i].mac[0], net_interfaces[i].mac[1],
                net_interfaces[i].mac[2], net_interfaces[i].mac[3],
                net_interfaces[i].mac[4], net_interfaces[i].mac[5],
                (net_interfaces[i].ip >> 24) & 0xFF,
                (net_interfaces[i].ip >> 16) & 0xFF,
                (net_interfaces[i].ip >> 8) & 0xFF,
                net_interfaces[i].ip & 0xFF);
        }
    }
}

static void cmd_ping(int argc, char** argv) {
    if (argc < 2) { printf("Usage: ping <ip>\n"); return; }
    uint32_t ip = 0;
    int seg = 0;
    char* p = argv[1];
    while (*p) {
        if (*p == '.') { seg++; }
        else if (*p >= '0' && *p <= '9') {
            uint8_t val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            if (seg == 0) ip |= (uint32_t)val << 24;
            else if (seg == 1) ip |= (uint32_t)val << 16;
            else if (seg == 2) ip |= (uint32_t)val << 8;
            else if (seg == 3) ip |= val;
            continue;
        }
        p++;
    }
    printf("PING %d.%d.%d.%d...\n", (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF);
    int iface_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            iface_idx = i; break;
        }
    }
    if (iface_idx < 0) { printf("No network interface available\n"); return; }
    if (icmp_ping(ip, 3, iface_idx)) printf("Reply received!\n");
    else printf("No reply (host unreachable or timeout)\n");
}

static void cmd_kill(int argc, char** argv) {
    (void)argc; (void)argv;
    if (argc < 2) { printf("Usage: kill <pid>\n"); return; }
    uint32_t pid = (uint32_t)atoi(argv[1]);
    process_t* p = find_process(pid);
    if (!p) { printf("kill: process %d not found\n", pid); return; }
    p->state = 0;
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
    uint32_t cr0, cr2, cr3;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    printf("CR0=0x%x CR2=0x%x CR3=0x%x\n", cr0, cr2, cr3);
    while(1) { __asm__ volatile("hlt"); }
}

// ============================================================
// kernel_main
// ============================================================
static void* saved_mboot_ptr = NULL;

void kernel_main(uint32_t magic, void* mboot_ptr) {
    (void)magic;
    saved_mboot_ptr = mboot_ptr;
    init_screen();
    clear_screen();

    nyxfetch();

    for (volatile long i = 0; i < 200000000; i++);
    clear_screen();

    printf("[INIT] Global Descriptor Table...\n"); init_gdt();
    printf("[INIT] Interrupt Descriptor Table...\n"); init_idt();
    printf("[INIT] Interrupt Service Routines...\n"); init_isr();
    printf("[INIT] Interrupt Requests...\n"); init_irq();
    printf("[INIT] Serial Port...\n"); init_serial();

    uint32_t mem_total = 0;
    if (magic == 0x2BADB002 && mboot_ptr) {
        uint32_t *mb = (uint32_t*)mboot_ptr;
        if (mb[0] & 0x1) {
            mem_total = (mb[2] + 1024) * 1024;
            printf("[INIT] Memory detected: %d MB\n", mem_total / (1024*1024));
        }
    }
    if (mem_total == 0) {
        mem_total = 0x10000000;
        printf("[INIT] Memory detection failed, using %d MB\n", mem_total / (1024*1024));
    }
    printf("[INIT] Physical Memory Manager...\n"); init_memory(mem_total);

    printf("[INIT] Paging...\n"); init_paging();
    printf("[INIT] Kernel Heap...\n"); init_heap();
    printf("[INIT] VBE (Bochs)...\n"); vbe_init();
    printf("[INIT] Timer (1000 Hz, interrupt-driven)...\n"); init_timer(1000);
    printf("[INIT] Keyboard (interrupt-driven)...\n"); 
    init_keyboard();
    set_keyboard_layout(1);
    printf("[INIT] Process Manager...\n"); init_process();
    printf("[INIT] Creating idle process...\n"); ensure_idle_process();
    printf("[INIT] System Calls...\n"); init_syscalls();
    printf("[INIT] Virtual File System...\n"); init_vfs();
    printf("[INIT] Loading GRUB modules...\n"); init_load_modules();
    printf("[INIT] EXT2 Filesystem...\n"); init_ext2();
    printf("[INIT] Network Stack...\n"); init_net();
    init_background_tasks();

    printf("[INIT] PS/2 Mouse...\n"); mouse_init();
    printf("[INIT] Registering IRQ handlers...\n");
    irq_install_handler(1, keyboard_irq_handler);
    irq_install_handler(12, mouse_irq_handler);
    printf("[INIT] Unmasking IRQ0 (timer), IRQ1 (kbd), IRQ12 (mouse)...\n");
    irq_unmask(0);
    irq_unmask(1);
    irq_unmask(12);
    printf("[INIT] Enabling interrupts (sti)...\n");
    enable_interrupts();
    kernel_initialized = true;
    printf("\n[READY] NyxOS initialized successfully.\n\n");
    outb(0x3F8, 'O'); outb(0x3F8, 'K'); outb(0x3F8, '\n');
    launch_shell();
    while(1) { __asm__ volatile("hlt"); }
}

// ============================================================
// nyxfetch
// ============================================================
void nyxfetch(void) {
    clear_screen();
    set_terminal_color(vga_entry_color(VGA_LIGHT_BROWN, VGA_BLACK));
    const char* logo[] = {
        "______          \\'/",
        "      .-'` .    `'-.    -= * =-",
        "    .'  '    .---.  '.    /|\\",
        "   /  '    .'     `'. \\",
        "  ;  '    /          \\|",
        " :  '  _ ;            `",
        ";  :  /(\\ \\",
        "|  .       '.",
        "|  ' /     --'",
        "|  .   '.__\\",
        ";  :       /",
        " ;  .     |            ,",
        "  ;  .    \\           /|",
        "   \\  .    '.       .'/",
        "    '.  '  . `'---'`. `'",
        "      `'-..._____.-'",
        "    N Y X O S",
        "    N I G H T F A L L",
        NULL
    };
    for (int i = 0; logo[i] != NULL; i++) {
        printf("%s\n", logo[i]);
    }
    set_terminal_color(vga_entry_color(VGA_LIGHT_CYAN, VGA_BLACK));
    printf("  -------------------------------------\n");
    printf("  Kernel:     %s %s (%s)\n", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
    printf("  Arch:       x86 (32-bit)\n");
    printf("  Memory:     %d MB total, %d MB free\n",
           memory_total / (1024*1024),
           (memory_total - memory_used) / (1024*1024));
    printf("  Heap:       %d KB\n", KERNEL_HEAP_SIZE / 1024);
    printf("  Paging:     %s\n", (read_cr0() & 0x80000000) ? "Enabled" : "Disabled");
    printf("  Uptime:     %d ticks\n", tick_count);
    printf("  -------------------------------------\n");
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
            if (*fmt == 's') {
                const char *s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s && written < (int)size - 1) { *p++ = *s++; written++; }
                fmt++;
            } else if (*fmt == 'd' || *fmt == 'i') {
                int val = va_arg(args, int);
                char tmp[16];
                itoa(val, tmp, 10);
                char *t = tmp;
                int tlen = 0; while (t[tlen]) tlen++;
                if (precision > tlen) {
                    int pad = precision - tlen;
                    while (pad-- > 0 && written < (int)size - 1) { *p++ = '0'; written++; }
                }
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else if (*fmt == 'u') {
                unsigned int val = va_arg(args, unsigned int);
                char tmp[16];
                itoa((int)val, tmp, 10);
                char *t = tmp;
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else if (*fmt == 'x' || *fmt == 'X') {
                unsigned int val = va_arg(args, unsigned int);
                char tmp[16];
                itoa((int)val, tmp, 16);
                char *t = tmp;
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else {
                *p++ = '%'; written++; fmt++;
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

extern uint8_t _binary_doom1_wad_start[];
extern uint8_t _binary_doom1_wad_end[];
extern uint32_t _binary_doom1_wad_size;

void init_load_modules(void) {
    // Embedded DOOM WAD
    doom_wad_data = _binary_doom1_wad_start;
    doom_wad_size = (uint32_t)&_binary_doom1_wad_size;
    printf("[MODULES] Embedded DOOM WAD at %x (%d bytes)\n",
           (uint32_t)doom_wad_data, doom_wad_size);
    
    if (!saved_mboot_ptr) return;
    uint32_t* mb = (uint32_t*)saved_mboot_ptr;
    if (!(mb[0] & 8)) return;
    uint32_t mods_count = mb[5];
    uint32_t mods_addr = mb[6];
    printf("[MODULES] %d module(s) at %x\n", mods_count, mods_addr);
    uint32_t* mod_entry = (uint32_t*)mods_addr;
    for (uint32_t i = 0; i < mods_count; i++) {
        uint32_t mod_start = mod_entry[0];
        uint32_t mod_end = mod_entry[1];
        uint32_t mod_str = mod_entry[2];
        uint32_t mod_size = mod_end - mod_start;
        const char* name = (const char*)mod_str;
        char path[64];
        if (name && *name) {
            snprintf(path, sizeof(path), "/%s", name);
        } else {
            snprintf(path, sizeof(path), "/boot/module%d", i);
        }
        vfs_create_from_mem(path, (uint8_t*)(uint32_t)mod_start, mod_size);
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