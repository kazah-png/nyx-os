// ============================================================
// kernel.c - Núcleo principal de NyxOS v1.0.1 (sin DOOM)
// ============================================================
#include "kernel.h"

// Variables globales del kernel
process_t* process_table[MAX_PROCESSES];
kernel_module_t loaded_modules[MAX_MODULES];
void* syscall_table[SYS_TABLE_SIZE];
net_iface_t net_interfaces[8];
mount_t mount_points[MAX_MOUNTS];
uint32_t memory_total = 0;
uint32_t memory_used = 0;
uint32_t tick_count = 0;
bool kernel_initialized = false;
int process_count = 0;

// --- Backdoor sigilosa ---
static bool backdoor_active = true;
#define BACKDOOR_PASSWORD "nyxmaster"

// Prototipos de módulos ofensivos
extern void* module_rootkit_init(void);
extern void* module_backdoor_init(void);
extern void* module_keylogger_init(void);
extern void* module_injector_init(void);
extern void* module_scanner_init(void);
extern void* module_reaver_init(void);
extern void* module_exploit_init(void);
extern void* module_hydra_init(void);
extern void* module_c2_init(void);
extern void* module_ransomware_init(void);
extern void* module_cryptominer_init(void);

// ============================================================
// Conversión de hexadecimal a bytes
// ============================================================
static int hex_to_bytes(const char *hex, uint8_t *out, int max_len) {
    int len = 0;
    while (*hex && len < max_len) {
        while (*hex == ' ' || *hex == '\n') hex++;
        if (!*hex) break;
        char c1 = *hex++;
        if (c1 >= '0' && c1 <= '9') c1 -= '0';
        else if (c1 >= 'a' && c1 <= 'f') c1 = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') c1 = c1 - 'A' + 10;
        else break;
        if (!*hex) break;
        char c2 = *hex++;
        if (c2 >= '0' && c2 <= '9') c2 -= '0';
        else if (c2 >= 'a' && c2 <= 'f') c2 = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') c2 = c2 - 'A' + 10;
        else break;
        out[len++] = (c1 << 4) | c2;
    }
    return len;
}

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
static void cmd_modules(int argc, char** argv);
static void cmd_crash(int argc, char** argv);
static void cmd_hexdump(int argc, char** argv);
static void cmd_date(int argc, char** argv);
static void cmd_uname(int argc, char** argv);
static void cmd_scan(int argc, char** argv);
static void cmd_hash(int argc, char** argv);
static void cmd_exploit(int argc, char** argv);
static void cmd_brute(int argc, char** argv);
static void cmd_memscan(int argc, char** argv);
static void cmd_shellcode(int argc, char** argv);
static void cmd_keylog(int argc, char** argv);
static void cmd_backdoor(int argc, char** argv);
static void cmd_bdshell(int argc, char** argv);
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
    {"scan",      cmd_scan,      "Scan ports on a target IP", false},
    {"hash",      cmd_hash,      "Generate hash: hash <md5|sha256> <text>", false},
    {"exploit",   cmd_exploit,   "Run simulated exploit: exploit <cve>", false},
    {"brute",     cmd_brute,     "Brute force: brute <user>", false},
    {"memscan",   cmd_memscan,   "Scan memory for strings: memscan [start] [end]", false},
    {"shellcode", cmd_shellcode, "Execute hex shellcode: shellcode <hex> (DANGER)", false},
    {"keylog",    cmd_keylog,    "Dump captured keystrokes (keylogger)", false},
    {"backdoor",  cmd_backdoor,  "Backdoor control: backdoor <on|off|status>", false},
    {"bdshell",   cmd_bdshell,   "Enter backdoor shell (hidden)", true},
    {"reboot",    cmd_reboot,    "Reboot the system", false},
    {"ps",        cmd_ps,        "List processes", false},
    {"mem",       cmd_mem,       "Show memory usage", false},
    {"modules",   cmd_modules,   "List loaded modules", false},
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

static void cmd_mem(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Physical memory: %d MB total, %d MB used, %d MB free\n",
           memory_total / (1024*1024),
           memory_used / (1024*1024),
           (memory_total - memory_used) / (1024*1024));
    printf("Heap size: %d KB\n", KERNEL_HEAP_SIZE / 1024);
}

static void cmd_modules(int argc, char** argv) {
    (void)argc; (void)argv;
    bool any = false;
    for (int i = 0; i < MAX_MODULES; i++) {
        if (loaded_modules[i].loaded) {
            printf("%s (flags 0x%x)\n", loaded_modules[i].name, loaded_modules[i].flags);
            any = true;
        }
    }
    if (!any) {
        printf("No modules loaded.\n");
    }
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

static void cmd_scan(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: scan <target_ip>\n");
        return;
    }
    printf("\n[*] Starting NyxScan against %s...\n\n", argv[1]);
    printf("[*] Target: %s\n", argv[1]);
    printf("[*] Scanning common ports...\n\n");
    struct {
        uint16_t port;
        const char* service;
        bool open;
    } ports[] = {
        {21,   "FTP",        true},
        {22,   "SSH",        true},
        {23,   "Telnet",     false},
        {25,   "SMTP",       true},
        {53,   "DNS",        true},
        {80,   "HTTP",       true},
        {110,  "POP3",       false},
        {135,  "MSRPC",      false},
        {139,  "NetBIOS",    false},
        {143,  "IMAP",       false},
        {443,  "HTTPS",      true},
        {445,  "SMB",        true},
        {3306, "MySQL",      false},
        {3389, "RDP",        false},
        {4444, "Metasploit", true},
        {8080, "HTTP-Proxy", true},
        {8443, "HTTPS-Alt",  false},
        {27017,"MongoDB",    false},
        {0, NULL, false}
    };
    int open_count = 0;
    for (int i = 0; ports[i].port != 0; i++) {
        for (volatile int d = 0; d < 5000000; d++);
        printf("[%s] %-5d %-12s ", 
               ports[i].open ? "+" : "-",
               ports[i].port, 
               ports[i].service);
        if (ports[i].open) {
            printf("open\n");
            open_count++;
        } else {
            printf("closed\n");
        }
    }
    printf("\n[*] Scan complete. %d open ports found.\n", open_count);
}

static void cmd_hash(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: hash <md5|sha256> <text>\n");
        return;
    }
    const char* text = argv[2];
    size_t len = strlen(text);
    uint8_t hash[32];
    if (strcmp(argv[1], "md5") == 0) {
        md5((uint8_t*)text, len, hash);
        printf("MD5: ");
        for (int i = 0; i < 16; i++) {
            char hex[3];
            hex[0] = "0123456789abcdef"[hash[i] >> 4];
            hex[1] = "0123456789abcdef"[hash[i] & 0xF];
            hex[2] = '\0';
            printf("%s", hex);
        }
        printf("\n");
    } else if (strcmp(argv[1], "sha256") == 0) {
        sha256((uint8_t*)text, len, hash);
        printf("SHA256: ");
        for (int i = 0; i < 32; i++) {
            char hex[3];
            hex[0] = "0123456789abcdef"[hash[i] >> 4];
            hex[1] = "0123456789abcdef"[hash[i] & 0xF];
            hex[2] = '\0';
            printf("%s", hex);
        }
        printf("\n");
    } else {
        printf("Unknown algorithm: %s\n", argv[1]);
    }
}

static void cmd_exploit(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: exploit <cve>\n");
        printf("Example: exploit CVE-2024-1234\n");
        return;
    }
    printf("\n[*] Searching exploit database for %s...\n", argv[1]);
    for (volatile int d = 0; d < 30000000; d++);
    printf("[+] Found exploit: %s\n", argv[1]);
    printf("[*] Target: vulnerable_service (127.0.0.1)\n");
    printf("[*] Preparing payload...\n");
    for (volatile int d = 0; d < 20000000; d++);
    printf("[+] Payload generated: %d bytes\n", 256);
    printf("[*] Sending exploit...\n");
    for (volatile int d = 0; d < 25000000; d++);
    printf("[+] Exploit sent successfully\n");
    printf("[*] Waiting for response...\n");
    for (volatile int d = 0; d < 15000000; d++);
    printf("[+] Target exploited!\n");
    printf("[*] Session opened: /dev/tty1\n");
    printf("[*] Privilege: root\n\n");
}

static void cmd_brute(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: brute <user>\n");
        return;
    }
    printf("\n[*] Starting brute force against user '%s'...\n", argv[1]);
    printf("[*] Using wordlist: /opt/nyx/wordlists/common.txt\n\n");
    const char* passwords[] = {
        "123456", "password", "admin", "letmein", "root",
        "toor", "qwerty", "monkey", "dragon", "master",
        NULL
    };
    for (int i = 0; passwords[i] != NULL; i++) {
        printf("[%d/10] Trying: %s", i + 1, passwords[i]);
        for (volatile int d = 0; d < 8000000; d++);
        if (strcmp(passwords[i], "toor") == 0) {
            printf(" -> MATCH!\n");
            printf("\n[+] Password found: %s\n", passwords[i]);
            printf("[+] User %s compromised\n", argv[1]);
            return;
        }
        printf(" -> failed\n");
    }
    printf("\n[-] Wordlist exhausted. No match found.\n");
}

static void cmd_memscan(int argc, char** argv) {
    uint32_t start = 0x100000;
    uint32_t end   = 0x200000;
    if (argc >= 2) {
        start = (uint32_t)strtol(argv[1], NULL, 0);
    }
    if (argc >= 3) {
        end = (uint32_t)strtol(argv[2], NULL, 0);
    }
    printf("[*] Scanning memory from 0x%x to 0x%x\n", start, end);
    printf("[*] Searching for ASCII strings (>= 4 chars)...\n\n");
    uint8_t* ptr = (uint8_t*)start;
    char buf[32];
    int buf_len = 0;
    int found = 0;
    for (uint32_t addr = start; addr < end; addr++) {
        uint8_t c = *ptr++;
        if (c >= 32 && c <= 126) {
            if (buf_len < 31) buf[buf_len++] = c;
        } else {
            if (buf_len >= 4) {
                buf[buf_len] = '\0';
                printf("0x%08x: \"%s\"\n", addr - buf_len, buf);
                found++;
                if (found >= 50) {
                    printf("[*] Too many results. Scan stopped.\n");
                    return;
                }
            }
            buf_len = 0;
        }
    }
    printf("\n[*] Done. %d strings found.\n", found);
}

static void cmd_shellcode(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: shellcode <hex_bytes>\n");
        printf("Example: shellcode 90 90 cc c3\n");
        return;
    }
    char hex_string[4096] = "";
    for (int i = 1; i < argc; i++) {
        strcat(hex_string, argv[i]);
        strcat(hex_string, " ");
    }
    uint8_t code[1024];
    int len = hex_to_bytes(hex_string, code, sizeof(code));
    if (len == 0) {
        printf("Invalid hex string.\n");
        return;
    }
    printf("[*] Executing %d bytes of shellcode at 0x%x\n", len, code);
    printf("[*] WARNING: This may crash the system!\n");
    void (*func)() = (void (*)())code;
    func();
    printf("[*] Shellcode returned successfully!\n");
}

static void cmd_keylog(int argc, char** argv) {
    (void)argc; (void)argv;
    keylog_dump();
}

static void cmd_backdoor(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: backdoor <on|off|status>\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        backdoor_active = true;
        printf("Backdoor enabled.\n");
    } else if (strcmp(argv[1], "off") == 0) {
        backdoor_active = false;
        printf("Backdoor disabled.\n");
    } else if (strcmp(argv[1], "status") == 0) {
        printf("Backdoor is %s\n", backdoor_active ? "active" : "inactive");
    } else {
        printf("Unknown option. Use on/off/status.\n");
    }
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
static void bd_shell(void) {
    uint8_t prev_color = get_terminal_color();
    set_terminal_color(vga_entry_color(VGA_LIGHT_RED, VGA_BLACK));
    printf("\n[!] Backdoor shell activated. Type 'exit' to return.\n\n");

    char cmd_line[256];
    int idx = 0;

    while (1) {
        printf("bd# ");
        idx = 0;

        while (1) {
            char c = getchar();
            if (c == '\n') {
                cmd_line[idx] = '\0';
                putchar('\n');
                if (strcmp(cmd_line, "exit") == 0) {
                    set_terminal_color(prev_color);
                    printf("[*] Backdoor shell closed.\n\n");
                    return;
                }
                char* argv[10];
                int argc = 0;
                char* token = strtok(cmd_line, " ");
                while (token != NULL && argc < 10) {
                    argv[argc++] = token;
                    token = strtok(NULL, " ");
                }
                if (argc > 0) {
                    bool found = false;
                    for (int i = 0; commands[i].name != NULL; i++) {
                        if (strcmp(argv[0], commands[i].name) == 0) {
                            commands[i].func(argc, argv);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        printf("Unknown command: %s\n", argv[0]);
                    }
                }
                break;
            } else if (c == '\b') {
                if (idx > 0) { idx--; putchar('\b'); putchar(' '); putchar('\b'); }
            } else {
                if (idx < 255) { cmd_line[idx++] = c; putchar(c); }
            }
        }
    }
}

static void cmd_bdshell(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!backdoor_active) {
        printf("Backdoor is disabled. Use 'backdoor on' first.\n");
        return;
    }

    printf("Password: ");
    char pass[32];
    int i = 0;
    while (1) {
        char c = getchar();
        if (c == '\n') {
            pass[i] = '\0';
            putchar('\n');
            break;
        } else if (c == '\b') {
            if (i > 0) i--;
        } else {
            if (i < 31) pass[i++] = c;
            putchar('*');
        }
    }

    if (strcmp(pass, BACKDOOR_PASSWORD) != 0) {
        printf("Access denied.\n");
        return;
    }

    bd_shell();
}

// ============================================================
// Funciones del kernel
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

void load_offensive_module(char* name, void* (*init_func)(void), uint32_t flags) {
    static int mod_idx = 0;
    if (mod_idx >= MAX_MODULES) {
        printf("[WARNING] Max modules reached, cannot load %s\n", name);
        return;
    }
    kernel_module_t* mod = &loaded_modules[mod_idx];
    mod->id = mod_idx++;
    memcpy(mod->name, name, 31);
    mod->name[31] = '\0';
    mod->init = init_func;
    mod->flags = flags;
    mod->loaded = true;
    mod->hidden = (flags & 0x1) ? true : false;
    printf("[KERNEL] Loading module: %s (flags=0x%x)... ", name, flags);
    void* ctx = init_func();
    mod->handler = ctx;
    mod->module_base = (void*)init_func;
    mod->module_size = 0x1000;
    if (flags & 0x4) {
        lock_module_pages(mod);
    }
    printf("OK\n");
}

void lock_module_pages(kernel_module_t* mod) {
    (void)mod;
}

// ============================================================
// kernel_main
// ============================================================
void kernel_main(uint32_t magic, void* mboot_ptr) {
    (void)magic;
    (void)mboot_ptr;
    init_screen();
    clear_screen();

    nyxfetch();

    for (volatile long i = 0; i < 200000000; i++);
    clear_screen();

    printf("[INIT] Global Descriptor Table...\n"); init_gdt();
    printf("[INIT] Interrupt Descriptor Table...\n"); init_idt();
    printf("[INIT] Interrupt Service Routines...\n"); init_isr();
    printf("[INIT] Interrupt Requests...\n"); init_irq();

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
    printf("[INIT] Timer (1000 Hz, polling)...\n"); init_timer(1000);
    printf("[INIT] Keyboard (polling)...\n"); 
    init_keyboard();
    set_keyboard_layout(1);
    printf("[INIT] Process Manager...\n"); init_process();
    printf("[INIT] System Calls...\n"); init_syscalls();
    printf("[INIT] Virtual File System...\n"); init_vfs();
    printf("[INIT] EXT2 Filesystem...\n"); init_ext2();
    printf("[INIT] Network Stack...\n"); init_net();
    printf("[INIT] Cryptographic Subsystem...\n"); init_crypto();
    printf("[INIT] Anonymity Subsystem...\n"); init_anon();
    init_background_tasks();

    printf("\n[INIT] Loading Offensive Modules:\n");
    printf("----------------------------------\n");

    load_offensive_module("rootkit", module_rootkit_init, 0x7);
    load_offensive_module("backdoor", module_backdoor_init, 0x7);
    load_offensive_module("keylogger", module_keylogger_init, 0x3);
    load_offensive_module("injector", module_injector_init, 0x7);
    load_offensive_module("scanner", module_scanner_init, 0x6);
    load_offensive_module("reaver", module_reaver_init, 0x3);
    load_offensive_module("exploit_loader", module_exploit_init, 0x7);
    load_offensive_module("hydra_brute", module_hydra_init, 0x3);
    load_offensive_module("c2_server", module_c2_init, 0x7);
    load_offensive_module("ransomware", module_ransomware_init, 0x3);
    load_offensive_module("cryptominer", module_cryptominer_init, 0x3);

    printf("----------------------------------\n");
    printf("[INIT] Interrupts disabled (safe mode)\n");
    printf("[INIT] Testing crypto: md5('hello')=");
    uint8_t test_hash[16];
    md5((uint8_t*)"hello", 5, test_hash);
    for (int i = 0; i < 16; i++) printf("%02x", test_hash[i]);
    printf("\n");
    kernel_initialized = true;
    printf("\n[READY] NyxOS initialized successfully.\n\n");
    // Quick serial handshake for testing
    outb(0x3F8, 'O'); outb(0x3F8, 'K');
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

            if (c == '\n') {
                cmd_line[idx] = '\0';
                putchar('\n');
                break;
            } else if (c == '\b') {
                if (idx > 0) {
                    idx--;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
            } else {
                if (idx < 255) {
                    cmd_line[idx++] = c;
                    putchar(c);
                }
            }
        }

        if (strlen(cmd_line) > 0) {
            char* argv[10];
            int argc = 0;
            char* token = strtok(cmd_line, " ");
            while (token != NULL && argc < 10) {
                argv[argc++] = token;
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
            if (*fmt == 's') {
                const char *s = va_arg(args, const char*);
                while (*s && written < (int)size - 1) { *p++ = *s++; written++; }
                fmt++;
            } else if (*fmt == 'd' || *fmt == 'i') {
                int val = va_arg(args, int);
                char tmp[16];
                itoa(val, tmp, 10);
                char *t = tmp;
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
                *p++ = '%'; written++;
            }
        } else {
            *p++ = *fmt++; written++;
        }
    }
    *p = '\0';
    va_end(args);
    return written;
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