// ============================================================
// kernel.c - Núcleo principal de NyxOS v3.0.0
// ============================================================
#include "kernel.h"
#include "namecheck.h"
#include "urlparse.h"
#include "urlcodec.h"
#include "../gui/core/compositor.h"
#include "../drivers/misc/apic.h"
#include "../drivers/misc/pci.h"
#include "../drivers/misc/nvme.h"
#include "../drivers/misc/blockdev.h"
#include "../drivers/misc/rtc.h"
#include "../drivers/audio/speaker.h"
#include "../net/tcp.h"
#include "../drivers/audio/sb16.h"
#include "../fs/ext2.h"
#include "../fs/grub_blobs.h"
#include "../fs/gpt.h"
#include "../fs/fat.h"
#include "../fs/grubefi_blob.h"
#include "../fs/tar.h"
#include "datefmt.h"
#include "ini.h"
#include "numparse.h"
#include "factor.h"
#include "semver.h"
#include "comm.h"
#include "seq.h"
#include "paste.h"
#include "tac.h"
#include "csv.h"
#include "tsort.h"
#include "base58.h"
#include "cut.h"
#include "uniq.h"
#include "join.h"
#include "calc.h"
#include "json.h"
#include "fletcher.h"
#include "../crypto/bech32.h"
#include "../crypto/sha256.h"
#include "../crypto/murmur3.h"
#include "../net/dns.h"
#include "../net/http.h"
#include "../net/ipaddr.h"
#include "../net/ipcalc.h"
#include "../crypto/tls/tls.h"
#include "../crypto/curve25519.h"
#include "../crypto/tls/tls_prf.h"
#include "../crypto/aes_gcm.h"
#include "../crypto/csprng.h"
#include "../crypto/der.h"
#include "../crypto/p256.h"
#include "../crypto/p384.h"
#include "../crypto/x509.h"
#include "../image/inflate.h"
#include "../image/deflate.h"
#include "../image/png.h"
#include "../image/bmp.h"
#include "../image/gif.h"
#include "../image/jpeg.h"
#include "../gui/apps/selene_win.h"
#include "../crypto/rsa.h"
#include "../crypto/sha512.h"
#include "../crypto/fnv.h"
#include "smp.h"
#include "../fs/initramfs.h"
#include "../gui/core/bootsplash.h"
#include "../auth/auth.h"
#include "../auth/login.h"
#include "../gui/core/nyx_logo.h"

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
static void cmd_man(int argc, char** argv);
static void cmd_version(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_nyxfetch(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);
static void cmd_ps(int argc, char** argv);
static void cmd_mtdemo(int argc, char** argv);
static void cmd_mem(int argc, char** argv);
static void cmd_cpus(int argc, char** argv);
static void cmd_smpstress(int argc, char** argv);
static void cmd_smpthreads(int argc, char** argv);
static void cmd_smpuser(int argc, char** argv);
static void cmd_tlbtest(int argc, char** argv);
static void cmd_smpbalance(int argc, char** argv);
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
static void cmd_useradd(int argc, char** argv);
static void cmd_users(int argc, char** argv);
static void cmd_ifconfig(int argc, char** argv);
static void cmd_arp(int argc, char** argv);
static void cmd_ping(int argc, char** argv);
static void cmd_kill(int argc, char** argv);
static void cmd_which(int argc, char** argv);
static void cmd_basename(int argc, char** argv);
static void cmd_dirname(int argc, char** argv);
static void cmd_head(int argc, char** argv);
static void cmd_file(int argc, char** argv);
static void cmd_tar(int argc, char** argv);
static void cmd_iniget(int argc, char** argv);
static void cmd_factor(int argc, char** argv);
static void cmd_isprime(int argc, char** argv);
static void cmd_strings(int argc, char** argv);
static void cmd_sha256sum(int argc, char** argv);
static void cmd_hmac(int argc, char** argv);
static void cmd_semver(int argc, char** argv);
static void cmd_fnv(int argc, char** argv);
static void cmd_seq(int argc, char** argv);
static void cmd_urlcode(int argc, char** argv);
static void cmd_paste(int argc, char** argv);
static void cmd_cut(int argc, char** argv);
static void cmd_uniq(int argc, char** argv);
static void cmd_join(int argc, char** argv);
static void cmd_tac(int argc, char** argv);
static void cmd_csv(int argc, char** argv);
static void cmd_tsort(int argc, char** argv);
static void cmd_base58(int argc, char** argv);
static void cmd_bech32(int argc, char** argv);
static void cmd_crc32c(int argc, char** argv);
static void cmd_fletcher(int argc, char** argv);
static void cmd_murmur(int argc, char** argv);
uint32_t crc32c_calc(const uint8_t* data, uint32_t len);
static void cmd_comm(int argc, char** argv);
static void cmd_vfsstat(int argc, char** argv);
static void cmd_rev(int argc, char** argv);
static void cmd_tr(int argc, char** argv);
static void cmd_fold(int argc, char** argv);
static void cmd_nl(int argc, char** argv);
static void cmd_printf(int argc, char** argv);
static void cmd_expand(int argc, char** argv);
static void cmd_unexpand(int argc, char** argv);
static void cmd_deflate(int argc, char** argv);
static void cmd_gzip(int argc, char** argv);
static void cmd_ipcalc(int argc, char** argv);
static void cmd_calc(int argc, char** argv);
static void cmd_json(int argc, char** argv);
static void cmd_tree(int argc, char** argv);
static void cmd_du(int argc, char** argv);
static void cmd_disks(int argc, char** argv);
static void cmd_lspci(int argc, char** argv);
static void cmd_nvme(int argc, char** argv);
static void cmd_nyxgrub(int argc, char** argv);
static void cmd_nyxpart(int argc, char** argv);
static void cmd_nyxgpt(int argc, char** argv);
static void cmd_nyxfat(int argc, char** argv);
static void cmd_nyxverify(int argc, char** argv);
static void cmd_mkfs(int argc, char** argv);
static void cmd_nyxinstall(int argc, char** argv);
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
static void cmd_mode(int argc, char** argv);
static void cmd_gui(int argc, char** argv);
static void cmd_fonttest(int argc, char** argv);
static void cmd_open(int argc, char** argv);
static void cmd_desktop(int argc, char** argv);
static void cmd_beep(int argc, char** argv);
static void cmd_play(int argc, char** argv);
static void cmd_sb16play(int argc, char** argv);
static void cmd_exec(int argc, char** argv);
static void cmd_cc(int argc, char** argv);
static void cmd_xbm(int argc, char** argv);
static void cmd_spawn(int argc, char** argv);
static void cmd_doom(int argc, char** argv);
static void cmd_pong(int argc, char** argv);
static void cmd_voxel(int argc, char** argv);
static void cmd_fire(int argc, char** argv);
static void cmd_matrix(int argc, char** argv);
static void cmd_lava(int argc, char** argv);
static void cmd_fractal(int argc, char** argv);
static void cmd_julia(int argc, char** argv);
static void cmd_particles(int argc, char** argv);
static void cmd_rotor(int argc, char** argv);
static void cmd_fill(int argc, char** argv);
static void cmd_life(int argc, char** argv);
static void cmd_blobs(int argc, char** argv);
static void cmd_screenshot(int argc, char** argv);
static void cmd_clip(int argc, char** argv);
static void cmd_notify(int argc, char** argv);
static void cmd_stackcheck(int argc, char** argv);
extern int stack_canary_sweep(void);      // process.c — scan task-stack canaries
extern int stack_canary_selftest(void);   // process.c — canary KAT
extern int term_paste_selftest(void);      // gui/apps/terminal_win.c — Ctrl+V paste KAT
extern int ext2_format_selftest(void);      // fs/ext2.c — mkfs.ext2 layout KAT
extern int ext2_format_mg_selftest(void);   // fs/ext2.c — multi-group mkfs KAT
extern int notify_selftest(void);           // gui/core/compositor.c — toast ring KAT
static void cmd_snake(int argc, char** argv);
static void cmd_tetris(int argc, char** argv);
static void cmd_selene(int argc, char** argv);
static void cmd_imageview(int argc, char** argv);
static void cmd_files(int argc, char** argv);
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
static void cmd_tls(int argc, char** argv);
static void cmd_posttest(int argc, char** argv);
static void cmd_tlsstrict(int argc, char** argv);
static void cmd_x25519test(int argc, char** argv);
static void cmd_prftest(int argc, char** argv);
static void cmd_tlskeytest(int argc, char** argv);
static void cmd_gcmtest(int argc, char** argv);
static void cmd_tlsrectest(int argc, char** argv);
static void cmd_csprngtest(int argc, char** argv);
static void cmd_dertest(int argc, char** argv);
static void cmd_p256test(int argc, char** argv);
static void cmd_p384test(int argc, char** argv);
static void cmd_skp384test(int argc, char** argv);
static void cmd_deflatetest(int argc, char** argv);
static void cmd_pngtest(int argc, char** argv);
static void cmd_bmptest(int argc, char** argv);
static void cmd_giftest(int argc, char** argv);
static void cmd_jpegtest(int argc, char** argv);
static void cmd_imgreject(int argc, char** argv);
static void cmd_httptest(int argc, char** argv);
static void cmd_ext2test(int argc, char** argv);
static void cmd_rsatest(int argc, char** argv);
static void cmd_sha512test(int argc, char** argv);
static void cmd_chaintest(int argc, char** argv);
static void cmd_formtest(int argc, char** argv);
static void cmd_setip(int argc, char** argv);
static void cmd_mount(int argc, char** argv);
static void cmd_df(int argc, char** argv);
static void cmd_setres(int argc, char** argv);

static const command_t commands[] = {
    {"help",      cmd_help,      "Show this help", false},
    {"man",       cmd_man,       "Show a command's manual page: man <command>", false},
    {"version",   cmd_version,   "Show kernel version", false},
    {"clear",     cmd_clear,     "Clear the screen", false},
    {"setres",    cmd_setres,    "Set screen resolution (setres 800 600)", false},
    {"nyxfetch",  cmd_nyxfetch,  "Show system info with ASCII logo", false},
    {"fastfetch", cmd_nyxfetch,  "Alias for nyxfetch", false},
    {"echo",      cmd_echo,      "Print a line of text", false},
    {"hexdump",   cmd_hexdump,   "Dump memory: hexdump <addr> [bytes]", false},
    {"date",      cmd_date,      "Show date/time; date +FORMAT for strftime output", false},
    {"uname",     cmd_uname,     "Show system information", false},
    {"reboot",    cmd_reboot,    "Reboot the system", false},
    {"ps",        cmd_ps,        "List processes", false},
    {"mtdemo",    cmd_mtdemo,    "Preemptive multitasking self-test", false},
    {"mem",       cmd_mem,       "Show memory usage", false},
    {"cpus",      cmd_cpus,      "List CPU cores (SMP)", false},
    {"smpstress", cmd_smpstress, "Hammer the allocators from every CPU: smpstress [secs]", false},
    {"smpthreads",cmd_smpthreads,"Run the per-AP kernel threads: smpthreads [secs]", false},
    {"smpuser",   cmd_smpuser,   "Spread user processes over the CPUs: smpuser [n]", false},
    {"tlbtest",   cmd_tlbtest,   "Prove cross-CPU TLB shootdown works", false},
    {"smpbalance",cmd_smpbalance,"Spread new processes AND threads over CPUs: smpbalance [on|off]", false},
    {"cowtest",   cmd_cowtest,   "Test demand paging + copy-on-write", false},

    {"crash",     cmd_crash,     "Trigger a kernel panic", false},
    {"layout",    cmd_layout,    "Change keyboard layout: layout <us|es>", false},
    {"ls",        cmd_ls,        "List directory contents: ls [-la] [path]", false},
    {"cd",        cmd_cd,        "Change directory: cd <path>", false},
    {"pwd",       cmd_pwd,       "Print working directory", false},
    {"cat",       cmd_cat,       "Display file contents: cat <file>", false},
    {"open",      cmd_open,      "Open a file in the GUI Text Editor: open <file>", false},
    {"touch",     cmd_touch,     "Create empty file: touch <file>", false},
    {"mkdir",     cmd_mkdir,     "Create directory: mkdir <dir>", false},
    {"rm",        cmd_rm,        "Remove file or directory: rm <path>", false},
    {"cp",        cmd_cp,        "Copy a file (cp <src> <dst>) or a tree (cp -r <srcdir> <dstdir>)", false},
    {"mv",        cmd_mv,        "Move/rename file: mv <src> <dst>", false},
    {"useradd",   cmd_useradd,   "Add a user account: useradd <user> <pass>", false},
    {"users",     cmd_users,     "List user accounts", false},
    {"ifconfig",  cmd_ifconfig,  "Show network interfaces", false},
    {"arp",       cmd_arp,       "Show the ARP cache (IPv4 -> MAC neighbours)", false},
    {"dns",       cmd_dns,       "DNS resolve: dns <hostname>", false},
    {"ping",      cmd_ping,      "Ping a host: ping <ip|hostname>", false},
    {"kill",      cmd_kill,      "Kill a process: kill <pid>", false},
    {"which",     cmd_which,     "Show path of a command: which <name>", false},
    {"basename",  cmd_basename,  "Strip directory (and suffix) from a path: basename <path> [suffix]", false},
    {"dirname",   cmd_dirname,   "Strip the last component from a path: dirname <path>", false},
    {"head",      cmd_head,      "Show first lines of a file: head <file> [lines]", false},
    {"file",      cmd_file,      "Identify a file's type: file <file>...", false},
    {"tar",       cmd_tar,       "List a tar archive: tar t[v] <file.tar>", false},
    {"iniget",    cmd_iniget,    "Read an INI/conf value: iniget <file> <section|-> <key>", false},
    {"tree",      cmd_tree,      "Show filesystem tree: tree [path]", false},
    {"du",        cmd_du,        "Disk usage: du [path] sums file sizes over a subtree", false},
    {"disks",     cmd_disks,     "List physical disks + their MBR partitions", false},
    {"lsblk",     cmd_disks,     "Alias for disks", false},
    {"lspci",     cmd_lspci,     "List PCI devices (finds NVMe/AHCI/network controllers)", false},
    {"nvme",      cmd_nvme,      "Bring up the NVMe controller (probe + admin queue)", false},
    {"nyxpart",   cmd_nyxpart,   "Write an MBR partition table: nyxpart <drive> new [size_MB]", false},
    {"mkfs",      cmd_mkfs,      "Format ext2 on a disk: mkfs <drive> [part_lba] [size_MB]", false},
    {"nyxinstall",cmd_nyxinstall,"Install NyxOS to a disk (add --uefi for GPT+ESP): nyxinstall [--uefi] <drive> confirm", false},
    {"nyxgrub",   cmd_nyxgrub,   "Install GRUB boot sectors so a disk boots standalone: nyxgrub <drive>", false},
    {"nyxgpt",    cmd_nyxgpt,    "Write a UEFI GUID Partition Table (ESP + NyxOS): nyxgpt <drive> confirm", false},
    {"nyxfat",    cmd_nyxfat,    "Format the ESP FAT32 + write /EFI/BOOT: nyxfat <drive> confirm", false},
    {"nyxverify", cmd_nyxverify, "Check an installed disk's kernel (ELF+multiboot2, ext2+raw): nyxverify <drive>", false},
    {"env",       cmd_env,       "Show environment variables", false},
    {"export",    cmd_export,    "Set env variable: export <name>=<value>", false},
    {"find",      cmd_find,      "Find files by name: find <name> [path]", false},
    {"grep",      cmd_grep,      "Search file contents: grep [-inv] <pattern> <file>", false},
    {"tail",      cmd_tail,      "Show last lines of a file: tail <file> [lines]", false},
    {"sort",      cmd_sort,      "Sort lines of a file: sort [-rn] <file>", false},
    {"rev",       cmd_rev,       "Reverse the characters of each line: rev <file>", false},
    {"tr",        cmd_tr,        "Translate/delete/squeeze chars: tr [-ds] SET1 [SET2] <file>", false},
    {"fold",      cmd_fold,      "Wrap long lines to a width: fold [-w width] <file>", false},
    {"nl",        cmd_nl,        "Number lines: nl [-b a|t|n] [-w N] [-s SEP] <file>", false},
    {"factor",    cmd_factor,    "Prime factorization: factor N [N ...]", false},
    {"isprime",   cmd_isprime,   "Test primality of each integer: isprime N [N ...]", false},
    {"strings",   cmd_strings,   "Print printable-character runs in a file: strings [-n MIN] <file>", false},
    {"sha256sum", cmd_sha256sum, "Print the SHA-256 digest of each file: sha256sum <file>...", false},
    {"hmac",      cmd_hmac,      "HMAC-SHA256 of a message under a key: hmac <key> <message>", false},
    {"seq",       cmd_seq,       "Integer sequence: seq [FIRST [STEP]] LAST", false},
    {"paste",     cmd_paste,     "Merge lines of files: paste [-s] [-d LIST] <file ...>", false},
    {"clip",      cmd_clip,      "Clipboard: clip <text> to copy, clip to paste, clip -c to clear", false},
    {"notify",    cmd_notify,    "Show a desktop notification: notify <title> [message...]", false},
    {"cut",       cmd_cut,       "Select fields/chars of each line: cut -f|-c|-b LIST [-d C] [-s] <file>", false},
    {"uniq",      cmd_uniq,      "Collapse adjacent equal lines: uniq [-c|-d|-u|-i|-f N|-s N|-w N] <file>", false},
    {"join",      cmd_join,      "Join two sorted files on a field: join [-t C] [-1/-2/-j N] [-a 1|2] <f1> <f2>", false},
    {"tac",       cmd_tac,       "Print a file's lines in reverse order: tac [-s SEP] <file>", false},
    {"csv",       cmd_csv,       "View a CSV file as an aligned table: csv [-d DELIM] <file>", false},
    {"tsort",     cmd_tsort,     "Topological sort of a dependency list: tsort <file>", false},
    {"comm",      cmd_comm,      "Compare two sorted files: comm [-123] <f1> <f2>", false},
    {"vfsstat",   cmd_vfsstat,   "VFS node-pool usage census (diagnose exhaustion)", false},
    {"semver",    cmd_semver,    "Validate/compare SemVer 2.0.0: semver <ver> [<ver2>]", false},
    {"fnv",       cmd_fnv,       "FNV-1a hash of text: fnv [-64] <text ...>", false},
    {"urlcode",   cmd_urlcode,   "Percent-encode/decode text: urlcode [-d] <text ...>", false},
    {"crc32c",    cmd_crc32c,    "CRC-32C (Castagnoli) checksum: crc32c <text ...>", false},
    {"fletcher",  cmd_fletcher,  "Fletcher-16/32 checksum: fletcher [-32] <text ...>", false},
    {"murmur",    cmd_murmur,    "MurmurHash3-32 (non-crypto hash): murmur [-s SEED] <text ...>", false},
    {"base58",    cmd_base58,    "Base58 (Bitcoin) encode/decode text: base58 [-d] <text ...>", false},
    {"bech32",    cmd_bech32,    "Verify/decode a Bech32 (BIP-173) checksummed string: bech32 <string>", false},
    {"printf",    cmd_printf,    "Format and print: printf FORMAT [ARG...]", false},
    {"expand",    cmd_expand,    "Convert tabs to spaces: expand [-t N] <file>", false},
    {"unexpand",  cmd_unexpand,  "Convert spaces to tabs: unexpand [-a] [-t N] <file>", false},
    {"deflate",   cmd_deflate,   "Compress a file (zlib/DEFLATE): deflate <in> <out>", false},
    {"gzip",      cmd_gzip,      "Compress a file to gunzip-readable .gz: gzip <in> <out>", false},
    {"ipcalc",    cmd_ipcalc,    "IPv4 subnet calc: ipcalc <ip>/<prefix> | <ip> <mask>", false},
    {"calc",      cmd_calc,      "Evaluate an integer expression: calc <expr>", false},
    {"json",      cmd_json,      "Validate / query JSON: json <file> | json get <file> <path>", false},
    {"wc",        cmd_wc,        "Count lines/words/chars: wc <file>", false},
    {"write",     cmd_write,     "Write text to file: write <file> <text>", false},
    {"dhcp",      cmd_dhcp,      "Request IP via DHCP", false},
    {"history",   cmd_history,   "Show command history", false},
    {"mode",      cmd_mode,      "Set VBE video mode: mode <width> <height> <bpp>", false},
    {"gui",       cmd_gui,       "Launch GUI demo with mouse", false},
    {"fonttest",  cmd_fonttest,  "Test font rendering on framebuffer", false},
    {"desktop",   cmd_desktop,   "Launch window compositor desktop", false},
    {"beep",      cmd_beep,      "Play a tone: beep [freq] [ms]", false},
    {"play",      cmd_play,      "Play a demo melody", false},
    {"sb16play",  cmd_sb16play,  "Test SB16 playback: sb16play [freq] [ms]", false},
    {"exec",      cmd_exec,      "Run ELF in foreground (waits): exec <file>", false},
    {"cc",        cmd_cc,        "Compile/link C in-OS: cc [-c] [--self-libc] <in.c/.o ...> [-o out]", false},
    {"xbm",       cmd_xbm,       "Package manager: xbm install|remove|verify|deps <name> | xbm search <str> | xbm list [--installed]", false},
    {"pkg",       cmd_xbm,       "Alias for xbm (package manager)", true},
    {"spawn",     cmd_spawn,     "Run ELF in background: spawn <file>", false},
    {"doom",      cmd_doom,      "Play DOOM in a window (Ctrl-C to quit)", false},
    {"pong",      cmd_pong,      "Play Pong (mouse or arrows)", false},
    {"voxel",     cmd_voxel,     "Open Nyx Voxels (a voxel scene)", false},
    {"fire",      cmd_fire,      "Open Nyx Fire (animated doom-fire effect)", false},
    {"matrix",    cmd_matrix,    "Open Nyx Matrix (green code-rain effect)", false},
    {"lava",      cmd_lava,      "Open Nyx Lava (animated plasma effect)", false},
    {"fractal",   cmd_fractal,   "Open Nyx Fractal (Mandelbrot render-perf demo)", false},
    {"julia",     cmd_julia,     "Open Nyx Julia (morphing Julia-set render-perf demo)", false},
    {"particles", cmd_particles, "Open Nyx Particles (particle-fountain render-perf demo)", false},
    {"rotor", cmd_rotor, "Open Nyx Rotor (spinning 3D point-lattice render-perf demo)", false},
    {"fill", cmd_fill, "Open Nyx Fill (2D fill-rate / overdraw render-perf demo)", false},
    {"life", cmd_life, "Open Nyx Life (Conway's Game of Life compute/render-perf demo)", false},
    {"blobs", cmd_blobs, "Open Nyx Blobs (metaballs render-perf demo)", false},
    {"snake",     cmd_snake,     "Play Snake (arrows/WASD)", false},
    {"tetris",    cmd_tetris,    "Play Tetris (arrows, Space=drop)", false},
    {"selene",    cmd_selene,    "Open Selene, the web browser", false},
    {"imageview", cmd_imageview, "View an image (PNG/BMP/GIF/JPEG): imageview <path>", false},
    {"files",     cmd_files,     "Open the File Manager: files [path]", false},
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
    {"tls",       cmd_tls,       "TLS handshake test: tls <host> (https :443)", false},
    {"posttest",  cmd_posttest,  "Live HTTP POST self-test (POST a form to httpbin over TLS)", false},
    {"tlsstrict", cmd_tlsstrict, "Strict TLS cert enforcement: tlsstrict [on|off]", false},
    {"x25519test",cmd_x25519test,"X25519 (Curve25519) self-test — RFC 7748 vectors", false},
    {"prftest",   cmd_prftest,   "TLS 1.2 PRF self-test — RFC 4231 + P_SHA256 vectors", false},
    {"tlskeytest",cmd_tlskeytest,"TLS 1.2 key-schedule self-test (master secret + keys)", false},
    {"gcmtest",   cmd_gcmtest,   "AES-128-GCM self-test — FIPS-197 + NIST GCM vectors", false},
    {"tlsrectest",cmd_tlsrectest,"TLS record + Finished self-test (GCM records, verify_data)", false},
    {"csprngtest",cmd_csprngtest,"CSPRNG self-test — HMAC_DRBG (NIST) + hardware entropy", false},
    {"dertest",   cmd_dertest,   "DER/ASN.1 reader self-test (X.509 pubkey extraction)", false},
    {"p256test",  cmd_p256test,  "NIST P-256 self-test (point multiples + ECDSA verify)", false},
    {"p384test",  cmd_p384test,  "NIST P-384 self-test (point multiples + ECDSA verify)", false},
    {"skp384test",cmd_skp384test,"ECDSA-P384 ServerKeyExchange verification self-test", false},
    {"deflatetest",cmd_deflatetest,"DEFLATE/zlib decompression self-test (for PNG images)", false},
    {"pngtest",   cmd_pngtest,   "PNG image decoder self-test (color types + filters)", false},
    {"bmptest",   cmd_bmptest,   "BMP image decoder self-test (24/32/8-bit + top-down)", false},
    {"giftest",   cmd_giftest,   "GIF image decoder self-test (LZW + interlace + transparency)", false},
    {"jpegtest",  cmd_jpegtest,  "JPEG (baseline) decoder self-test (Huffman + IDCT + YCbCr)", false},
    {"imgreject", cmd_imgreject, "Image-decoder reject self-test (png/bmp/gif/jpeg refuse hostile input)", false},
    {"httptest", cmd_httptest, "HTTP-parser robustness self-test (untrusted responses: chunked, hostile sizes)", false},
    {"ext2test", cmd_ext2test, "ext2 directory-scanner self-test (untrusted dir blocks: bad rec_len/name_len)", false},
    {"rsatest",   cmd_rsatest,   "RSA PKCS#1 v1.5 SHA-256 signature-verify self-test", false},
    {"sha512test",cmd_sha512test,"SHA-512 / SHA-384 self-test (FIPS 180-4 vectors)", false},
    {"chaintest", cmd_chaintest, "X.509 certificate-chain verification self-test (pinned root)", false},
    {"formtest",  cmd_formtest,  "Selene HTML-forms self-test (parse form + build GET URL)", false},
    {"setip",     cmd_setip,     "Set static IP: setip <ip> <mask> <gw>", false},
    {"mount",     cmd_mount,     "Mount EXT2: mount [drive] [part_lba]", false},
    {"ext2ls",    cmd_mount,     "Alias for mount", false},
    {"ext2cat",   cmd_mount,     "Alias for mount", false},
    {"df",        cmd_df,        "Show disk usage of the mounted filesystem", false},
    {"screenshot",cmd_screenshot,"Save a PNG/PPM snapshot of the screen: screenshot [path]", false},
    {"stackcheck",cmd_stackcheck,"Check every task's kernel-stack overflow canary", false},
    {NULL, NULL, NULL, false}
};

static int is_builtin(const char* name) {
    for (int i = 0; commands[i].name != NULL; i++)
        if (!commands[i].hidden && strcmp(commands[i].name, name) == 0) return 1;
    return 0;
}

// Enumerate every tab-completion candidate for the command word: shell builtins,
// plus the base names (minus ".elf") of userspace programs in the initramfs root
// (now that a bare `wget`/`whoami` auto-execs, they should complete too). An ELF
// whose base name shadows a builtin is skipped, so each name is offered once.
static void enum_completions(void (*cb)(const char* name, void* ctx), void* ctx) {
    for (int i = 0; commands[i].name != NULL; i++)
        if (!commands[i].hidden) cb(commands[i].name, ctx);
    int fd = vfs_open("/", 0, 0);
    if (fd < 0) return;
    dirent_t* de = vfs_readdir(fd);
    while (de) {
        int l = strlen(de->name);
        if (l > 4 && strcmp(de->name + l - 4, ".elf") == 0) {
            char base[64];
            int bl = l - 4; if (bl > 63) bl = 63;
            memcpy(base, de->name, bl); base[bl] = '\0';
            if (!is_builtin(base)) cb(base, ctx);
        }
        de = vfs_readdir(fd);
    }
    vfs_close(fd);
}

typedef struct { const char* partial; int plen; char* out; int out_size; int pos; } list_ctx_t;
static void list_cb(const char* name, void* c) {
    list_ctx_t* x = (list_ctx_t*)c;
    if (strncmp(name, x->partial, x->plen) != 0) return;
    int len = strlen(name);
    if (x->pos + len + 2 > x->out_size - 1) return;   // out of room -> drop extras
    memcpy(x->out + x->pos, name, len); x->pos += len;
    x->out[x->pos++] = ' '; x->out[x->pos] = '\0';
}

void command_list_matches(const char* partial, char* out, int out_size) {
    out[0] = '\0';
    list_ctx_t x = { partial, strlen(partial), out, out_size, 0 };
    enum_completions(list_cb, &x);
}

typedef struct { const char* partial; int plen; int count; char first[64]; char common[64]; } compl_ctx_t;
static void compl_cb(const char* name, void* c) {
    compl_ctx_t* x = (compl_ctx_t*)c;
    if (strncmp(name, x->partial, x->plen) != 0) return;
    if (x->count == 0) {
        strncpy(x->first, name, sizeof(x->first) - 1);  x->first[sizeof(x->first) - 1]  = '\0';
        strncpy(x->common, name, sizeof(x->common) - 1); x->common[sizeof(x->common) - 1] = '\0';
    } else {
        for (int j = 0; x->common[j]; j++)              // shrink to the common prefix
            if (name[j] != x->common[j]) { x->common[j] = '\0'; break; }
    }
    x->count++;
}

void command_complete(const char* partial, char* out, int out_size, int* match_count) {
    compl_ctx_t x = { partial, strlen(partial), 0, "", "" };
    enum_completions(compl_cb, &x);
    *match_count = x.count;
    if (out && out_size > 0) out[0] = '\0';
    if (x.count == 1 && out) {
        strncpy(out, x.first, out_size - 1);
        out[out_size - 1] = '\0';
    } else if (x.count > 1 && out && strlen(x.common) > (size_t)x.plen) {
        strncpy(out, x.common, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

// Run a userspace ELF as a foreground job: spawn it (forwarding argv), block —
// repainting the desktop so it stays live — until it exits, then reap it and print
// the exit code. Shared by `exec` and the shell's auto-exec fallback.
static int run_foreground_elf(const char* path, char* const* argv, int argc) {
    int pid = spawn_user_path_args(path, argv, argc);
    if (pid < 0) {
        printf("exec: could not load %s (err %d)\n", path, pid);
        return pid;
    }
    extern uint32_t g_foreground_pid;
    g_foreground_pid = (uint32_t)pid;         // Ctrl-C posts SIGINT here while it runs
    for (;;) {
        process_t* child = find_process((uint32_t)pid);
        if (!child || child->state == PROC_ZOMBIE) break;
        if (child->state == PROC_STOPPED)     // no job control here: just resume
            signal_raise(child, SIGCONT);
        compositor_redraw_now();
        sleep(60);
    }
    int code = find_process((uint32_t)pid) ? kwait((uint32_t)pid) : 0;  // reap
    g_foreground_pid = 0;
    compositor_redraw_now();
    printf("[exec] PID %d exited (code %d)\n", pid, code);
    return code;
}

// GUI entry point: launch a userspace ELF from a desktop icon EXACTLY the way the shell
// launches it from a typed command — spawn, then pump the desktop and block until exit.
// A bare spawn_user_path() from the compositor left the child unscheduled (the click
// looked like a no-op): run_foreground_elf's redraw+sleep loop is what yields the CPU so
// the child actually runs and its SYS_FBPRESENT fullscreen takeover is serviced. Blocking
// the compositor here is fine — it is the same call chain the terminal uses. (v5.9.36)
void gui_launch_elf(const char* path) {
    run_foreground_elf(path, (char* const*)0, 0);
}

// Resolve a bare command name to a userspace ELF path in the initramfs. A name that
// already contains '/' is used verbatim; otherwise "/<name>.elf", then "/<name>",
// then "/mnt/bin/<name>" (installed packages) are tried — a PATH-like search so an
// xbm-installed binary runs by bare name. Returns 1 and fills `out` if a file exists.
static int resolve_user_elf(const char* name, char* out, int outsz) {
    int fd;
    if (strchr(name, '/')) {
        strncpy(out, name, outsz - 1); out[outsz - 1] = '\0';
        if ((fd = vfs_open(out, 0, 0)) >= 0) { vfs_close(fd); return 1; }
        return 0;
    }
    snprintf(out, outsz, "/%s.elf", name);
    if ((fd = vfs_open(out, 0, 0)) >= 0) { vfs_close(fd); return 1; }
    snprintf(out, outsz, "/%s", name);
    if ((fd = vfs_open(out, 0, 0)) >= 0) { vfs_close(fd); return 1; }
    snprintf(out, outsz, "/mnt/bin/%s", name);   // installed packages (xbm) — bare-name run
    if ((fd = vfs_open(out, 0, 0)) >= 0) { vfs_close(fd); return 1; }
    return 0;
}

static void shell_expand_vars(const char* in, char* out, int outsz);   // $VAR / ${VAR} expansion (defined below)

void execute_command(const char* cmd_line) {
    if (!cmd_line || !*cmd_line) return;
    char cmd_copy[256];
    strncpy(cmd_copy, cmd_line, 255);
    cmd_copy[255] = '\0';
    char* argv[MAX_CMD_ARGS];
    // Expand $VAR / ${VAR} only when the line actually contains a '$'. Non-'$' commands take
    // the original path byte-for-byte, so nothing the shell already ran changes behaviour
    // (including the cc self-host, which runs `cc …` through here). The expansion scratch
    // (MAX_CMD_ARGS*256 = 8 KB) is kmalloc'd per call — off the 4 KB kernel stack, and
    // re-entrant when a command runs another via execute_command; a failed alloc harmlessly
    // falls back to no expansion.
    char (*argbuf)[256] = (strchr(cmd_copy, '$') != NULL)
                          ? (char (*)[256])kmalloc(MAX_CMD_ARGS * 256) : NULL;
    int argc = 0;
    char* token = strtok(cmd_copy, " ");
    while (token != NULL && argc < MAX_CMD_ARGS) {
        if (argbuf) { shell_expand_vars(token, argbuf[argc], 256); argv[argc] = argbuf[argc]; }
        else        { argv[argc] = token; }
        argc++;
        token = strtok(NULL, " ");
    }
    if (token != NULL) {          /* more tokens than the cap: refuse rather than silently truncate */
        printf("error: too many arguments (max %d)\n", MAX_CMD_ARGS);
        if (argbuf) kfree(argbuf);
        return;
    }
    if (argc == 0) { if (argbuf) kfree(argbuf); return; }
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            if (argbuf) kfree(argbuf);
            return;
        }
    }
    // Not a builtin — auto-exec a matching userspace ELF (e.g. `wget http://…`,
    // `grep foo bar`) so tools run without the `exec` prefix. argv[0] is the typed
    // name; the child gets the full argv.
    char path[128];
    if (resolve_user_elf(argv[0], path, sizeof(path))) {
        run_foreground_elf(path, argv, argc);
        if (argbuf) kfree(argbuf);
        return;
    }
    // Command not found - output will be captured by putchar hook
    printf("Command not found: %s\n", argv[0]);
    if (argbuf) kfree(argbuf);
}

// ------------------------------------------------------------
// Implementación de comandos
// ------------------------------------------------------------
// `help` groups the builtins under category headers instead of one flat list, so
// the ~110 commands are navigable. Each category lists the names that belong to it;
// a final "Other" pass prints any visible command not placed above, so nothing is
// ever dropped even if a new command isn't categorised here yet.
typedef struct { const char* title; const char* const* names; } help_cat_t;
static const char* const HC_shell[] = {"help","man","version","clear","history","exec","spawn","jobs","wait","nice","renice",0};
static const char* const HC_files[] = {"ls","cd","pwd","cat","file","tar","iniget","open","touch","mkdir","rm","cp","mv","tree","find","which","basename","dirname","files","df","du","disks","lsblk","lspci","nvme","nyxpart","mkfs","nyxinstall","nyxgrub","mount","ext2ls","ext2cat",0};
static const char* const HC_text[]  = {"echo","head","tail","grep","sort","rev","tac","csv","tsort","tr","fold","nl","expand","unexpand","factor","isprime","strings","sha256sum","seq","paste","clip","cut","uniq","join","comm","printf","wc","write","hexdump",0};
static const char* const HC_sys[]   = {"ps","kill","mem","cpus","uname","date","reboot","env","export","layout","setres","mode","beep","desktop","gui","fonttest","nyxfetch","fastfetch","vfsstat","screenshot","stackcheck",0};
static const char* const HC_user[]  = {"useradd","users",0};
static const char* const HC_net[]   = {"ifconfig","arp","dhcp","dns","ping","setip","httpget","tls","ipcalc",0};
static const char* const HC_dev[]   = {"cc","xbm","semver","fnv","urlcode","crc32c","fletcher","murmur","base58","bech32","deflate","gzip","calc","json","hmac",0};
static const char* const HC_media[] = {"play","sb16play","imageview","selene",0};
static const char* const HC_games[] = {"doom","pong","voxel","fire","matrix","lava","fractal","julia","particles","snake","tetris",0};
static const char* const HC_test[]  = {"mtdemo","smpstress","smpuser","smpthreads","smpbalance","tlbtest","cowtest","crash","usertest","tcptest","tcpdrop","tcploop","tcpserve","posttest","tlsstrict","prftest","gcmtest","dertest","p256test","p384test","x25519test","tlskeytest","tlsrectest","csprngtest","skp384test","deflatetest","sha512test","pngtest","bmptest","giftest","jpegtest","imgreject","httptest","ext2test","rsatest","chaintest","formtest",0};
static const help_cat_t help_cats[] = {
    {"Shell & help",              HC_shell},
    {"Files & directories",       HC_files},
    {"Text",                      HC_text},
    {"Processes & system",        HC_sys},
    {"Users",                     HC_user},
    {"Networking",                HC_net},
    {"Development",               HC_dev},
    {"Media & apps",              HC_media},
    {"Games & visuals",           HC_games},
    {"Self-tests & diagnostics",  HC_test},
};
#define HELP_NCATS ((int)(sizeof(help_cats) / sizeof(help_cats[0])))

static const char* help_text_of(const char* name) {   // visible command's one-line help, or NULL
    for (int i = 0; commands[i].name; i++)
        if (!commands[i].hidden && strcmp(commands[i].name, name) == 0) return commands[i].help;
    return 0;
}
static int help_in_any_cat(const char* name) {
    for (int c = 0; c < HELP_NCATS; c++)
        for (const char* const* n = help_cats[c].names; *n; n++)
            if (strcmp(*n, name) == 0) return 1;
    return 0;
}
static void help_print_row(const char* name, const char* help) {
    char buf[16];
    int len = (int)strlen(name); if (len > 14) len = 14;
    memcpy(buf, name, (size_t)len);
    for (int j = len; j < 14; j++) buf[j] = ' ';
    buf[14] = '\0';
    printf("  %s - %s\n", buf, help);
}

static void cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("NyxOS shell commands  (use `man <cmd>` for a full page):\n");
    for (int c = 0; c < HELP_NCATS; c++) {
        int printed_header = 0;
        for (const char* const* n = help_cats[c].names; *n; n++) {
            const char* h = help_text_of(*n);
            if (!h) continue;                       // listed name that no longer exists / is hidden
            if (!printed_header) { printf("\n%s:\n", help_cats[c].title); printed_header = 1; }
            help_print_row(*n, h);
        }
    }
    // Other: any visible command not placed in a category above.
    int printed_other = 0;
    for (int i = 0; commands[i].name; i++) {
        if (commands[i].hidden || help_in_any_cat(commands[i].name)) continue;
        if (!printed_other) { printf("\nOther:\n"); printed_other = 1; }
        help_print_row(commands[i].name, commands[i].help);
    }
}

// Longer manual-page bodies for the most-used commands. Any command NOT listed
// here still gets a page from `man` (built from its one-line commands[] help), so
// `man` works for everything; these entries just give the core tools a proper
// DESCRIPTION paragraph, closer to a real Unix manual.
typedef struct { const char* name; const char* body; } man_page_t;
static const man_page_t man_pages[] = {
    {"ls",       "List the contents of a directory. With no path the current directory is listed. -a also shows entries whose name begins with a dot; -l gives a long listing with each entry's type, size and name."},
    {"cd",       "Change the shell's current working directory to <path>."},
    {"pwd",      "Print the full path of the current working directory."},
    {"cat",      "Write the contents of <file> to standard output. The usual way to view a text file."},
    {"file",     "Identify the type of each <file> from its leading bytes (magic numbers: ELF, PNG, GIF, JPEG, PDF, Zip, gzip, WAV, BMP, tar, #! scripts) and, for text, whether it is ASCII or UTF-8 (with a source-extension hint like `C source`). Prints `<file>: <type>`."},
    {"tar",      "List the members of a POSIX ustar (.tar) archive: `tar t <file.tar>` prints each member's path, and `tar tv` also prints its type flag and byte size. Listing only — extraction is not yet supported."},
    {"iniget",   "Read one value from an INI/.conf file: `iniget <file> <section> <key>` prints the trimmed value under [section], or `iniget <file> - <key>` reads the global section (keys before any [section]). '=' is the delimiter; ';'/'#' begin whole-line comments. Prints nothing and reports not-found if the key is absent."},
    {"cp",       "Copy the file <src> to <dst>, replacing <dst> if it already exists. With `cp -r <srcdir> <dstdir>` it copies a whole directory tree recursively, creating each destination directory and copying every file across filesystems (e.g. from the RAM image to a freshly-formatted disk mounted at /mnt) — this is how `nyxinstall` populates a target disk. The walk is iterative with a bounded off-stack frontier, so deep trees are safe on the small kernel stack."},
    {"mv",       "Move or rename <src> to <dst>. Within one filesystem this only rewrites the directory entry."},
    {"rm",       "Remove <path>. There is no recycle bin, so a removed file is gone for good."},
    {"mkdir",    "Create a new, empty directory named <dir>."},
    {"echo",     "Write the arguments to standard output separated by spaces and followed by a newline. `echo text > file` writes to a file instead of the screen."},
    {"grep",     "Print the lines of <file> that match <pattern>. -i ignores letter case, -n prefixes each match with its line number, and -v inverts the search to print the lines that do NOT match."},
    {"sort",     "Sort the lines of <file>. -r reverses the result; -n sorts numerically by the integer at the start of each line instead of alphabetically."},
    {"rev",      "Print each line of <file> with the order of its characters reversed."},
    {"tr",       "Translate, delete or squeeze characters read from <file>. With two sets, each character of <file> that appears in SET1 is replaced by the character at the same position in SET2 (a shorter SET2 repeats its last character). -d deletes every SET1 character instead; -s collapses each run of a repeated result character into one. Sets may use ascending ranges such as a-z or 0-9."},
    {"fold",     "Wrap the lines of <file> so no output line is longer than the given width (80 by default, or -w width). A line longer than the width is broken with a hard newline at exactly that many characters; shorter lines and existing line breaks are left alone."},
    {"nl",       "Number the lines of <file>. By default only non-empty lines are numbered (-b t); -b a numbers every line and -b n numbers none. Each line number is right-justified in a field N columns wide (6 by default, or -w N) and followed by a separator (a tab by default, or -s SEP), then the line text."},
    {"factor",   "Print the prime factorization of each integer argument, one per line, as `N: p1 p2 ...` with factors ascending and repeated by multiplicity (e.g. `factor 90` prints `90: 2 3 3 5`). 0 and 1 print just `N:`. Accepts any 64-bit unsigned value; a non-numeric or negative argument is reported and skipped. Small factors are peeled by trial division and the rest by Miller-Rabin + Pollard's rho, so even a large 64-bit semiprime factors quickly."},
    {"isprime",  "Test each integer argument for primality and print `N is prime` or `N is not prime`. Uses an exact deterministic Miller-Rabin (the twelve witnesses 2..37, proven correct for all 64-bit N), so the answer is definitive — not probabilistic — and it correctly rejects Carmichael numbers that fool a naive Fermat test. Much faster than `factor` for a large prime, since it never has to find the factors. A non-numeric or negative argument is reported and skipped."},
    {"strings",  "Print each run of at least MIN (default 4, or -n MIN) consecutive printable characters found in <file>, one run per line — the classic way to read the text embedded in a binary (an ELF, an image, a package). A printable character is a space through `~` (0x20-0x7E) or a tab; any other byte ends the current run. The file is streamed in fixed chunks, so even a large binary needs no whole-file buffer."},
    {"hmac","Compute HMAC-SHA256 of <message> keyed by <key> and print it as 64 lowercase hex digits: `hmac <key> <message>`. HMAC is the standard keyed-hash message authentication code (RFC 2104 / FIPS 198) — the way API requests are signed, JWT `HS256` tokens are built, and webhook payloads are verified: the same key + message always yields the same tag, and without the key the tag can't be forged. Both arguments are taken as text (quote a message with spaces). Backed by the same KAT'd `hmac_sha256` the CSPRNG, HKDF and PBKDF2 auth path use; the RFC 4231 test vectors pin it (the `hmac` self-test)."},
    {"sha256sum","Print the SHA-256 digest of each file argument as `<64-hex-digits>  <name>` (two spaces between, the GNU sha256sum format), the standard way to check a file's integrity — e.g. that a downloaded package matches a published hash. Each file is streamed through the hash in fixed chunks, so a large binary needs no whole-file buffer, and the total is capped so an endless special like /dev/zero cannot spin forever. A file that cannot be opened is reported and skipped."},
    {"vfsstat",  "Report VFS node-pool usage: how many of the fixed node slots are live, free, and the linear high-water mark, plus a breakdown of the transient mount-backed (ext2 /mnt mirror) nodes into those still held by an open fd versus idle-but-unfreed. A diagnostic for node-pool exhaustion under sustained in-OS file I/O (issue #66): if `mount held` climbs and never falls across a compile session, an fd is leaking; watch it before/after `cc`/`xbm` runs."},
    {"comm",     "Compare two files that are each already sorted, line by line, in three columns: lines only in <file1> (column 1), lines only in <file2> (column 2, indented one tab), and lines common to both (column 3, indented two tabs). `-1`/`-2`/`-3` suppress the respective column (and drop its indentation from the later columns), so e.g. `comm -12 a b` prints just the lines common to both. Input is assumed sorted in byte order."},
    {"semver",   "Parse and compare Semantic Versioning 2.0.0 strings (MAJOR.MINOR.PATCH[-prerelease][+build]). With one argument, validate it and print the parsed fields. With two, print their precedence relation (`A < B`, `A = B`, or `A > B`) per the semver spec: core numbers compared numerically, a prerelease ranks below the same version without one, and build metadata is ignored. Useful for comparing package versions."},
    {"seq",      "Print an inclusive sequence of integers, one per line. `seq LAST` counts 1..LAST; `seq FIRST LAST` counts FIRST..LAST; `seq FIRST STEP LAST` advances by STEP (which may be negative). A range that starts on the wrong side of LAST prints nothing, and a zero STEP is an error. Integer-only (64-bit signed), matching GNU seq for integer arguments."},
    {"stackcheck","Scan every running task's kernel stack for overflow. Each 4 KB kernel task stack carries a magic canary word at its low (overflow) end, stamped when the stack is allocated; `stackcheck` walks the process table and reports whether every canary is still intact or names any task whose stack has been smashed (grown past 4 KB into the canary). A cheap detect-and-report tripwire for stack overflows, complementing the off-stack-buffer hardening."},
    {"notify",   "Post a desktop notification toast. `notify <title> [message...]` pops a small purple Nyx card in the top-right of the desktop (title on the first line, the rest of the arguments joined as the message below) that stays up for a few seconds and then dismisses itself — the compositor keeps repainting while a toast is alive so it fades out on its own. Up to a few toasts stack at once; a new one past that evicts the oldest. Useful for surfacing background events (a finished job, a download) to the user without stealing focus."},
    {"clip",     "A system text clipboard (like pbcopy/pbpaste). `clip <text...>` copies the arguments (joined with spaces) to the clipboard; `clip` with no arguments pastes - prints the current contents; `clip -c` clears it. The clipboard holds up to 4096 bytes and persists across commands, so you can copy a value from one command's output and paste it into another. The same buffer is exposed to the rest of the kernel (clipboard_get/set), so the GUI terminal and editor can be wired to copy/paste through it."},
    {"paste",    "Merge corresponding lines of files. By default the i-th line of each file is printed on one row, separated by a tab, continuing until every file runs out (a spent file leaves its column empty). -s writes each file's lines onto a single line instead. -d LIST replaces the tab with the characters of LIST used in turn (\\t, \\n, \\\\ escapes recognised). Reads each whole file; up to 16 files. Matches GNU paste for newline-delimited text."},
    {"cut",      "Print selected parts of each line of <file>. Choose one mode: -f LIST cuts fields (a field is text between delimiters; the delimiter is a TAB by default, or the single character given by -d C), -c LIST cuts characters, -b LIST cuts bytes. A LIST is a comma-separated set of 1-based ranges: 'N' one position, 'N-M' the inclusive range, 'N-' from N to the end, '-M' the same as '1-M'. Selected parts are always emitted in increasing position order, never duplicated, so the order the ranges are written in does not matter. In field mode a line that contains no delimiter is printed unchanged, unless -s suppresses such lines; selected fields are re-joined with the delimiter. Matches GNU cut byte-for-byte for newline-delimited text (chars and bytes coincide for ASCII). Reads a bounded chunk of each file; several files may be given."},
    {"join",     "Join two files, <f1> and <f2>, on a common field — the relational join of the shell. Both files must already be SORTED on their join field (join is a merge join; sort first). For every pair of lines whose join fields match it prints the join field, then the other fields of <f1>, then the other fields of <f2>; a repeated key prints the full cartesian product of the matching lines. By default the join field is field 1 and fields are separated by runs of blanks (collapsed to a single space on output). -t C uses the single character C as the field separator on input and output. -1 N / -2 N set the join field for file 1 / file 2 (or -j N for both). -a 1 or -a 2 additionally prints the unpaired lines of that file (reformatted). Matches GNU join byte-for-byte under the C locale (byte-ordered keys). Reads a bounded chunk of each file."},
    {"uniq",     "Collapse ADJACENT equal lines of <file> into one — the classic companion to sort (uniq does not sort; it only folds neighbouring duplicates, so run `sort` first to remove all duplicates). Prints the first line of each run. -c prefixes each line with the number of times it occurred (a 7-wide count, then a space). -d prints only lines that repeat (runs of 2+); -u prints only lines that occur exactly once. -i compares case-insensitively. -f N ignores the first N blank-separated fields when comparing, -s N then ignores the next N characters, and -w N compares at most N characters of what remains — the whole (unmodified) line is still what gets printed. Matches GNU uniq byte-for-byte. Reads a bounded chunk of the file."},
    {"tac",      "Print the lines of <file> in reverse order — the last line first, the first line last (the line-order companion to rev, which reverses characters within a line). The newline stays attached to its line, so a file without a trailing newline joins its last two lines when reversed, exactly like GNU tac. -s SEP uses SEP (one or more characters) as the record separator instead of newline. Reads a bounded chunk of the file."},
    {"tsort",    "Topologically sort a dependency list. The file holds whitespace-separated tokens in pairs; each pair 'A B' means A must come before B. Prints one item per line in an order that respects every dependency (Kahn's algorithm), matching GNU tsort byte-for-byte including its tie-break (roots emitted in sorted order). If the pairs contain a cycle the order is impossible, so the acyclic part is printed and a loop is reported; an odd number of tokens is a malformed input. Useful for build/order-of-operations problems."},
    {"csv",      "View a comma-separated-values file as an aligned table. Parses RFC 4180 CSV: fields are separated by commas; a field may be wrapped in double quotes to protect embedded commas, newlines, and quotes (a doubled \"\" inside a quoted field is one literal \"). Columns are padded so they line up, and embedded control characters are shown as spaces. -d DELIM uses DELIM as the field separator instead of a comma (e.g. -d ';' or a tab). Reads a bounded chunk of the file. The same hardened parser backs a self-test (csv) in the boot battery."},
    {"fnv",      "Print the FNV-1a hash of the argument text (all arguments joined by single spaces) as lowercase hex. FNV-1a is a small, fast NON-cryptographic hash used for hash tables and quick content fingerprints; the 32-bit variant is the default and -64 selects the 64-bit variant. It is not collision-resistant, so do not use it where an adversary controls the input (SipHash is the keyed alternative)."},
    {"urlcode",  "Percent-encode the argument text per RFC 3986 (the unreserved set A-Za-z0-9-._~ passes through, every other byte becomes %XX in uppercase hex), or with -d strict-decode a single percent-encoded token back to its bytes. Arguments are joined with single spaces before encoding. The decoder is strict: a '%' not followed by exactly two hex digits is rejected. Backed by the shared codec used for URL handling."},
    {"crc32c",   "Print the CRC-32C (Castagnoli) checksum of the argument text (all arguments joined by single spaces) as 8-digit lowercase hex. CRC-32C is the data-integrity checksum used by ext4 metadata, Btrfs, iSCSI and SCTP — a different, stronger polynomial (0x1EDC6F41) than the zip/PNG CRC-32, and the one modern CPUs accelerate in hardware. crc32c of `123456789` is e3069283."},
    {"murmur",   "Print the MurmurHash3 (x86 32-bit) hash of the argument text (all arguments joined by single spaces) as 8-digit lowercase hex. -s SEED sets the 32-bit seed (decimal, or 0x-prefixed hex). MurmurHash3 is a fast NON-cryptographic hash — the workhorse behind hash tables, bloom filters and content sharding — with excellent avalanche and spread, but it is NOT secure against an adversary choosing inputs (use a keyed hash like SipHash for that). It is a distinct algorithm from FNV-1a, the CRC/Fletcher checksums and the cryptographic hashes. murmur of `test` is ba6bd213 (seed 0); the empty string with seed 1 is 514e28b7. Verified by the boot self-test battery against the canonical vectors."},
    {"fletcher", "Print the Fletcher checksum of the argument text (all arguments joined by single spaces): Fletcher-16 by default as 4-digit hex, or Fletcher-32 with -32 as 8-digit hex. Fletcher's checksum keeps two running modular sums (the second accumulates the first), so its result depends on byte ORDER — it detects most transpositions that a plain sum misses, at a fraction of a CRC's cost. Fletcher-16 folds bytes mod 255; Fletcher-32 folds 16-bit little-endian words mod 65535 (a trailing odd byte is zero-padded). Used by ZFS and several transport/routing protocols. fletcher of `abcde` is c8f0 (and -32 f04fc729). Verified by the boot self-test battery against the canonical vectors."},
    {"bech32",   "Verify and decode a Bech32 (BIP-173) string. Bech32 is the checksummed base-32 encoding used by Bitcoin SegWit and Lightning identifiers: a human-readable prefix (HRP), a '1' separator, the base-32 data symbols, and a 6-symbol checksum computed by a BCH code over GF(32) so that a small number of altered or transposed characters is provably detected — the reason it is safer to transcribe than plain base58. `bech32 <string>` prints the decoded HRP and the data symbols (as hex) when the checksum verifies, or reports the string invalid. Rejects exactly what BIP-173 rejects: mixed upper/lower case, any byte outside printable ASCII, a missing or misplaced separator, an empty HRP, an out-of-charset symbol, a length above 90, or a bad checksum. Verified by the boot self-test battery against the canonical BIP-173 vectors. Pairs with base58, the other Bitcoin encoding."},
    {"base58",   "Base58-encode the argument text (Bitcoin alphabet — the digits and letters minus 0, O, I and l, which are easy to confuse), or with -d decode a base58 string back to its bytes. Base58 writes a byte string as one big-endian base-58 number and renders each leading zero byte as a leading '1', so it is the compact, ambiguity-free encoding used for crypto addresses. Arguments are joined with single spaces before encoding; an out-of-alphabet character is rejected on decode. Verified by the boot self-test battery against the canonical Bitcoin vectors."},
    {"printf",   "Print ARGs under the control of FORMAT (like the C/coreutil printf). FORMAT is reused as needed to consume all ARGs. It interprets backslash escapes (\\n \\t \\r \\a \\b \\f \\v \\\\) and the conversions %s %d %i %u %x %X %c %% with optional flags and field width (e.g. %-10s, %05d). Numeric ARGs are read as decimal. Unlike echo, printf never appends a trailing newline unless FORMAT contains one."},
    {"expand",   "Convert the tabs in <file> to spaces. Each tab advances to the next tab stop, which are spaced N columns apart (8 by default, or -t N), so columns stay aligned instead of a fixed number of spaces per tab. Non-tab characters and newlines pass through unchanged (a newline resets the column count)."},
    {"unexpand", "The inverse of expand: convert runs of spaces in <file> back into tabs, collapsing each blank run to the fewest tabs+spaces at N-column tab stops (8 by default, or -t N). A single space is never turned into a tab. By default only the LEADING blanks of each line are converted (matching GNU unexpand); -a converts blank runs everywhere on the line, and -t N implies -a."},
    {"wc",       "Count the lines, words and characters in <file>. -l, -w or -c limit the output to just one of those counts."},
    {"basename", "Strip the directory prefix (and, if given, a trailing suffix) from <path>, printing only the final component."},
    {"dirname",  "Strip the final component from <path>, printing the directory portion that remains."},
    {"deflate",  "Compress <in> into <out> using DEFLATE with a zlib wrapper (RFC 1950) - the format stock zlib and Python zlib.decompress read. Fixed-Huffman + LZ77; the output is verified to inflate back byte-for-byte, so it can be decompressed anywhere."},
    {"gzip",     "Compress <in> into <out> as a gzip (.gz) file (RFC 1952) - the format stock gunzip, web browsers, and Python gzip.decompress read. Wraps the DEFLATE compressor with a gzip header and a CRC-32 + uncompressed-size trailer."},
    {"ipcalc",   "IPv4 subnet calculator. Give an address as `ipcalc <ip>/<prefix>`, `ipcalc <ip> <netmask>`, or `ipcalc <ip> <prefix>` and it prints the network and broadcast addresses, the netmask and wildcard, the usable host range (HostMin..HostMax) and the host count. RFC-correct at the edges: a /31 has two usable addresses (RFC 3021) and a /32 is a single host."},
    {"calc",     "Evaluate a 64-bit signed integer expression with C operators and precedence: + - * / %, the bitwise/shift operators | ^ & << >>, unary - + ~, parentheses, and decimal / 0x-hex / 0b-binary literals. Division truncates toward zero. Quote or join the terms, e.g. calc (2 + 3) * 4. Reports divide-by-zero, bad syntax, and unbalanced parentheses."},
    {"json",     "Validate or query a JSON document (RFC 8259) read from a file. `json <file>` prints whether it is valid or the byte offset of the first error - strict: rejects trailing commas, unquoted keys, leading zeros, bad escapes, control chars in strings, and NaN/Infinity. `json get <file> <path>` extracts the value at a jq-lite path built from `.name` (object member) and `[N]` (array index) steps, e.g. `json get cfg.json .users[0].name`; `.` selects the whole document; it prints the value's raw JSON (scalar or subtree), or a not-found / type-mismatch / bad-path error. Both parsing and extraction are iterative (O(1) kernel stack), so nesting beyond 256 levels is rejected and even hostile deeply-nested input cannot exhaust the stack."},
    {"xbm",      "The NyxOS package manager. `xbm install <name>` resolves the recipe's `deps:` (installing every dependency first, in topological order — the apt/pacman \"pull the whole tree\" model), compiles each package from its recipe with the in-OS C compiler, installs it, and records a SHA-256 integrity manifest; `xbm verify <name>` re-hashes the installed binary and reports OK or MODIFIED against that manifest (apt/pacman-style tamper detection); `xbm remove <name>` uninstalls it; `xbm deps <name>` resolves and prints the install order from the recipes' `deps:` fields (dependencies before dependents, target last), detecting dependency cycles and missing packages - a topological sort like the one apt/pacman use to order an install. `xbm search <str>` and `xbm list` browse the available and installed packages. A recipe may carry a `url:` line pointing at a remote http:// source; xbm then downloads that source into the package before building it, the pacman/apt \"fetch source, then compile in-OS\" model."},
    {"cc",       "Compile and link C source into an ELF program with the in-OS TinyCC toolchain. -c stops after producing an object file."},
    {"fire",     "Open Nyx Fire, an animated doom-fire effect window. It is also a visual performance demo, re-filling the whole framebuffer region each frame."},
    {"fractal",  "Open Nyx Fractal, a fixed-point Mandelbrot renderer that auto-zooms toward the seahorse valley. A P4 rendering-performance-testing demo: a benchmark HUD shows the measured per-frame render time, the derived render FPS, the current iteration cap, and the zoom factor, so you can watch the software renderer's cost rise and fall with the load. All-integer (Q8.24 fixed point)."},
    {"julia",    "Open Nyx Julia, a fixed-point Julia-set renderer whose constant c orbits a circle so the set continuously morphs. A P4 rendering-performance-testing demo (sibling to `fractal`): its benchmark HUD shows the per-frame render time, render FPS, iteration cap and the live c value, so you can watch the renderer's cost change as the shape moves between connected and disconnected sets. All-integer (Q8.24 fixed point)."},
    {"particles","Open Nyx Particles, a purple particle-fountain and P4 rendering-performance-testing demo. Particles get integer sub-pixel physics (gravity plus floor/wall bounce) and respawn at the nozzle when they settle; the benchmark HUD shows the per-frame simulation time and derived FPS. Press + / - to change the live particle count so the frame-time rises and falls with the load, and r to reset. All-integer (no floats)."},
    {"rotor","Open Nyx Rotor, a spinning 3D point-lattice and P4 rendering-performance-testing demo (the 3D-transform sibling of Nyx Particles). An NxNxN cube of points tumbles on two axes, each point rotated with fixed-point sin/cos, perspective-projected and shaded by depth; the benchmark HUD shows the per-frame render time and derived FPS. Press + / - to change the lattice density so the point count (N^3) and the frame-time scale with the load. All-integer (no floats)."},
    {"fill","Open Nyx Fill, a 2D fill-rate / overdraw P4 rendering-performance-testing demo (the pure-fill sibling of Nyx Rotor and Nyx Fractal). Each frame paints the whole window over N times with a scrolling purple/lilac gradient; the benchmark HUD shows the pixels drawn per frame, the measured frame time, and the derived fill rate in Mpx/s. Press + / - to change the overdraw factor so the pixel throughput and the frame-time scale with the load. All-integer (no floats)."},
    {"life","Open Nyx Life, Conway's Game of Life as a P4 compute/render performance testbed (the cellular-automaton sibling of Nyx Fill and Nyx Fractal). A bounded grid steps by the classic B3/S23 rule and renders live cells as blocks (fresh births glow brighter than survivors); the benchmark HUD shows the generation counter, the live-cell count, the per-frame step time and the derived generations/second. Press + / - to change generations-per-frame so the compute load and the frame-time scale; 'r' reseeds a random soup, 'p' pauses, 'c' clears. All-integer (no floats)."},
    {"blobs","Open Nyx Blobs, a metaballs P4 rendering-performance-testing demo (the organic scalar-field sibling of Nyx Fractal and Nyx Fill). N soft blobs drift and bounce around the window; each frame sums an inverse-square field from every blob at each 2x2 cell and maps it through a purple/lilac ramp, so the blobs glow and merge. The benchmark HUD shows the per-frame field evaluations, the frame time and the rate in Mev/s. Press + / - to change the blob count so the per-sample cost and the frame-time scale. All-integer (no floats)."},
    {"voxel",    "Open Nyx Voxels, an isometric voxel sandbox used as a 3D-render performance testbed (press B for a benchmark HUD)."},
    {"selene",   "Open Selene, the NyxOS web browser (HTTP and TLS, HTML and images)."},
    {"man",      "Display the manual page for a command: its name, a synopsis, and a description of what it does."},
    {"head",     "Write the first lines of <file> to standard output (10 by default, or a count given as the second argument). Useful for peeking at the top of a file without reading the whole thing."},
    {"tail",     "Write the last lines of <file> to standard output (10 by default, or a count given as the second argument)."},
    {"tree",     "Print the directory rooted at [path] (the current directory by default) as an indented tree, showing each subdirectory's immediate children."},
    {"disks",    "List the physical disks attached to the machine and their partitions (aliased as `lsblk`). For each ATA disk it reads the drive's IDENTIFY data (model name + capacity) and then sector 0, parsing the MBR partition table: for every non-empty entry it prints the partition number, type byte + a human label (Linux/EFI System/FAT32/NTFS/...), the starting LBA, the size, and a [boot] flag for an active partition. Read-only — the first rung of the `nyxinstall` disk-management work; partitioning, formatting and installing come later. A raw or GPT-only disk reports that it has no MBR table."},
    {"lspci",    "Enumerate the machine's PCI/PCIe devices via configuration mechanism #1 (the 0xCF8/0xCFC port pair). For each device it prints the bus:slot.func address, the vendor:device ID, and a human label decoded from the class code (Host bridge, Display/VGA, Ethernet, USB, IDE/SATA AHCI/NVMe storage, ...). Storage controllers additionally show their class:subclass:prog-if triple, revision and BAR0. This is the hardware-recon step for real machines: on a modern UEFI laptop the disk is an NVMe controller on PCIe, which the ATA-based `disks` command cannot see, so `lspci` is how you locate it (and read the BAR a future NVMe driver will map). Read-only."},
    {"nvme",     "Bring up the machine's NVMe disk controller. Finds it by PCI class 01:08 (`lspci`), reads its BAR0 (handling a 64-bit memory BAR) and maps that MMIO register window uncached, then resets the controller (clears CC.EN, waits for CSTS.RDY=0) and re-enables it with a freshly allocated admin submission/completion queue pair, waiting for CSTS.RDY=1. Prints the vendor:device, NVMe version, and the decoded CAP fields (max queue entries, doorbell stride, timeout, min page size). NVMe is a standard register interface, so this drives both QEMU's emulated `-device nvme` and a real SSD. It also sets up an I/O queue pair. `nvme read <lba>` then reads one logical block and hex-dumps its first bytes — READ-ONLY: the command never writes to the disk (installing to the NVMe happens only through an explicit, confirmed nyxinstall). NVMe is a standard register interface, so this drives both QEMU's emulated `-device nvme` and a real SSD."},
    {"mkfs",     "Format a fresh ext2 filesystem onto a disk (the 3rd nyxinstall rung, after nyxpart). `mkfs <drive> [part_lba] [size_MB]` writes a valid single-block-group ext2 (1024-byte blocks) at part_lba (default LBA 2048, matching `nyxpart`): superblock, block-group descriptor, block + inode bitmaps, inode table, the root directory and lost+found. The layout is byte-for-byte a real ext2 (verified against Linux `fsck.ext2` at 1 through 13 block groups). Multi-block-group is supported (a superblock + descriptor-table backup at the start of every group), so volumes up to 256 MB work; beyond that is capped for now. Destructive — it erases the target area. After it finishes, `mount <drive> <part_lba>` mounts the new filesystem on /mnt. Typical install flow: `nyxpart 0 new` -> `mkfs 0 2048` -> `mount 0 2048`."},
    {"nyxinstall","Guided installer that puts NyxOS onto a disk in one command — the `archinstall`-style capstone of the disk-management tools. `nyxinstall <drive> confirm [srcdir]` runs the whole flow: [1] write a fresh MBR with one bootable Linux partition spanning the disk, [2] mkfs a multi-block-group ext2 filesystem on it, [3] mount it at /mnt, [4] recursively copy the source tree (srcdir, default /bin) onto it with `cp -r`. Each step prints progress and the run ends with a file/directory count. DESTRUCTIVE: it erases the whole target drive, so it refuses to run unless the literal word `confirm` is given as the second argument. The individual steps are also available on their own (`disks`, `nyxpart`, `mkfs`, `mount`, `cp -r`). Installing a bootloader so the disk boots standalone, and drivers for modern SATA/NVMe hardware, are separate later steps."},
    {"nyxpart",  "Write a fresh MBR partition table to a disk (the first WRITE step of nyxinstall's disk management). `nyxpart <drive> new [size_MB]` erases the drive's existing partition table and writes a single bootable Linux (0x83) partition, 1 MiB-aligned (starting at LBA 2048), spanning the whole disk or the given size in megabytes. The destructive action requires the explicit `new` keyword. Pair it with `disks`/`lsblk` to see the result. WARNING: this overwrites the partition table on the selected drive — only run it on a disk you intend to repartition."},
    {"nyxgpt",   "Write a UEFI GUID Partition Table (GPT) to a disk — the partitioning a modern UEFI machine boots from, versus the legacy MBR `nyxpart` writes. `nyxgpt <drive> confirm` erases the existing table and lays down a protective MBR, primary + backup GPT headers (each with the two CRC-32s the spec mandates) and a 128-entry partition array describing two partitions: a 64 MiB EFI System Partition and a NyxOS partition filling the rest. DESTRUCTIVE: requires the literal `confirm`. Validated against Linux `sgdisk -v`/`gdisk -l`. This is the first rung of the UEFI-install path (the ESP is formatted FAT and given a GRUB-EFI loader in later steps); `disks` lists a GPT disk's partitions."},
    {"nyxfat",   "Format a disk's EFI System Partition as FAT32 and populate it for UEFI boot — rung E2 of the UEFI-install path (after `nyxgpt`). `nyxfat <drive> confirm` locates the ESP in the disk's GPT, writes a spec-correct FAT32 volume (boot sector/BPB with the FATSz32 geometry, FSInfo, two FATs, backups) and creates the /EFI/BOOT directory tree containing BOOTX64.EFI (a real embedded GRUB x86_64-efi loader) + grub.cfg — the path a UEFI firmware boots. DESTRUCTIVE: erases the ESP, requires the literal `confirm`, and needs `nyxgpt` to have written the GPT first. The FAT layout is validated on the host with `fsck.fat` and mtools."},
    {"du",       "Disk usage: sum the sizes of every file in the subtree rooted at [path] (the current directory by default) and print the total, both human-readable (B/K/M/G) and in exact bytes, with a file and directory count. The walk is iterative with a bounded off-stack frontier (the 4 KB kernel task stack forbids deep recursion); a subtree wider than that many pending directories reports the total as a floor. Complements `df`, which reports whole-filesystem free space rather than per-directory usage."},
    {"find",     "Search for files whose name matches <name>, starting at [path] (the current directory by default), and print the path of every match."},
    {"touch",    "Create <file> as a new, empty file if it does not already exist."},
    {"which",    "Look <name> up as a shell command and report how it would run: as a built-in, or as the program at a particular path."},
    {"env",      "Print the shell's environment variables, one NAME=value pair per line."},
    {"export",   "Set an environment variable: `export NAME=value`. Exported variables are passed on to the programs the shell runs."},
    {"ps",       "List the running processes with their PID, scheduler state and name. Use kill to stop one."},
    {"kill",     "Terminate the process with the given <pid>. Run ps first to find the pid you want."},
    {"mem",      "Show a summary of physical memory: how much is in use, how much is free, and the total the kernel manages."},
    {"df",       "Report the total, used and available space of the mounted ext2 filesystem on /mnt."},
    {"screenshot","Save a snapshot of the whole screen to an image file: `screenshot [path]` (default /tmp/screenshot.png; pass /mnt/shot.png to keep it on disk). If the path ends in .png the image is written as a real PNG (8-bit RGB, DEFLATE-compressed - small and universally viewable); any other extension writes an uncompressed binary PPM (Netpbm P6). Captured from the framebuffer the compositor last presented, at the current resolution."},
    {"mount",    "Mount an ext2 filesystem onto /mnt. With no arguments it probes the first ATA disk; [drive] and [part_lba] select a specific disk and partition."},
    {"date",     "Print the current date and time from the CMOS real-time clock. With a `+FORMAT` argument, format it strftime-style: %Y %y %m %d %e %H %I %M %S %p (year/month/day/hour/min/sec), %A/%a (weekday), %B/%b (month name), %j (day of year), %% — e.g. `date +%A` or `date +%Y-%m-%d`."},
    {"uname",    "Print system information: the operating-system name, its version and the machine architecture."},
    {"nyxfetch", "Print a system summary beside the NyxOS logo: the version, uptime, memory use and other details. `fastfetch` is an alias."},
    {"clear",    "Clear the terminal and move the cursor back to the top-left corner."},
    {"history",  "List the most recently entered shell commands, oldest first."},
    {"reboot",   "Restart the machine immediately."},
    {"cpus",     "List the logical CPU cores the kernel started (SMP), with each core's id and state."},
    {"hexdump",  "Print a side-by-side hex and ASCII dump of memory, starting at <addr> for [bytes] bytes (256 by default)."},
    {"ifconfig", "Show each network interface with its IPv4 address, netmask and gateway. Configure one with dhcp or setip."},
    {"arp",      "Show the ARP cache: the IPv4 -> MAC address mappings the stack has resolved for neighbours on the local link (the DHCP gateway, a `ping`/`dns`/`httpget` target). Entries are filled on demand by ARP resolution and looked up when sending each frame; a host that has not been contacted yet will not appear. Read-only view of what `arp -a` shows on Linux."},
    {"dhcp",     "Obtain an IPv4 address, netmask and gateway for the network interface automatically from a DHCP server."},
    {"dns",      "Resolve the hostname <hostname> to an IPv4 address using the configured DNS server."},
    {"ping",     "Send ICMP echo requests to <ip|hostname> and report the replies, to check whether a host is reachable."},
    {"write",    "Write <text> to <file>, creating or replacing it. The words after the filename are joined with single spaces and a trailing newline is added."},
    {"open",     "Open <file> in the graphical text editor."},
    {"files",    "Open the graphical File Manager, browsing [path] (the filesystem root by default)."},
    {"imageview","Open the image at <path> (PNG, BMP, GIF or JPEG) in a picture-viewer window."},
    {"spawn",    "Run the program <file> in the background and return to the prompt at once; manage it with jobs and wait."},
    {"exec",     "Run the program <file> in the foreground, waiting for it to finish before the prompt returns."},
    {"jobs",     "List the background jobs started with spawn, with their pid and state."},
    {"wait",     "Wait for background jobs to finish; given a [pid], wait only for that one."},
    {"layout",   "Switch the keyboard layout: `layout us` or `layout es` (the US or Spanish key mapping)."},
    {"beep",     "Sound a tone through the PC speaker at [freq] Hz for [ms] milliseconds."},
    {"imgreject","Run the adversarial image-decoder self-test: check that the PNG, BMP, GIF and JPEG decoders refuse malformed or hostile input instead of crashing."},
    {"httptest","Run the HTTP-response parser robustness self-test: feed http_parse_response crafted responses (chunked, header-less, hostile oversized/overflowing chunk sizes, Content-Length larger than the body) and check it stays bounds-safe. The parser is on the xbm package-fetch path, so it handles untrusted server data."},
    {"ext2test","Run the ext2 directory-scanner self-test: feed the on-disk directory-block parser crafted blocks (valid entries plus hostile ones - a zero record length, a length past the block, a 255-byte name near the block end) and check it finds the valid names and stays bounds-safe on the corrupt ones. A filesystem driver's normal input includes corrupt or hostile disk images."},
    {"desktop",  "Start the NyxOS graphical desktop: the window compositor with the taskbar, Start menu, draggable windows and the app/game launchers. This is the main GUI; `gui` is a lighter demo."},
    {"gui",      "Launch a small GUI demo with a working mouse cursor — a quick way to exercise the framebuffer, pointer and window drawing without bringing up the full `desktop`."},
    {"doom",     "Play the original 1993 DOOM (shareware episode) in a window: the software renderer runs E1M1 with the status-bar HUD. Move and fire from the keyboard; Ctrl-C quits back to the shell. Also launchable from the desktop Games folder."},
    {"snake",    "Play Snake: steer the snake with the arrow keys or WASD, eat the food to grow, and avoid running into the walls or your own tail. A window game (also in the desktop Games folder)."},
    {"tetris",   "Play Tetris: the arrow keys move and rotate the falling piece, Space hard-drops it, and completing a full row clears it. Speed rises as you go."},
    {"pong",     "Play Pong: steer your paddle with the mouse or the arrow keys and rally the ball past the opponent."},
    {"lava",     "Open `Nyx Lava`, an animated plasma / lava-lamp window — four phase-shifted integer sine waves cycled through a heat palette (all fixed-point, no floats). Eye-candy that also stresses the framebuffer fill as a small render benchmark."},
    {"matrix",   "Open `Nyx Matrix`, a cmatrix-style green code-rain window: per-column falling glyph streams with a bright head and a fading tail. Eye-candy and a light framebuffer stress demo."},
    {"play",     "Play a short built-in demo melody through the sound device, a quick way to confirm audio output is working."},
    {"sb16play", "Test Sound Blaster 16 playback: `sb16play [freq] [ms]` emits a tone at [freq] Hz for [ms] milliseconds through the SB16 DMA path (defaults if omitted), verifying the audio driver end to end."},
    {"fastfetch","Alias for `nyxfetch`: print the NyxOS logo beside a summary of the running system — kernel version, CPU, memory and uptime — the neofetch-style banner."},
    {"httpget",  "Fetch a URL over HTTP and print the response: `httpget <url>`. Resolves the host via DNS, opens a TCP connection, sends the GET and shows the status and body. Plain HTTP only; HTTPS is handled by the TLS stack (see `tls`)."},
    {"tls",      "Run a TLS 1.2 handshake test against a host: `tls <host>` connects on :443 (HTTPS), performs the full handshake and certificate-chain verification, and reports the negotiated connection — a diagnostic for the crypto/TLS stack."},
    {"setip",    "Assign a static IPv4 configuration to the network interface: `setip <ip> <mask> <gw>` (address, netmask, gateway). Use instead of DHCP when you need a fixed address; check the result with `ifconfig`."},
    {"setres",   "Set the screen resolution, e.g. `setres 800 600`. Picks the matching VBE mode and re-inits the framebuffer; the desktop redraws at the new size. See `mode` for full width/height/bpp control."},
    {"mode",     "Set a VBE video mode explicitly: `mode <width> <height> <bpp>`. Lower level than `setres` — you choose the colour depth too. An unsupported combination is rejected and the current mode is kept."},
    {"useradd",  "Create a user account: `useradd <user> <pass>`. The credentials are stored (salted-hash) in the auth database so the user can sign in at the login screen; list existing accounts with `users`."},
    {"users",    "List the user accounts known to the login system (the accounts created with `useradd` / the login screen's Create Account)."},
    {"version",  "Print the running NyxOS kernel version string (also shown by `nyxfetch` and in the desktop's About)."},
    {"nice",     "Spawn a program at a chosen scheduler weight: `nice <weight> <file>`. A higher weight gets more CPU time from the weighted scheduler; the program then runs like `spawn`. Adjust a running process later with `renice`."},
    {"renice",   "Change the scheduler weight of a running process: `renice <pid> <weight>` (find the pid with `ps`). Raises or lowers how much CPU time the weighted scheduler gives it."},
    {"pkg",      "Alias for `xbm`, the NyxOS package manager (its pacman/apt): install/remove/update/search/list packages, compiling them from a recipe with the in-OS `cc`. See `man xbm` for the full verb set."},
    {"ext2ls",   "List a directory on the mounted ext2 disk image (/mnt) — a convenience alias into the `mount` tool's ext2 browser. Use it to see files persisted on the disk across reboots."},
    {"ext2cat",  "Print the contents of a file stored on the mounted ext2 disk image — a convenience alias into the `mount` tool. The disk-backed counterpart of `cat`."},
};

static void cmd_man(int argc, char** argv) {
    if (argc < 2) { printf("What manual page do you want? Try 'man <command>'.\n"); return; }
    const char* name = argv[1];

    const command_t* cmd = NULL;
    for (int i = 0; commands[i].name != NULL; i++)
        if (strcmp(commands[i].name, name) == 0) { cmd = &commands[i]; break; }
    if (!cmd) { printf("No manual entry for %s\n", name); return; }

    // The one-line help reads "Short description: synopsis". Split on the first
    // ':' so the description and the usage line land in the right man sections.
    const char* help  = cmd->help;
    const char* colon = strchr(help, ':');
    char shortdesc[96], synopsis[96];
    if (colon) {
        int dl = (int)(colon - help); if (dl > 95) dl = 95;
        memcpy(shortdesc, help, dl); shortdesc[dl] = '\0';
        const char* s = colon + 1; while (*s == ' ') s++;
        int sl = (int)strlen(s); if (sl > 95) sl = 95;
        memcpy(synopsis, s, sl); synopsis[sl] = '\0';
    } else {
        int hl = (int)strlen(help); if (hl > 95) hl = 95;
        memcpy(shortdesc, help, hl); shortdesc[hl] = '\0';
        synopsis[0] = '\0';
    }

    const char* body = NULL;
    for (int i = 0; i < (int)(sizeof(man_pages) / sizeof(man_pages[0])); i++)
        if (strcmp(man_pages[i].name, name) == 0) { body = man_pages[i].body; break; }

    printf("NAME\n       %s - %s\n\n", name, shortdesc);
    printf("SYNOPSIS\n       %s\n\n", synopsis[0] ? synopsis : name);
    printf("DESCRIPTION\n       %s\n", body ? body : shortdesc);
}

static void cmd_version(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s %s (%s)\n", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
}

static void cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    clear_screen();
}

// Change the screen mode from the shell — the same thing the Settings Display
// tab does, through the same function, so the two can never disagree.
//
// The mode list is a whitelist on purpose: vbe_set_mode() validates nothing and
// programs whatever numbers it is handed straight into the VBE registers, so a
// typo like `setres 640 4800` would leave the machine with no usable display and
// no way to type the command that would fix it.
static void cmd_setres(int argc, char** argv) {
    static const struct { uint32_t w, h; } modes[] = {
        {640,480}, {800,600}, {1024,768}, {1280,720}
    };
    const int nmodes = (int)(sizeof(modes) / sizeof(modes[0]));

    if (argc != 3) {
        printf("Usage: setres <width> <height>\n");
        printf("Current: %ux%u\n", fb_get_width(), fb_get_height());
        printf("Modes:");
        for (int i = 0; i < nmodes; i++) printf(" %ux%u", modes[i].w, modes[i].h);
        printf("\n");
        return;
    }

    uint32_t w = (uint32_t)atoi(argv[1]);
    uint32_t h = (uint32_t)atoi(argv[2]);
    for (int i = 0; i < nmodes; i++) {
        if (modes[i].w == w && modes[i].h == h) {
            display_set_mode(w, h);
            return;
        }
    }
    printf("setres: unsupported mode %ux%u (try `setres` for the list)\n", w, h);
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

// basename PATH [SUFFIX] — POSIX: strip the directory prefix (and an optional
// trailing SUFFIX) from PATH. Matches GNU: "/usr/lib"->"lib", "/usr/"->"usr",
// "/"->"/", ""->"" (empty line), "/a/b.txt" ".txt"->"b". The suffix is only
// removed when it is a proper (shorter) suffix, so `basename .txt .txt` keeps
// ".txt". Works on a bounded local copy — the argv strings come from the fixed
// command-line buffer, but the copy makes the length cap explicit.
static void cmd_basename(int argc, char** argv) {
    if (argc < 2) { printf("usage: basename <path> [suffix]\n"); return; }
    const char* src = argv[1];
    if (src[0] == '\0') { printf("\n"); return; }        // GNU basename "" -> empty line
    char buf[256];
    size_t n = strlen(src);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, src, n); buf[n] = '\0';
    int len = (int)n;
    while (len > 0 && buf[len - 1] == '/') buf[--len] = '\0';   // drop trailing slashes
    if (len == 0) { printf("/\n"); return; }             // input was all slashes
    char* base = buf;
    for (int i = 0; i < len; i++) if (buf[i] == '/') base = &buf[i + 1];
    if (argc >= 3 && argv[2][0]) {                       // optional suffix removal
        int bl = (int)strlen(base), sl = (int)strlen(argv[2]);
        if (sl < bl && strcmp(base + bl - sl, argv[2]) == 0) base[bl - sl] = '\0';
    }
    printf("%s\n", base);
}

// dirname PATH — POSIX: strip the last component from PATH. Matches GNU:
// "/usr/lib"->"/usr", "/usr/"->"/", "usr"->".", "/"->"/", ""->".", "a/b/c"->"a/b".
static void cmd_dirname(int argc, char** argv) {
    if (argc < 2) { printf("usage: dirname <path>\n"); return; }
    const char* src = argv[1];
    if (src[0] == '\0') { printf(".\n"); return; }
    char buf[256];
    size_t n = strlen(src);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, src, n); buf[n] = '\0';
    int len = (int)n;
    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';   // trailing slashes (keep a lone '/')
    int last = -1;
    for (int i = 0; i < len; i++) if (buf[i] == '/') last = i;
    if (last < 0)  { printf(".\n"); return; }            // no slash -> current dir
    if (last == 0) { printf("/\n"); return; }            // slash only at root
    buf[last] = '\0';
    while (last > 1 && buf[last - 1] == '/') buf[--last] = '\0';  // collapse trailing slashes of the result
    printf("%s\n", buf);
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

// file <path>... — identify each file's type from its leading bytes (magic numbers)
// and, for text, a recognised source extension. Reads a bounded prefix like head/wc.
extern const char* filetype_identify(const char* name, const uint8_t* data, uint32_t len);
static void cmd_file(int argc, char** argv) {
    if (argc < 2) { printf("Usage: file <file>...\n"); return; }
    for (int a = 1; a < argc; a++) {
        int fd = vfs_open(argv[a], 0, 0);
        if (fd < 0) { printf("%s: cannot open\n", argv[a]); continue; }
        uint8_t buf[512];
        int n = vfs_read(fd, (char*)buf, sizeof(buf));
        vfs_close(fd);
        if (n < 0) n = 0;
        printf("%s: %s\n", argv[a], filetype_identify(argv[a], buf, (uint32_t)n));
    }
}

// tar t[v] <file.tar> — list the members of a POSIX ustar archive (v also shows the
// type + size). Listing only. Uses the whole in-memory file (vfs_fdata), like the ELF
// loader, and the hardened tar_next() walker (kernel/fs/tar.c).
static void cmd_tar(int argc, char** argv) {
    if (argc < 3 || argv[1][0] != 't') {
        printf("Usage: tar t[v] <file.tar>   (list an archive; extraction not supported)\n");
        return;
    }
    int verbose = 0;
    for (int i = 0; argv[1][i]; i++) if (argv[1][i] == 'v') verbose = 1;
    int fd = vfs_open(argv[2], 0, 0);
    if (fd < 0) { printf("tar: cannot open '%s'\n", argv[2]); return; }
    uint32_t size = vfs_fsize(fd);
    uint8_t* data = vfs_fdata(fd);
    if (!data || size < 512) { vfs_close(fd); printf("tar: not a tar archive\n"); return; }
    tar_entry_t e;
    uint32_t off = 0;
    int n = 0;
    while (n < 4096 && tar_next(data, size, &off, &e)) {
        if (verbose) printf("%c %10u  %s\n", e.type ? e.type : '0', e.size, e.name);
        else         printf("%s\n", e.name);
        n++;
    }
    vfs_close(fd);
    if (n == 0) printf("tar: not a ustar archive\n");
}

// iniget <file> <section|-> <key> — print one value from an INI/.conf file. A section
// of "-" (or "") reads the global section (keys before any [section]). Uses the whole
// in-memory file (vfs_fdata) + the hardened ini_get() reader (kernel/core/ini.c).
static void cmd_iniget(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: iniget <file> <section|-> <key>\n");
        return;
    }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("iniget: cannot open '%s'\n", argv[1]); return; }
    uint32_t size = vfs_fsize(fd);
    uint8_t* data = vfs_fdata(fd);
    if (!data) { vfs_close(fd); printf("iniget: cannot read '%s'\n", argv[1]); return; }
    const char* sec = (argv[2][0] == '-' && argv[2][1] == 0) ? "" : argv[2];
    char val[256];
    int found = ini_get((const char*)data, (int)size, sec, argv[3], val, (int)sizeof(val));
    vfs_close(fd);
    if (found) printf("%s\n", val);
    else       printf("iniget: '%s' not found in [%s]\n", argv[3], sec[0] ? sec : "(global)");
}

// factor N [N ...] — print the prime factorization of each integer, GNU factor style:
// "N: p1 p2 ..." (ascending, with multiplicity). Strict parse via parse_u64(); the
// factoring core is factor_one() (kernel/core/factor.c).
// --- strings: streaming printable-run extractor (shared by `strings` + its KAT) ---
// A "printable" byte is 0x20..0x7E or TAB; any other byte ends the current run,
// which is emitted (via the sink callback) only if it reached `minlen`. Runs longer
// than the fixed buffer are split at the cap and continued, so nothing is lost and
// the buffer stays bounded — the state can stream a file in arbitrary-sized chunks
// with an in-progress run carried across calls (strings_finish flushes the tail).
#define STRINGS_MAX_RUN 512
typedef struct {
    char  run[STRINGS_MAX_RUN];
    int   run_len;
    int   minlen;
    void (*emit)(const char* s, void* ctx);
    void* ctx;
} strings_state_t;

static void strings_feed(strings_state_t* s, const uint8_t* buf, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = buf[i];
        if ((c >= 0x20 && c <= 0x7E) || c == '\t') {
            s->run[s->run_len++] = (char)c;
            if (s->run_len >= STRINGS_MAX_RUN - 1) {          // cap reached: emit + continue
                s->run[s->run_len] = 0;
                if (s->run_len >= s->minlen) s->emit(s->run, s->ctx);
                s->run_len = 0;
            }
        } else {
            if (s->run_len >= s->minlen) { s->run[s->run_len] = 0; s->emit(s->run, s->ctx); }
            s->run_len = 0;
        }
    }
}
static void strings_finish(strings_state_t* s) {
    if (s->run_len >= s->minlen) { s->run[s->run_len] = 0; s->emit(s->run, s->ctx); }
    s->run_len = 0;
}

static void strings_emit_print(const char* s, void* ctx) { (void)ctx; printf("%s\n", s); }

// strings [-n MIN] <file> — print each run of >=MIN (default 4) printable characters
// in a file, one per line: the classic way to eyeball text inside a binary (an ELF,
// an image, a package). Streams the file in fixed chunks so even a large binary needs
// no whole-file buffer; an in-progress run carries across chunk boundaries.
static void cmd_strings(int argc, char** argv) {
    const char* path = 0; int minlen = 4;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            minlen = atoi(argv[++i]); if (minlen < 1) minlen = 1;
        } else if (!path) path = argv[i];
    }
    if (!path) { printf("Usage: strings [-n MIN] <file>\n"); return; }
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) { printf("strings: %s: cannot open\n", path); return; }
    static strings_state_t st;
    static uint8_t buf[512];
    st.run_len = 0; st.minlen = minlen; st.emit = strings_emit_print; st.ctx = 0;
    // vfs_pread is offset-aware (0 at EOF); advance the offset ourselves. Cap the
    // total so an endless special (/dev/zero, /dev/random) can't loop forever.
    uint32_t off = 0; const uint32_t cap = 64u * 1024u * 1024u;
    int n;
    while (off < cap && (n = vfs_pread(fd, buf, sizeof(buf), off)) > 0) {
        strings_feed(&st, buf, (uint32_t)n);
        off += (uint32_t)n;
    }
    strings_finish(&st);
    vfs_close(fd);
}

// sha256sum <file>... — print each file's SHA-256 digest as `<64-hex>  <name>` (the
// GNU format: digest, two spaces, name), the standard file-integrity check for a
// package-managing OS. Streams each file through the hash in fixed chunks via the
// offset-aware vfs_pread — no whole-file buffer, so a large binary hashes fine — and
// caps the total so an endless special (/dev/zero) can't spin forever.
static void cmd_sha256sum(int argc, char** argv) {
    if (argc < 2) { printf("Usage: sha256sum <file>...\n"); return; }
    for (int a = 1; a < argc; a++) {
        int fd = vfs_open(argv[a], 0, 0);
        if (fd < 0) { printf("sha256sum: %s: cannot open\n", argv[a]); continue; }
        sha256_ctx_t ctx; sha256_init(&ctx);
        static uint8_t buf[512];
        uint32_t off = 0; const uint32_t cap = 256u * 1024u * 1024u;
        int n;
        while (off < cap && (n = vfs_pread(fd, buf, sizeof(buf), off)) > 0) {
            sha256_update(&ctx, buf, (uint32_t)n);
            off += (uint32_t)n;
        }
        vfs_close(fd);
        uint8_t dg[SHA256_DIGEST_SIZE]; sha256_final(&ctx, dg);
        char hex[SHA256_DIGEST_SIZE * 2 + 1]; sha256_to_hex(dg, hex);
        printf("%s  %s\n", hex, argv[a]);
    }
}

// hmac <key> <message> — HMAC-SHA256 of <message> keyed by <key>, printed as 64 lowercase
// hex digits. HMAC is the standard keyed-hash MAC: signing API requests, JWT "HS256"
// tokens, verifying webhook payloads. Wraps the KAT'd hmac_sha256() (RFC 2104 / FIPS 198);
// key and message are taken as text argv (the common case — an ASCII/base64 secret).
static void cmd_hmac(int argc, char** argv) {
    if (argc != 3) { printf("Usage: hmac <key> <message>\n"); return; }
    uint8_t mac[SHA256_DIGEST_SIZE];
    hmac_sha256((const uint8_t*)argv[1], (uint32_t)strlen(argv[1]),
                (const uint8_t*)argv[2], (uint32_t)strlen(argv[2]), mac);
    char hex[SHA256_DIGEST_SIZE * 2 + 1]; sha256_to_hex(mac, hex);
    printf("%s\n", hex);
}

static void cmd_factor(int argc, char** argv) {
    if (argc < 2) { printf("Usage: factor N [N ...]\n"); return; }
    for (int i = 1; i < argc; i++) {
        uint64_t n;
        if (parse_u64(argv[i], &n) != 0) {
            printf("factor: '%s' is not a valid positive integer\n", argv[i]);
            continue;
        }
        uint64_t f[64];
        int k = factor_one(n, f, 64);
        printf("%llu:", (unsigned long long)n);                      // %llu: n/factors span the full 64-bit range
        for (int j = 0; j < k; j++) printf(" %llu", (unsigned long long)f[j]);
        printf("\n");
    }
}

// isprime N [N ...] — test each argument for primality with the exact 64-bit
// deterministic Miller-Rabin (is_prime_u64, factor.c): "N is prime" / "N is not
// prime". Fast even for large 64-bit values where `factor` (which must find the
// factors) would be slow.
static void cmd_isprime(int argc, char** argv) {
    if (argc < 2) { printf("Usage: isprime N [N ...]\n"); return; }
    for (int i = 1; i < argc; i++) {
        uint64_t n;
        if (parse_u64(argv[i], &n) != 0) {
            printf("isprime: '%s' is not a valid positive integer\n", argv[i]);
            continue;
        }
        printf("%llu is %s\n", (unsigned long long)n, is_prime_u64(n) ? "prime" : "not prime");
    }
}

// semver <ver> [<ver2>] — validate one SemVer 2.0.0 string, or compare two by precedence.
// Backed by the strict parser + comparator in kernel/core/semver.c.
static void cmd_semver(int argc, char** argv) {
    if (argc == 2) {
        semver_t v;
        if (semver_parse(argv[1], &v) == 0) {
            char pbuf[64]; int m = v.pre_len; if (m > 63) m = 63;
            for (int i = 0; i < m; i++) pbuf[i] = v.pre[i];
            pbuf[m] = 0;
            printf("%s: valid (%lu.%lu.%lu%s%s)\n", argv[1],
                   (unsigned long)v.major, (unsigned long)v.minor, (unsigned long)v.patch,
                   m ? " pre=" : "", pbuf);
        } else {
            printf("%s: invalid semver\n", argv[1]);
        }
        return;
    }
    if (argc == 3) {
        semver_t a, b;
        if (semver_parse(argv[1], &a) != 0) { printf("semver: '%s' is not valid semver\n", argv[1]); return; }
        if (semver_parse(argv[2], &b) != 0) { printf("semver: '%s' is not valid semver\n", argv[2]); return; }
        int c = semver_cmp(&a, &b);
        printf("%s %s %s\n", argv[1], c < 0 ? "<" : (c > 0 ? ">" : "="), argv[2]);
        return;
    }
    printf("Usage: semver <version> [<version2>]   (validate, or compare precedence)\n");
}

// fnv [-64] <text ...> — FNV-1a hash of the argument text (args joined by single
// spaces), as lowercase hex. 32-bit by default; -64 selects the 64-bit variant. A
// fast non-crypto fingerprint — see kernel/crypto/fnv.h. Hex is formatted by hand to
// stay independent of printf width/length support.
static void cmd_fnv(int argc, char** argv) {
    static const char* HEX = "0123456789abcdef";
    int bits = 32, start = 1;
    if (start < argc && strcmp(argv[start], "-64") == 0) { bits = 64; start++; }
    if (start >= argc) { printf("Usage: fnv [-64] <text ...>\n"); return; }
    char out[17];
    if (bits == 32) {
        uint32_t h = FNV1A_32_INIT;
        for (int i = start; i < argc; i++) {
            if (i > start) h = fnv1a_32_update(h, " ", 1);       // rejoin args with spaces
            uint32_t n = 0; while (argv[i][n]) n++;
            h = fnv1a_32_update(h, argv[i], n);
        }
        for (int i = 7; i >= 0; i--) { out[i] = HEX[h & 0xF]; h >>= 4; }
        out[8] = 0;
    } else {
        uint64_t h = FNV1A_64_INIT;
        for (int i = start; i < argc; i++) {
            if (i > start) h = fnv1a_64_update(h, " ", 1);
            uint32_t n = 0; while (argv[i][n]) n++;
            h = fnv1a_64_update(h, argv[i], n);
        }
        for (int i = 15; i >= 0; i--) { out[i] = HEX[h & 0xF]; h >>= 4; }
        out[16] = 0;
    }
    printf("%s\n", out);
}

// urlcode [-d] <text ...> — percent-encode the argument text (args joined by single
// spaces), or with -d strict-decode a single percent-encoded token. Backed by the
// shared RFC 3986 codec in kernel/core/urlcodec.c.
static void cmd_urlcode(int argc, char** argv) {
    int dec = 0, start = 1;
    if (start < argc && strcmp(argv[start], "-d") == 0) { dec = 1; start++; }
    if (start >= argc) { printf("Usage: urlcode [-d] <text ...>\n"); return; }
    if (dec) {
        uint32_t n = 0; while (argv[start][n]) n++;           // a %XX token has no spaces
        uint8_t out[256];
        int dl = url_pct_decode(argv[start], n, out, sizeof(out));
        if (dl < 0) { printf("urlcode: malformed percent-encoding\n"); return; }
        for (int i = 0; i < dl; i++) putchar((char)out[i]);
        putchar('\n');
    } else {
        char in[256]; uint32_t o = 0;
        for (int i = start; i < argc; i++) {                  // rejoin args with spaces
            if (i > start && o < sizeof(in)) in[o++] = ' ';
            for (uint32_t k = 0; argv[i][k] && o < sizeof(in); k++) in[o++] = argv[i][k];
        }
        char out[768];
        if (url_pct_encode((const uint8_t*)in, o, out, sizeof(out)) < 0) { printf("urlcode: input too long\n"); return; }
        printf("%s\n", out);
    }
}

// crc32c <text ...> — CRC-32C (Castagnoli) of the argument text (args joined by single
// spaces), as 8-digit lowercase hex. Backed by crc32c_calc; hex is hand-formatted to
// stay independent of printf width support.
static void cmd_crc32c(int argc, char** argv) {
    static const char* HEX = "0123456789abcdef";
    if (argc < 2) { printf("Usage: crc32c <text ...>\n"); return; }
    uint8_t in[256]; uint32_t o = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1 && o < sizeof(in)) in[o++] = ' ';                      // rejoin args with spaces
        for (uint32_t k = 0; argv[i][k] && o < sizeof(in); k++) in[o++] = (uint8_t)argv[i][k];
    }
    uint32_t c = crc32c_calc(in, o);
    char out[9];
    for (int i = 7; i >= 0; i--) { out[i] = HEX[c & 0xF]; c >>= 4; }
    out[8] = 0;
    printf("%s\n", out);
}

// fletcher [-32] <text ...> — Fletcher-16 (default) or Fletcher-32 checksum of the joined args,
// printed as fixed-width lowercase hex (4 / 8 digits). Backed by the KAT'd fletcher16/32.
static void cmd_fletcher(int argc, char** argv) {
    static const char* HEX = "0123456789abcdef";
    int use32 = 0, start = 1;
    if (start < argc && strcmp(argv[start], "-32") == 0) { use32 = 1; start++; }
    if (start >= argc) { printf("Usage: fletcher [-32] <text ...>\n"); return; }
    uint8_t in[512]; uint32_t o = 0;
    for (int i = start; i < argc; i++) {
        if (i > start && o < sizeof(in)) in[o++] = ' ';                 // rejoin args with spaces
        for (uint32_t k = 0; argv[i][k] && o < sizeof(in); k++) in[o++] = (uint8_t)argv[i][k];
    }
    if (use32) {
        uint32_t c = fletcher32(in, o);
        char out[9];
        for (int i = 7; i >= 0; i--) { out[i] = HEX[c & 0xF]; c >>= 4; }
        out[8] = 0; printf("%s\n", out);
    } else {
        uint16_t c = fletcher16(in, o);
        char out[5];
        for (int i = 3; i >= 0; i--) { out[i] = HEX[c & 0xF]; c >>= 4; }
        out[4] = 0; printf("%s\n", out);
    }
}

// murmur [-s SEED] <text ...> — MurmurHash3-32 of the joined args, printed as 8-digit lowercase
// hex. SEED is decimal or 0x-hex (default 0). Backed by the KAT'd murmur3_32.
static void cmd_murmur(int argc, char** argv) {
    static const char* HEX = "0123456789abcdef";
    uint32_t seed = 0; int start = 1;
    if (start + 1 < argc && strcmp(argv[start], "-s") == 0) {
        const char* s = argv[start + 1];
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            for (s += 2; *s; s++) {
                char c = *s;
                uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0')
                           : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10)
                           : (c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10) : 0;
                seed = seed * 16 + d;
            }
        } else {
            for (; *s >= '0' && *s <= '9'; s++) seed = seed * 10 + (uint32_t)(*s - '0');
        }
        start += 2;
    }
    if (start >= argc) { printf("Usage: murmur [-s SEED] <text ...>\n"); return; }
    uint8_t in[512]; uint32_t o = 0;
    for (int i = start; i < argc; i++) {
        if (i > start && o < sizeof(in)) in[o++] = ' ';                 // rejoin args with spaces
        for (uint32_t k = 0; argv[i][k] && o < sizeof(in); k++) in[o++] = (uint8_t)argv[i][k];
    }
    uint32_t h = murmur3_32(in, o, seed);
    char out[9];
    for (int i = 7; i >= 0; i--) { out[i] = HEX[h & 0xF]; h >>= 4; }
    out[8] = 0; printf("%s\n", out);
}

// base58 [-d] <text ...> — Base58 (Bitcoin) encode the argument text, or -d decode a
// base58 string back to its raw bytes. Backed by the hardened, KAT'd base58 codec.
static void cmd_base58(int argc, char** argv) {
    int dec = 0, start = 1;
    if (start < argc && strcmp(argv[start], "-d") == 0) { dec = 1; start++; }
    if (start >= argc) { printf("Usage: base58 [-d] <text ...>\n"); return; }
    if (dec) {
        uint32_t n = 0; while (argv[start][n]) n++;           // a base58 token has no spaces
        uint8_t out[BASE58_MAX];
        int dl = base58_decode(argv[start], n, out, sizeof(out));
        if (dl < 0) { printf("base58: invalid base58 input\n"); return; }
        for (int i = 0; i < dl; i++) putchar((char)out[i]);
        putchar('\n');
    } else {
        uint8_t in[BASE58_MAX]; uint32_t o = 0;
        for (int i = start; i < argc; i++) {                  // rejoin args with spaces
            if (i > start && o < sizeof(in)) in[o++] = ' ';
            for (uint32_t k = 0; argv[i][k] && o < sizeof(in); k++) in[o++] = (uint8_t)argv[i][k];
        }
        char out[BASE58_MAX * 2];
        if (base58_encode(in, o, out, sizeof(out)) < 0) { printf("base58: input too long\n"); return; }
        printf("%s\n", out);
    }
}

// bech32 <string> — verify and decode a Bech32 (BIP-173) string, printing the human-readable
// prefix and the data symbols as hex when the checksum is valid. The 6-symbol BCH checksum
// detects a handful of altered/transposed characters, which is why crypto addresses use it.
// Backed by the KAT'd bech32_decode(); operates on the 5-bit-symbol layer (no convertbits).
static void cmd_bech32(int argc, char** argv) {
    if (argc < 2) { printf("Usage: bech32 <string>\n"); return; }
    char hrp[BECH32_MAX_HRP + 1];
    uint8_t data[BECH32_MAX_DATA];
    uint32_t dlen;
    if (bech32_decode(argv[1], hrp, sizeof(hrp), data, sizeof(data), &dlen) != 0) {
        printf("bech32: invalid (bad checksum, character, case, or length)\n");
        return;
    }
    static const char hexd[] = "0123456789abcdef";
    printf("valid  hrp=\"%s\"  data=%u symbol%s: ", hrp, dlen, dlen == 1 ? "" : "s");
    for (uint32_t i = 0; i < dlen; i++) {
        putchar(hexd[(data[i] >> 4) & 0xf]);
        putchar(hexd[data[i] & 0xf]);
    }
    putchar('\n');
}

// seq output callback: print each value as a decimal on its own line. The number is
// formatted by hand (unsigned magnitude, so INT64_MIN is safe) to avoid depending on
// printf %lld support.
static void seq_emit_print(long long value, void* ctx) {
    (void)ctx;
    char buf[24]; int i = (int)sizeof(buf); buf[--i] = '\0';
    unsigned long long m = (value < 0) ? (0ULL - (unsigned long long)value) : (unsigned long long)value;
    if (m == 0) buf[--i] = '0';
    while (m) { buf[--i] = (char)('0' + (m % 10)); m /= 10; }
    if (value < 0) buf[--i] = '-';
    printf("%s\n", &buf[i]);
}

// seq [FIRST [STEP]] LAST — print an inclusive integer sequence, one per line.
//   seq L        -> 1..L step 1        seq F L    -> F..L step 1
//   seq F S L    -> F, F+S, ... bounded by L (STEP may be negative; 0 is an error)
// Integer-only (64-bit signed), matching GNU seq for integer arguments.
static void cmd_seq(int argc, char** argv) {
    long long first = 1, step = 1, last = 0;
    int ok = 1;
    if (argc == 2)      ok = (parse_i64(argv[1], &last) == 0);
    else if (argc == 3) ok = (parse_i64(argv[1], &first) == 0) && (parse_i64(argv[2], &last) == 0);
    else if (argc == 4) ok = (parse_i64(argv[1], &first) == 0) && (parse_i64(argv[2], &step) == 0) &&
                             (parse_i64(argv[3], &last) == 0);
    else { printf("Usage: seq [FIRST [STEP]] LAST\n"); return; }
    if (!ok) { printf("seq: arguments must be integers\n"); return; }
    if (seq_run(first, step, last, seq_emit_print, 0) != 0) printf("seq: step must not be zero\n");
}

// comm output callback: `tabs` tab stops, then the raw line bytes, then a newline.
static void comm_emit_print(int tabs, const char* line, int len, void* ctx) {
    (void)ctx;
    for (int i = 0; i < tabs; i++) putchar('\t');
    for (int i = 0; i < len; i++) putchar(line[i]);
    putchar('\n');
}

// comm [-123] <file1> <file2> — read two SORTED files and print, in three tab-offset
// columns, the lines unique to file1, unique to file2, and common to both. -1/-2/-3
// suppress the respective column. Uses the whole in-memory files + comm_run().
static void cmd_comm(int argc, char** argv) {
    int s1 = 1, s2 = 1, s3 = 1, ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        for (int k = 1; argv[ai][k]; k++) {
            if      (argv[ai][k] == '1') s1 = 0;
            else if (argv[ai][k] == '2') s2 = 0;
            else if (argv[ai][k] == '3') s3 = 0;
            else { printf("comm: invalid option -- '%c'\n", argv[ai][k]); return; }
        }
        ai++;
    }
    if (argc - ai != 2) { printf("Usage: comm [-123] <file1> <file2>\n"); return; }

    int fa = vfs_open(argv[ai], 0, 0);
    if (fa < 0) { printf("comm: cannot open '%s'\n", argv[ai]); return; }
    int fb = vfs_open(argv[ai + 1], 0, 0);
    if (fb < 0) { vfs_close(fa); printf("comm: cannot open '%s'\n", argv[ai + 1]); return; }
    uint32_t na = vfs_fsize(fa), nb = vfs_fsize(fb);
    uint8_t* da = vfs_fdata(fa);
    uint8_t* db = vfs_fdata(fb);
    if ((na && !da) || (nb && !db)) { vfs_close(fa); vfs_close(fb); printf("comm: read error\n"); return; }
    comm_run((const char*)da, (int)na, (const char*)db, (int)nb, s1, s2, s3, comm_emit_print, 0);
    vfs_close(fa);
    vfs_close(fb);
}

// paste output callback: emit one byte.
static void paste_emit_putc(char c, void* ctx) { (void)ctx; putchar(c); }

// paste [-s] [-d LIST] <file ...> — merge lines of files. Default: line i of every file
// side by side, TAB-separated, until all files run out. -s: each file's lines joined
// onto one line. -d LIST: cycle LIST's chars as delimiters (\t \n \\ escapes). Reads
// each whole file via vfs (the cmd_comm pattern); up to 16 files. Backed by paste_run().
static void cmd_paste(int argc, char** argv) {
    int serial = 0, ai = 1;
    char del[64]; del[0] = '\t'; int nd = 1;                  // default delimiter: TAB
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        if (strcmp(a, "-s") == 0) { serial = 1; ai++; continue; }
        if (a[1] == 'd') {
            const char* list = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : "");
            nd = 0;
            for (uint32_t k = 0; list[k] && nd < (int)sizeof(del); k++) {
                char c = list[k];
                if (c == '\\' && list[k + 1]) {               // \t \n \\ \r escapes
                    char n = list[++k];
                    c = (n == 't') ? '\t' : (n == 'n') ? '\n' : (n == '\\') ? '\\' : (n == 'r') ? '\r' : n;
                }
                del[nd++] = c;
            }
            if (nd == 0) { del[0] = '\t'; nd = 1; }
            ai++; continue;
        }
        printf("paste: invalid option '%s'\n", a); return;
    }
    int nf = argc - ai;
    if (nf <= 0) { printf("Usage: paste [-s] [-d LIST] <file ...>\n"); return; }
    if (nf > 16) { printf("paste: too many files (max 16)\n"); return; }
    int fds[16]; paste_file_t files[16];
    int opened = 0, ok = 1;
    for (int i = 0; i < nf; i++) {
        int fd = vfs_open(argv[ai + i], 0, 0);
        if (fd < 0) { printf("paste: cannot open '%s'\n", argv[ai + i]); ok = 0; break; }
        fds[opened++] = fd;
        uint32_t sz = vfs_fsize(fd);
        uint8_t* d = vfs_fdata(fd);
        if (sz && !d) { printf("paste: read error\n"); ok = 0; break; }
        files[i].text = (const char*)d; files[i].len = sz;
    }
    if (ok) paste_run(files, nf, del, nd, serial, paste_emit_putc, 0);
    for (int i = 0; i < opened; i++) vfs_close(fds[i]);
}

// tac output callback: write a reversed record's bytes.
static void tac_emit_write(const char* data, uint32_t len, void* ctx) {
    (void)ctx;
    for (uint32_t i = 0; i < len; i++) putchar(data[i]);
}

// tac [-s SEP] <file> — print the file's lines in reverse order (the line-order sibling
// of rev). -s sets the record separator (default newline). Reads a bounded chunk of the
// file (like rev/head); the separator travels with its record. Backed by tac_run().
static void cmd_tac(int argc, char** argv) {
    const char* sep = "\n"; uint32_t seplen = 1; int ai = 1;
    if (ai < argc && strcmp(argv[ai], "-s") == 0 && ai + 1 < argc) {
        sep = argv[ai + 1]; seplen = 0; while (sep[seplen]) seplen++;
        if (seplen == 0) { sep = "\n"; seplen = 1; }
        ai += 2;
    }
    if (ai >= argc) { printf("Usage: tac [-s SEP] <file>\n"); return; }
    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("tac: cannot open '%s'\n", argv[ai]); return; }
    static char buf[4096];
    int bytes = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);
    if (bytes <= 0) return;
    static uint32_t starts[4098];                          // >= any record count for a 4096-byte read
    tac_run(buf, (uint32_t)bytes, sep, seplen, starts, 4098, tac_emit_write, 0);
}

// csv [-d DELIM] <file> — view a CSV file as an aligned table. Backed by the hardened
// RFC 4180 csv_parse(). Two streaming passes over the same in-memory buffer: pass 1 finds
// each column's display width, pass 2 prints padded cells so the columns line up. No large
// buffer is stored (only the per-column widths); long cells are truncated to CSV_CELL_MAX
// and control bytes shown as spaces so an embedded newline can't wreck the layout.
#define CSV_MAXCOLS   16
#define CSV_CELL_MAX  18
struct csv_view { uint8_t colw[CSV_MAXCOLS]; };

// Print up to CSV_CELL_MAX bytes of a cell (control bytes -> space); return the count shown.
static uint32_t csv_print_cell(const char* f, uint32_t len) {
    uint32_t shown = len > CSV_CELL_MAX ? CSV_CELL_MAX : len;
    for (uint32_t i = 0; i < shown; i++) {
        unsigned char b = (unsigned char)f[i];
        putchar(b < 0x20 ? ' ' : (char)b);
    }
    return shown;
}
// Pass 1: widen each column to fit its widest (display-clamped) cell.
static void csv_width_emit(void* ctx, uint32_t row, uint32_t col,
                           const char* f, uint32_t len, uint32_t flags) {
    (void)row; (void)f;
    struct csv_view* v = (struct csv_view*)ctx;
    if (flags & CSV_EMPTY_ROW) return;
    if (col < CSV_MAXCOLS) {
        uint32_t w = len > CSV_CELL_MAX ? CSV_CELL_MAX : len;
        if (w > v->colw[col]) v->colw[col] = (uint8_t)w;
    }
}
// Pass 2: print each record, padding interior cells to the column width (the last cell is
// not padded, to avoid trailing spaces); a blank line prints as a blank line.
static void csv_print_emit(void* ctx, uint32_t row, uint32_t col,
                           const char* f, uint32_t len, uint32_t flags) {
    (void)row;
    struct csv_view* v = (struct csv_view*)ctx;
    if (flags & CSV_EMPTY_ROW) { putchar('\n'); return; }
    if (col > 0) printf(" | ");
    uint32_t shown = csv_print_cell(f, len);
    if (!(flags & CSV_LAST_IN_ROW) && col < CSV_MAXCOLS)
        for (uint32_t k = shown; k < v->colw[col]; k++) putchar(' ');
    if (flags & CSV_LAST_IN_ROW) putchar('\n');
}
static void cmd_csv(int argc, char** argv) {
    char delim = ',';
    int ai = 1;
    if (ai < argc && strcmp(argv[ai], "-d") == 0 && ai + 1 < argc) {
        delim = argv[ai + 1][0];
        if (!delim) delim = ',';
        ai += 2;
    }
    if (ai >= argc) { printf("Usage: csv [-d DELIM] <file>\n"); return; }
    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("csv: cannot open '%s'\n", argv[ai]); return; }
    static char buf[4096];
    int bytes = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);
    if (bytes <= 0) return;
    static char field[256];
    struct csv_view v;
    for (int i = 0; i < CSV_MAXCOLS; i++) v.colw[i] = 0;
    csv_parse(buf, (uint32_t)bytes, delim, field, sizeof(field), csv_width_emit, &v);  // widths
    csv_parse(buf, (uint32_t)bytes, delim, field, sizeof(field), csv_print_emit, &v);  // render
}

// cut -f|-c|-b LIST [-d C] [-s] <file ...> — print selected fields, characters, or bytes of
// each line, byte-for-byte with GNU cut. The pure LIST parse + per-line filter live in cut.c
// (host-diff-verified against GNU cut); this wrapper does the argv parsing and file I/O, then
// splits each file into newline-delimited lines. Every emitted line is newline-terminated,
// even a final unterminated one, matching GNU. Reads a bounded chunk of each file.
static void cmd_cut(int argc, char** argv) {
    cut_spec_t spec;
    spec.mode = CUT_FIELDS; spec.suppress = 0; spec.any = 0; spec.open_from = 0;
    for (uint32_t z = 0; z <= CUT_MAXPOS; z++) spec.sel[z] = 0;
    spec.delim = '\t';                                   // GNU cut's default field delimiter
    int mode_set = 0; const char* list = 0; int ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        if (strcmp(a, "-s") == 0) { spec.suppress = 1; ai++; continue; }
        char opt = a[1];
        if (opt == 'f' || opt == 'c' || opt == 'b') {
            spec.mode = (opt == 'f') ? CUT_FIELDS : (opt == 'c') ? CUT_CHARS : CUT_BYTES;
            mode_set = 1;
            list = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0);
            ai++; continue;
        }
        if (opt == 'd') {
            const char* dl = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : "");
            char d = dl[0];
            if (d == '\\' && dl[1]) {                    // accept \t \n \0 \\ escapes for -d
                char nx = dl[1];
                d = (nx == 't') ? '\t' : (nx == 'n') ? '\n' : (nx == '0') ? '\0' :
                    (nx == '\\') ? '\\' : nx;
            }
            spec.delim = d ? d : '\t';
            ai++; continue;
        }
        printf("cut: invalid option '%s'\n", a); return;
    }
    if (!mode_set || !list) {
        printf("Usage: cut -f LIST [-d C] [-s] <file>   (fields)\n");
        printf("       cut -c LIST <file>                (characters)\n");
        printf("       cut -b LIST <file>                (bytes)\n");
        return;
    }
    spec.odelim = spec.delim;
    if (cut_parse_list(list, &spec) != 0) { printf("cut: invalid list '%s'\n", list); return; }
    if (ai >= argc) { printf("cut: no input file\n"); return; }
    static char buf[4096];
    static char out[4096];
    for (int fi = ai; fi < argc; fi++) {
        int fd = vfs_open(argv[fi], 0, 0);
        if (fd < 0) { printf("cut: cannot open '%s'\n", argv[fi]); continue; }
        int bytes = vfs_read(fd, buf, sizeof(buf));
        vfs_close(fd);
        if (bytes <= 0) continue;
        uint32_t i = 0, n = (uint32_t)bytes;
        while (i < n) {
            uint32_t start = i;
            while (i < n && buf[i] != '\n') i++;
            uint32_t linelen = i - start;
            int had_nl = (i < n);
            uint32_t r = cut_line(&spec, buf + start, linelen, out, sizeof(out));
            if (r != CUT_SKIP) {
                for (uint32_t k = 0; k < r; k++) putchar(out[k]);
                putchar('\n');
            }
            if (had_nl) i++;
        }
    }
}

// uniq output callback: write one collapsed record followed by a newline.
static void uniq_emit_puts(void* ctx, const char* out, uint32_t len) {
    (void)ctx;
    for (uint32_t i = 0; i < len; i++) putchar(out[i]);
    putchar('\n');
}

// uniq [-c|-d|-u|-i|-f N|-s N|-w N] <file> — collapse ADJACENT equal lines, byte-for-byte with
// GNU uniq (the pure logic is in uniq.c, host-diff-verified). Flags may bundle (-cd, -cf N). The
// comparison key skips -f fields then -s chars and is capped at -w chars, optionally case-fold
// (-i); the whole first line of each run is printed, prefixed with a 7-wide count under -c.
static void cmd_uniq(int argc, char** argv) {
    uniq_opts_t o;
    o.count = 0; o.only_dup = 0; o.only_uniq = 0; o.ignore_case = 0;
    o.skip_fields = 0; o.skip_chars = 0; o.check_chars = 0; o.check_chars_set = 0;
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        for (int k = 1; a[k]; k++) {
            char c = a[k];
            if (c == 'c') o.count = 1;
            else if (c == 'd') o.only_dup = 1;
            else if (c == 'u') o.only_uniq = 1;
            else if (c == 'i') o.ignore_case = 1;
            else if (c == 'f' || c == 's' || c == 'w') {
                const char* numstr = a[k + 1] ? &a[k + 1] : (ai + 1 < argc ? argv[++ai] : "");
                uint32_t v = 0;
                for (uint32_t t = 0; numstr[t] >= '0' && numstr[t] <= '9'; t++) v = v * 10 + (uint32_t)(numstr[t] - '0');
                if (c == 'f') o.skip_fields = v;
                else if (c == 's') o.skip_chars = v;
                else { o.check_chars = v; o.check_chars_set = 1; }
                break;                                   // the rest of this arg was the number
            } else { printf("uniq: invalid option -- '%c'\n", c); return; }
        }
        ai++;
    }
    if (ai >= argc) { printf("Usage: uniq [-c|-d|-u|-i|-f N|-s N|-w N] <file>\n"); return; }
    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("uniq: cannot open '%s'\n", argv[ai]); return; }
    static char buf[4096];
    int bytes = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);
    if (bytes <= 0) return;
    uniq_run(&o, buf, (uint32_t)bytes, uniq_emit_puts, 0);
}

// join output callback: write one joined record followed by a newline.
static void join_emit_puts(void* ctx, const char* out, uint32_t len) {
    (void)ctx;
    for (uint32_t i = 0; i < len; i++) putchar(out[i]);
    putchar('\n');
}

// join [-t C] [-1 N|-2 N|-j N] [-a 1|2] <f1> <f2> — relational join of two sorted files on a
// common field, byte-for-byte with GNU join (pure logic in join.c, host-diff-verified). Keys
// are byte-ordered (the C-locale merge join); both files must already be sorted on the field.
static void cmd_join(int argc, char** argv) {
    join_opts_t o;
    o.delim = 0; o.jf1 = 1; o.jf2 = 1; o.a1 = 0; o.a2 = 0;
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        char opt = a[1];
        const char* val = a[2] ? &a[2] : (ai + 1 < argc ? argv[++ai] : "");
        uint32_t n = 0; for (uint32_t k = 0; val[k] >= '0' && val[k] <= '9'; k++) n = n * 10 + (uint32_t)(val[k] - '0');
        if (opt == 't') o.delim = val[0];
        else if (opt == '1') o.jf1 = n ? n : 1;
        else if (opt == '2') o.jf2 = n ? n : 1;
        else if (opt == 'j') { o.jf1 = o.jf2 = (n ? n : 1); }
        else if (opt == 'a') { if (n == 1) o.a1 = 1; else if (n == 2) o.a2 = 1; }
        else { printf("join: invalid option '%s'\n", a); return; }
        ai++;
    }
    if (ai + 1 >= argc) { printf("Usage: join [-t C] [-1/-2/-j N] [-a 1|2] <f1> <f2>\n"); return; }
    int fd1 = vfs_open(argv[ai], 0, 0);
    if (fd1 < 0) { printf("join: cannot open '%s'\n", argv[ai]); return; }
    static char b1[4096];
    int n1 = vfs_read(fd1, b1, sizeof(b1));
    vfs_close(fd1);
    int fd2 = vfs_open(argv[ai + 1], 0, 0);
    if (fd2 < 0) { printf("join: cannot open '%s'\n", argv[ai + 1]); return; }
    static char b2[4096];
    int n2 = vfs_read(fd2, b2, sizeof(b2));
    vfs_close(fd2);
    join_run(b1, n1 > 0 ? (uint32_t)n1 : 0, b2, n2 > 0 ? (uint32_t)n2 : 0, &o, join_emit_puts, 0);
}

// tsort output callback: one node per line.
static void tsort_emit_line(const char* name, uint32_t len, void* ctx) {
    (void)ctx;
    for (uint32_t i = 0; i < len; i++) putchar(name[i]);
    putchar('\n');
}
// tsort <file> — topological sort of a whitespace-separated pair list (A B => A before B).
// Byte-for-byte GNU tsort, backed by the hardened tsort_run(). Reads a bounded file chunk.
static void cmd_tsort(int argc, char** argv) {
    if (argc < 2) { printf("Usage: tsort <file>\n"); return; }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("tsort: cannot open '%s'\n", argv[1]); return; }
    static char buf[4096];
    int bytes = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);
    if (bytes <= 0) return;
    int rc = tsort_run(buf, (uint32_t)bytes, tsort_emit_line, 0);
    if (rc == -1)      printf("tsort: %s: input contains an odd number of tokens\n", argv[1]);
    else if (rc == -2) printf("tsort: %s: input contains a loop\n", argv[1]);
}

// vfsstat — census the VFS node pool (diagnostic for the #66 exhaustion). Splits the
// transient mount-backed nodes into held (open fd) vs idle (leaked off the free list).
static void cmd_vfsstat(int argc, char** argv) {
    (void)argc; (void)argv;
    extern void vfs_pool_report(uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    uint32_t total = 0, hw = 0, freel = 0, held = 0, idle = 0;
    vfs_pool_report(&total, &hw, &freel, &held, &idle);
    uint32_t live = (hw >= freel) ? hw - freel : 0;
    printf("VFS node pool: %u / %u live  (%u free, high-water %u)\n", live, total, freel, hw);
    printf("  mount-backed (/mnt ext2 mirror): %u held (open fd), %u idle\n", held, idle);
    printf("  ramdisk + /proc tree:            %u\n",
           (live >= held + idle) ? live - held - idle : 0);
}

// rev <file> — print each line with its characters in reverse order (the classic
// coreutil). Reads a single bounded chunk of the file (like head/wc; a streaming
// vfs_read loop never sees EOF in-kernel), then reverses each '\n'-delimited line.
static void cmd_rev(int argc, char** argv) {
    if (argc < 2) { printf("Usage: rev <file>\n"); return; }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("rev: cannot open '%s'\n", argv[1]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    int start = 0;
    for (int i = 0; i <= bytes; i++) {
        if (i == bytes || buf[i] == '\n') {         // end of a line (or of the buffer)
            for (int j = i - 1; j >= start; j--) putchar(buf[j]);
            if (i < bytes) putchar('\n');            // keep the newline (not after a final unterminated line)
            start = i + 1;
        }
    }
}

// Expand a tr SET string into a flat byte array, honouring GNU-style ascending
// ranges like "a-z" or "0-9". A '-' that is not a valid range (first/last char,
// or a descending pair) is treated literally. Returns the expanded length.
static int tr_expand(const char* s, unsigned char* out, int max) {
    int n = 0;
    while (*s && n < max) {
        if (s[1] == '-' && s[2] && (unsigned char)s[2] >= (unsigned char)s[0]) {
            int a = (unsigned char)s[0], b = (unsigned char)s[2];
            for (int c = a; c <= b && n < max; c++) out[n++] = (unsigned char)c;
            s += 3;
        } else {
            out[n++] = (unsigned char)*s++;
        }
    }
    return n;
}

// tr [-d] [-s] SET1 [SET2] <file> — translate, delete or squeeze characters read
// from <file>. NyxOS shell builtins are file-oriented (there is no stdin into a
// kernel builtin), so tr takes a file argument like rev/sort/wc rather than the
// classic stdin filter. SET1/SET2 support ascending ranges (a-z, 0-9). In
// translate mode a shorter SET2 has its last character repeated to SET1's length
// (GNU behaviour). -d deletes SET1; -s squeezes runs of the result/SET1 chars.
static void cmd_tr(int argc, char** argv) {
    int del = 0, sqz = 0, ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'd') del = 1;
            else if (*f == 's') sqz = 1;
            else { printf("tr: invalid option -- '%c'\n", *f); return; }
        }
        ai++;
    }
    const char *s1 = 0, *s2 = 0, *fname = 0;
    int rem = argc - ai;
    if (del) {                                   // -d SET1 file  (or -ds SET1 SET2 file)
        if (sqz && rem >= 3) { s1 = argv[ai]; s2 = argv[ai + 1]; fname = argv[ai + 2]; }
        else if (rem >= 2)   { s1 = argv[ai]; fname = argv[ai + 1]; }
    } else if (sqz && rem == 2) {                // -s SET1 file  (squeeze only)
        s1 = argv[ai]; fname = argv[ai + 1];
    } else if (rem >= 3) {                        // [-s] SET1 SET2 file (translate)
        s1 = argv[ai]; s2 = argv[ai + 1]; fname = argv[ai + 2];
    }
    if (!fname) { printf("Usage: tr [-s] SET1 SET2 <file>  |  tr -d[s] SET1 [SET2] <file>\n"); return; }

    unsigned char set1[256], set2[256];
    int n1 = tr_expand(s1, set1, 256);
    int n2 = s2 ? tr_expand(s2, set2, 256) : 0;

    int fd = vfs_open(fname, 0, 0);
    if (fd < 0) { printf("tr: cannot open '%s'\n", fname); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;

    int last = -1;                               // last emitted byte (for squeeze runs)
    for (int i = 0; i < bytes; i++) {
        unsigned char c = (unsigned char)buf[i];
        int idx = -1;
        for (int k = 0; k < n1; k++) if (set1[k] == c) { idx = k; break; }

        if (del && idx >= 0) continue;           // -d: drop matched characters

        unsigned char out = c;
        if (!del && s2 && idx >= 0)              // translate SET1[idx] -> SET2 (last char repeats)
            out = (n2 == 0) ? c : (idx < n2 ? set2[idx] : set2[n2 - 1]);

        if (sqz) {                               // collapse adjacent repeats of squeeze-set chars
            const unsigned char* ss = (s2 ? set2 : set1);
            int sn = (s2 ? n2 : n1);
            int in_set = 0;
            for (int k = 0; k < sn; k++) if (ss[k] == out) { in_set = 1; break; }
            if (in_set && (int)out == last) continue;
        }
        putchar(out);
        last = out;
    }
}

// fold [-w WIDTH] <file> — wrap each input line so no output line exceeds WIDTH
// columns (default 80). A line longer than WIDTH is broken at exactly WIDTH
// characters (classic `fold`: a hard break, no word boundaries); shorter lines and
// the input's own newlines pass through unchanged. File-arg like rev/tr/wc.
static void cmd_fold(int argc, char** argv) {
    int width = 80, ai = 1;
    if (ai < argc && argv[ai][0] == '-' && argv[ai][1] == 'w') {
        if (argv[ai][2] != '\0') { width = atoi(argv[ai] + 2); ai++; }        // -wN
        else if (ai + 1 < argc)  { width = atoi(argv[ai + 1]); ai += 2; }     // -w N
        else { printf("Usage: fold [-w width] <file>\n"); return; }
    }
    if (width < 1) width = 1;
    if (ai >= argc) { printf("Usage: fold [-w width] <file>\n"); return; }

    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("fold: cannot open '%s'\n", argv[ai]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;

    int col = 0;
    for (int i = 0; i < bytes; i++) {
        char c = buf[i];
        if (c == '\n') { putchar('\n'); col = 0; continue; }   // keep real line breaks
        if (col >= width) { putchar('\n'); col = 0; }          // hard-wrap before this char
        putchar(c);
        col++;
    }
}

// nl [-b a|t|n] [-w N] [-s SEP] <file> — number the lines of <file>. By default
// (-b t) only non-empty lines are numbered; -b a numbers every line, -b n none.
// The number is right-justified in N columns (default 6) and followed by SEP
// (default a tab). Arg-based like fold/expand (no stdin into a kernel builtin).
static void cmd_nl(int argc, char** argv) {
    int width = 6, ai = 1;
    char style = 't';                 // t = non-empty (default), a = all, n = none
    const char* sep = "\t";
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1] != '\0') {
        char f = argv[ai][1];
        if (f == 'b') {
            const char* v = (argv[ai][2] != '\0') ? argv[ai] + 2 : (ai + 1 < argc ? argv[++ai] : "");
            if (v[0] == 'a' || v[0] == 't' || v[0] == 'n') style = v[0];
            ai++;
        } else if (f == 'w') {
            if (argv[ai][2] != '\0') { width = atoi(argv[ai] + 2); ai++; }
            else if (ai + 1 < argc) { width = atoi(argv[ai + 1]); ai += 2; }
            else { printf("Usage: nl [-b a|t|n] [-w N] [-s SEP] <file>\n"); return; }
        } else if (f == 's') {
            if (argv[ai][2] != '\0') { sep = argv[ai] + 2; ai++; }
            else if (ai + 1 < argc) { sep = argv[ai + 1]; ai += 2; }
            else { printf("Usage: nl [-b a|t|n] [-w N] [-s SEP] <file>\n"); return; }
        } else break;
    }
    if (width < 1)  width = 1;
    if (width > 12) width = 12;
    if (ai >= argc) { printf("Usage: nl [-b a|t|n] [-w N] [-s SEP] <file>\n"); return; }

    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("nl: cannot open '%s'\n", argv[ai]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;

    int num = 1, i = 0;
    while (i < bytes) {
        int j = i;
        while (j < bytes && buf[j] != '\n') j++;         // j = end of this line
        int len = j - i;
        if (style == 'a' || (style == 't' && len > 0)) { // this line gets a number
            char nb[16]; snprintf(nb, sizeof(nb), "%d", num++);
            int nlen = (int)strlen(nb);
            for (int k = 0; k < width - nlen; k++) putchar(' ');   // right-justify
            for (int k = 0; nb[k]; k++)  putchar(nb[k]);
            for (int k = 0; sep[k]; k++) putchar(sep[k]);
        }
        for (int k = i; k < j; k++) putchar(buf[k]);
        if (j < bytes) putchar('\n');
        i = (j < bytes) ? j + 1 : j;
    }
}

// printf FORMAT [ARG...] — a formatting builtin like the coreutil/shell printf.
// Interprets C backslash escapes in FORMAT (\n \t \r \a \b \f \v \\) and the
// conversions %s %d %i %u %x %X %c %% (with optional flags/width such as %-10s or
// %05d, passed straight through to the kernel formatter). ARGs fill the
// conversions in order; numeric ARGs are read with atoi (0 if not a number). If
// FORMAT contains at least one conversion, the whole FORMAT is REUSED until the
// ARGs are exhausted (POSIX). Length modifiers (l) are ignored so the vararg
// stays int-width to match atoi. Unlike echo, no trailing newline is added.
static void cmd_printf(int argc, char** argv) {
    if (argc < 2) { printf("Usage: printf FORMAT [ARG...]\n"); return; }
    const char* fmt = argv[1];
    int ai = 2;
    int consumed;
    do {
        consumed = 0;
        for (const char* p = fmt; *p; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                    case 'n': putchar('\n'); break;
                    case 't': putchar('\t'); break;
                    case 'r': putchar('\r'); break;
                    case 'a': putchar('\a'); break;
                    case 'b': putchar('\b'); break;
                    case 'f': putchar('\f'); break;
                    case 'v': putchar('\v'); break;
                    case '\\': putchar('\\'); break;
                    default:  putchar('\\'); putchar(*p); break;   // unknown escape: keep literal
                }
            } else if (*p == '%' && p[1]) {
                // Rebuild a single conversion spec: %[-0]*[0-9]*<conv>, dropping any
                // 'l' length modifier (atoi is int-width, so %ld would misread).
                char spec[16]; int si = 0;
                spec[si++] = '%'; p++;
                while ((*p == '-' || *p == '0') && si < 12) spec[si++] = *p++;
                while (*p >= '0' && *p <= '9'   && si < 12) spec[si++] = *p++;
                while (*p == 'l') p++;                              // ignore length modifiers
                char conv = *p;
                spec[si++] = conv; spec[si] = '\0';
                const char* arg = (ai < argc) ? argv[ai] : "";
                switch (conv) {
                    case '%': putchar('%'); break;
                    case 's': printf(spec, arg);                 if (ai < argc) { ai++; consumed = 1; } break;
                    case 'c': putchar(arg[0]);                   if (ai < argc) { ai++; consumed = 1; } break;
                    case 'd': case 'i': printf(spec, atoi(arg)); if (ai < argc) { ai++; consumed = 1; } break;
                    case 'u': case 'x': case 'X':
                        printf(spec, (unsigned int)atoi(arg));   if (ai < argc) { ai++; consumed = 1; } break;
                    case '\0': putchar('%'); p--; break;           // trailing '%': literal, stop the walk
                    default:  printf("%s", spec); break;           // unknown conversion: print it literally
                }
            } else {
                putchar(*p);
            }
        }
    } while (ai < argc && consumed);
}

// expand [-t N] <file> — replace tabs with spaces, aligning to tab stops that are N
// columns apart (default 8): a tab advances to the NEXT multiple of N, so columns
// line up (not a fixed N spaces per tab). File-arg like fold/rev (kernel builtins
// have no stdin). Accepts both `-t N` and `-tN`.
static void cmd_expand(int argc, char** argv) {
    int tabw = 8, ai = 1;
    if (ai < argc && argv[ai][0] == '-' && argv[ai][1] == 't') {
        if (argv[ai][2] != '\0') { tabw = atoi(argv[ai] + 2); ai++; }        // -tN
        else if (ai + 1 < argc)  { tabw = atoi(argv[ai + 1]); ai += 2; }     // -t N
        else { printf("Usage: expand [-t N] <file>\n"); return; }
    }
    if (tabw < 1) tabw = 1;
    if (ai >= argc) { printf("Usage: expand [-t N] <file>\n"); return; }

    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("expand: cannot open '%s'\n", argv[ai]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;

    int col = 0;
    for (int i = 0; i < bytes; i++) {
        char c = buf[i];
        if (c == '\t') {
            int next = (col / tabw + 1) * tabw;       // advance to the next tab stop
            while (col < next) { putchar(' '); col++; }
        } else if (c == '\n') {
            putchar('\n'); col = 0;                   // newline resets the column
        } else {
            putchar(c); col++;
        }
    }
}

// Emit a run of blank columns [from, to) as the FEWEST tabs+spaces that reproduce
// it: step tab stop to tab stop, using a tab whenever it collapses >= 2 columns
// (a single column to the next stop stays a space, since one blank must never
// become a tab), then any leftover columns short of a stop as spaces.
static void unexpand_flush(int from, int to, int tabw) {
    int c = from;
    while ((c / tabw + 1) * tabw <= to) {
        int nt = (c / tabw + 1) * tabw;
        if (nt - c >= 2) { putchar('\t'); c = nt; }
        else             { putchar(' ');  c++;    }
    }
    while (c < to) { putchar(' '); c++; }
}

// unexpand — the inverse of expand: turn runs of spaces back into tabs. Default
// converts only the LEADING blanks of each line (GNU behaviour); -a converts
// blank runs anywhere; -t N sets the tab width and, like GNU, implies -a.
static void cmd_unexpand(int argc, char** argv) {
    int tabw = 8, all = 0, ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1] != '\0') {
        const char* f = argv[ai];
        if (f[1] == 'a' && f[2] == '\0') { all = 1; ai++; }
        else if (f[1] == 't') {
            all = 1;                                                    // GNU: -t enables -a
            if (f[2] != '\0')       { tabw = atoi(f + 2); ai++; }        // -tN
            else if (ai + 1 < argc) { tabw = atoi(argv[ai + 1]); ai += 2; } // -t N
            else { printf("Usage: unexpand [-a] [-t N] <file>\n"); return; }
        } else break;
    }
    if (tabw < 1) tabw = 1;
    if (ai >= argc) { printf("Usage: unexpand [-a] [-t N] <file>\n"); return; }

    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("unexpand: cannot open '%s'\n", argv[ai]); return; }
    char buf[2048];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;

    // col = current column; while `convert` is on, blanks are buffered as a column
    // run [runstart, col) and only flushed (collapsed to tabs) at the next non-blank
    // / newline / EOF. Default mode turns convert off after the first non-blank of a
    // line (leading-only); -a keeps it on for the whole line.
    int col = 0, run = 0, runstart = 0, convert = 1;
    for (int i = 0; i < bytes; i++) {
        char c = buf[i];
        if (convert && (c == ' ' || c == '\t')) {
            if (run == 0) runstart = col;
            col = (c == '\t') ? (col / tabw + 1) * tabw : col + 1;
            run = col - runstart;
        } else {
            if (run > 0) { unexpand_flush(runstart, col, tabw); run = 0; }
            if (c == '\n')      { putchar('\n'); col = 0; convert = 1; }
            else if (c == '\t') { putchar('\t'); col = (col / tabw + 1) * tabw; }
            else if (c == ' ')  { putchar(' ');  col++; }
            else                { putchar(c); col++; if (!all) convert = 0; }
        }
    }
    if (run > 0) unexpand_flush(runstart, col, tabw);
}

// deflate <in> <out> — zlib-compress a file (RFC 1950). The output round-trips through NyxOS's
// own inflate and stock zlib; handy for shrinking in-OS data. See image/deflate.c.
static void cmd_deflate(int argc, char** argv) {
    if (argc < 3) { printf("Usage: deflate <in> <out>\n"); return; }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("deflate: cannot open '%s'\n", argv[1]); return; }
    static uint8_t inbuf[65536];
    int bytes = vfs_read(fd, inbuf, sizeof(inbuf));
    vfs_close(fd);
    if (bytes < 0) { printf("deflate: read error\n"); return; }
    static uint8_t outbuf[65536 + 4096];
    uint32_t olen = 0;
    if (zlib_deflate(inbuf, (uint32_t)bytes, outbuf, sizeof(outbuf), &olen) != 0) {
        printf("deflate: input too large (max %u bytes)\n", (unsigned)sizeof(inbuf)); return;
    }
    if (vfs_write_file(argv[2], outbuf, olen) < 0) { printf("deflate: cannot write '%s'\n", argv[2]); return; }
    uint32_t pct10 = bytes ? (uint32_t)(((uint64_t)olen * 1000) / (uint32_t)bytes) : 0;
    printf("deflate: %d -> %u bytes (%u.%u%% of original) -> %s\n",
           bytes, olen, pct10 / 10, pct10 % 10, argv[2]);
}

// gzip <in> <out> — compress a file into a .gz that stock gunzip / browsers read (image/deflate.c).
static void cmd_gzip(int argc, char** argv) {
    if (argc < 3) { printf("Usage: gzip <in> <out>   (write a .gz that stock gunzip reads)\n"); return; }
    int fd = vfs_open(argv[1], 0, 0);
    if (fd < 0) { printf("gzip: cannot open '%s'\n", argv[1]); return; }
    static uint8_t inbuf[65536];
    int bytes = vfs_read(fd, inbuf, sizeof(inbuf));
    vfs_close(fd);
    if (bytes < 0) { printf("gzip: read error\n"); return; }
    static uint8_t outbuf[65536 + 4096];
    uint32_t olen = 0;
    if (gzip_deflate(inbuf, (uint32_t)bytes, outbuf, sizeof(outbuf), &olen) != 0) {
        printf("gzip: input too large (max %u bytes)\n", (unsigned)sizeof(inbuf)); return;
    }
    if (vfs_write_file(argv[2], outbuf, olen) < 0) { printf("gzip: cannot write '%s'\n", argv[2]); return; }
    uint32_t pct10 = bytes ? (uint32_t)(((uint64_t)olen * 1000) / (uint32_t)bytes) : 0;
    printf("gzip: %d -> %u bytes (%u.%u%% of original) -> %s\n", bytes, olen, pct10 / 10, pct10 % 10, argv[2]);
}

static uint32_t ipc_bswap32(uint32_t x) {
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) | ((x >> 8) & 0xFF00u) | ((x >> 24) & 0xFFu);
}

// ipcalc <ip>/<prefix> | <ip> <netmask> | <ip> <prefix> — IPv4 subnet calculator (net/ipcalc.c).
// ipv4_parse yields network order (octet1 low); flip to host order for the math + display.
static void cmd_ipcalc(int argc, char** argv) {
    if (argc < 2) { printf("Usage: ipcalc <ip>/<prefix> | <ip> <netmask> | <ip> <prefix>\n"); return; }
    char ipstr[32]; int p = -1, slash = -1;
    for (int i = 0; argv[1][i]; i++) if (argv[1][i] == '/') { slash = i; break; }
    if (slash >= 0) {
        if (slash >= (int)sizeof(ipstr)) { printf("ipcalc: address too long\n"); return; }
        int k = 0; for (; k < slash; k++) ipstr[k] = argv[1][k]; ipstr[k] = '\0';
        p = atoi(argv[1] + slash + 1);
    } else {
        int k = 0; for (; argv[1][k] && k < (int)sizeof(ipstr) - 1; k++) ipstr[k] = argv[1][k]; ipstr[k] = '\0';
        if (argc < 3) { printf("ipcalc: need a prefix or netmask (ip/prefix, ip netmask, or ip prefix)\n"); return; }
        int hasdot = 0; for (int i = 0; argv[2][i]; i++) if (argv[2][i] == '.') { hasdot = 1; break; }
        if (hasdot) {
            uint32_t nm_net;
            if (ipv4_parse(argv[2], &nm_net) != 0) { printf("ipcalc: invalid netmask: %s\n", argv[2]); return; }
            uint32_t nm = ipc_bswap32(nm_net);
            for (int q = 0; q <= 32; q++) { uint32_t em = (q == 0) ? 0u : (0xFFFFFFFFu << (32 - q)); if (em == nm) { p = q; break; } }
            if (p < 0) { printf("ipcalc: not a contiguous netmask: %s\n", argv[2]); return; }
        } else p = atoi(argv[2]);
    }
    if (p < 0 || p > 32) { printf("ipcalc: prefix must be 0..32\n"); return; }
    uint32_t ip_net;
    if (ipv4_parse(ipstr, &ip_net) != 0) { printf("ipcalc: invalid address: %s\n", ipstr); return; }
    uint32_t hip = ipc_bswap32(ip_net);
    ipcalc_result_t r; ipcalc_compute(hip, p, &r);
    #define IPC_Q(v) (unsigned)(((v) >> 24) & 0xFF), (unsigned)(((v) >> 16) & 0xFF), (unsigned)(((v) >> 8) & 0xFF), (unsigned)((v) & 0xFF)
    printf("Address:   %u.%u.%u.%u\n", IPC_Q(hip));
    printf("Netmask:   %u.%u.%u.%u = %d\n", IPC_Q(r.netmask), r.prefix);
    printf("Wildcard:  %u.%u.%u.%u\n", IPC_Q(r.hostmask));
    printf("Network:   %u.%u.%u.%u/%d\n", IPC_Q(r.network), r.prefix);
    printf("Broadcast: %u.%u.%u.%u\n", IPC_Q(r.broadcast));
    printf("HostMin:   %u.%u.%u.%u\n", IPC_Q(r.hostmin));
    printf("HostMax:   %u.%u.%u.%u\n", IPC_Q(r.hostmax));
    printf("Hosts:     %u\n", r.hosts);
    #undef IPC_Q
}

// calc <expression> — evaluate a 64-bit integer expression (core/calc.c). Joins the args so both
// `calc "2+3*4"` and `calc 2 + 3 * 4` work; formats the int64 result by hand (no printf %lld dep).
static void cmd_calc(int argc, char** argv) {
    if (argc < 2) { printf("Usage: calc <expression>   (e.g. calc \"2 + 3 * (4 - 1)\")\n"); return; }
    char buf[256]; int n = 0;
    for (int i = 1; i < argc && n < (int)sizeof(buf) - 1; i++) {
        if (i > 1 && n < (int)sizeof(buf) - 1) buf[n++] = ' ';
        for (int k = 0; argv[i][k] && n < (int)sizeof(buf) - 1; k++) buf[n++] = argv[i][k];
    }
    buf[n] = '\0';
    int64_t r;
    int e = calc_eval(buf, &r);
    if (e != 0) {
        const char* msg;
        switch (-e) {
            case 1:  msg = "syntax error"; break;
            case 2:  msg = "unbalanced parentheses"; break;
            case 3:  msg = "division by zero"; break;
            case 4:  msg = "trailing characters"; break;
            case 5:  msg = "shift amount out of range (0..63)"; break;
            case 6:  msg = "empty expression"; break;
            default: msg = "error"; break;
        }
        printf("calc: %s\n", msg);
        return;
    }
    char num[24], tmp[24]; int ni = 0, ti = 0, neg = (r < 0);
    uint64_t u = neg ? (uint64_t)(-(r + 1)) + 1u : (uint64_t)r;
    if (u == 0) tmp[ti++] = '0';
    while (u) { tmp[ti++] = (char)('0' + u % 10u); u /= 10u; }
    if (neg) num[ni++] = '-';
    while (ti) num[ni++] = tmp[--ti];
    num[ni] = '\0';
    printf("%s\n", num);
}

// json <file>            — validate a JSON document (RFC 8259) read from the VFS (core/json.c).
// json get <file> <path> — extract the value at a jq-lite path, e.g. json get cfg.json .a[0].b
static char json_buf[65536];   // off the 4 KB kernel stack: JSON documents can be large
static void cmd_json(int argc, char** argv) {
    int getmode = (argc >= 2 && strcmp(argv[1], "get") == 0);
    if (getmode ? (argc < 4) : (argc < 2)) {
        printf("Usage: json <file>            (validate, RFC 8259)\n");
        printf("       json get <file> <path> (extract value, e.g. .users[0].name)\n");
        return;
    }
    const char* file = getmode ? argv[2] : argv[1];
    int fd = vfs_open(file, 0, 0);
    if (fd < 0) { printf("json: cannot open '%s'\n", file); return; }
    int bytes = vfs_read(fd, json_buf, sizeof(json_buf) - 1);
    vfs_close(fd);
    if (bytes < 0) { printf("json: read error\n"); return; }
    json_buf[bytes] = '\0';
    int errpos = 0;
    if (json_validate(json_buf, &errpos) != 0) {
        printf("%s: invalid JSON at byte %d\n", file, errpos);
        return;
    }
    if (!getmode) { printf("%s: valid JSON\n", file); return; }
    int st, ln;
    int r = json_query(json_buf, argv[3], &st, &ln);
    if (r == JQ_OK) {
        char saved = json_buf[st + ln];
        json_buf[st + ln] = '\0';
        printf("%s\n", &json_buf[st]);       // value's raw byte span (scalar or subtree)
        json_buf[st + ln] = saved;
    } else if (r == JQ_ENOTFOUND) printf("json: path not found\n");
    else if (r == JQ_ETYPE)       printf("json: type mismatch at a path step\n");
    else if (r == JQ_EPATH)       printf("json: malformed path '%s'\n", argv[3]);
    else                          printf("json: extract error\n");
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

// ---------------------------------------------------------------------------
// du — disk usage (v6.4.246). Walks a directory subtree ITERATIVELY (an
// explicit, bounded off-stack frontier — the 4 KB kernel task stack forbids
// deep recursion) and sums the sizes of every file underneath. The traversal
// takes an injected "enumerate one directory" callback so it can be KAT'd
// against a synthetic tree with no VFS (same pattern as the xbm dep resolver).
// ---------------------------------------------------------------------------
#define DU_MAXPEND 128        // max directories pending in the DFS frontier
#define DU_PATHMAX 256        // longest path we track
static char du_pend[DU_MAXPEND][DU_PATHMAX];   // off-stack frontier (~32 KB BSS)

// Format `bytes` as a short human-readable string: "0B", "1023B", "1.0K",
// "1.5M", "2.0G" — one decimal, integer-only (the kernel is -mno-sse).
static void du_human(uint64_t bytes, char* out, int cap) {
    if (bytes < 1024) { snprintf(out, cap, "%uB", (uint32_t)bytes); return; }
    const char* units = "KMGT";
    uint64_t div = 1024;
    int e = 0;
    while (bytes / div >= 1024 && e < 3) { div *= 1024; e++; }
    uint64_t whole = bytes / div;
    uint64_t frac  = (bytes % div) * 10 / div;
    snprintf(out, cap, "%u.%u%c", (uint32_t)whole, (uint32_t)frac, units[e]);
}

// Join dir + "/" + name into out[], collapsing the "/name" case for the root.
static void du_join(const char* dir, const char* name, char* out, int cap) {
    if (dir[0] == '/' && dir[1] == '\0') snprintf(out, cap, "/%s", name);
    else                                 snprintf(out, cap, "%s/%s", dir, name);
}

// enum callback: for directory `path`, invoke child(cctx, name, is_dir, size)
// once per immediate child. Injected so du_walk is testable without a VFS.
typedef void (*du_child_fn)(void* cctx, const char* name, int is_dir, uint64_t size);
typedef void (*du_enum_fn)(void* ectx, const char* path, du_child_fn child, void* cctx);

typedef struct {
    int         npend;        // frontier depth
    int         truncated;    // set if the frontier overflowed
    uint64_t    total;
    int         files;
    const char* parent;       // directory currently being enumerated (for path join)
} du_state_t;

// child callback used by du_walk: a file adds to the total, a directory is
// pushed onto the frontier to visit later (dropped, with a flag, if it is full).
static void du_on_child(void* cctx, const char* name, int is_dir, uint64_t size) {
    du_state_t* st = (du_state_t*)cctx;
    if (!is_dir) { st->total += size; st->files++; return; }
    if (st->npend >= DU_MAXPEND) { st->truncated = 1; return; }
    du_join(st->parent, name, du_pend[st->npend], DU_PATHMAX);
    st->npend++;
}

// Iterative DFS from `root`. Returns total bytes; *files/*dirs/*truncated out.
static uint64_t du_walk(const char* root, du_enum_fn enum_dir, void* ectx,
                        int* out_files, int* out_dirs, int* out_trunc) {
    du_state_t st;
    st.npend = 0; st.truncated = 0; st.total = 0; st.files = 0;
    int dirs = 0;
    char cur[DU_PATHMAX];
    strncpy(du_pend[0], root, DU_PATHMAX - 1); du_pend[0][DU_PATHMAX - 1] = '\0';
    st.npend = 1;
    while (st.npend > 0) {
        st.npend--;                                        // pop
        strncpy(cur, du_pend[st.npend], DU_PATHMAX - 1); cur[DU_PATHMAX - 1] = '\0';
        dirs++;
        st.parent = cur;
        enum_dir(ectx, cur, du_on_child, &st);             // pushes children / adds file sizes
    }
    if (out_files) *out_files = st.files;
    if (out_dirs)  *out_dirs  = dirs;
    if (out_trunc) *out_trunc = st.truncated;
    return st.total;
}

// Real VFS enumeration for one directory. Snapshots the entries first (name +
// type) so a vfs_stat for a file's size never lands between two vfs_readdir
// calls on the same fd. du_walk never nests these, so the static buffer is safe.
static void du_vfs_enum(void* ectx, const char* path, du_child_fn child, void* cctx) {
    (void)ectx;
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) return;
    static dirent_t du_ents[256];
    int n = 0;
    dirent_t* de = vfs_readdir(fd);
    while (de && n < 256) { du_ents[n++] = *de; de = vfs_readdir(fd); }
    vfs_close(fd);
    for (int i = 0; i < n; i++) {
        const char* nm = du_ents[i].name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;  // skip . / ..
        if (du_ents[i].type == 1) {
            child(cctx, nm, 1, 0);
        } else {
            char childpath[DU_PATHMAX];
            du_join(path, nm, childpath, DU_PATHMAX);
            uint32_t sz = 0; int isd = 0;
            vfs_stat(childpath, &sz, &isd);
            child(cctx, nm, 0, (uint64_t)sz);
        }
    }
}

// Synthetic tree for the KAT (no VFS):
//   /      -> dir a, file f0(500)
//   /a     -> file x(100), file y(200), dir c
//   /a/c   -> file z(50)
// Expected: total 850, files 4, dirs 3.
static void du_test_enum(void* ectx, const char* path, du_child_fn child, void* cctx) {
    (void)ectx;
    if (strcmp(path, "/") == 0) {
        child(cctx, "a", 1, 0);
        child(cctx, "f0", 0, 500);
    } else if (strcmp(path, "/a") == 0) {
        child(cctx, "x", 0, 100);
        child(cctx, "y", 0, 200);
        child(cctx, "c", 1, 0);
    } else if (strcmp(path, "/a/c") == 0) {
        child(cctx, "z", 0, 50);
    }
}

int du_selftest(void) {
    int files = 0, dirs = 0, trunc = 0;
    uint64_t t = du_walk("/", du_test_enum, 0, &files, &dirs, &trunc);
    if (t != 850 || files != 4 || dirs != 3 || trunc != 0) return 1;

    char b[24];
    du_human(0, b, sizeof(b));            if (strcmp(b, "0B") != 0)    return 2;
    du_human(1023, b, sizeof(b));         if (strcmp(b, "1023B") != 0) return 3;
    du_human(1024, b, sizeof(b));         if (strcmp(b, "1.0K") != 0)  return 4;
    du_human(1536, b, sizeof(b));         if (strcmp(b, "1.5K") != 0)  return 5;
    du_human(1048576, b, sizeof(b));      if (strcmp(b, "1.0M") != 0)  return 6;
    du_human(1572864, b, sizeof(b));      if (strcmp(b, "1.5M") != 0)  return 7;
    du_human(1073741824ULL, b, sizeof(b));if (strcmp(b, "1.0G") != 0)  return 8;
    du_human(5242880, b, sizeof(b));      if (strcmp(b, "5.0M") != 0)  return 9;
    return 0;
}

static void cmd_du(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : vfs_getcwd();
    uint32_t sz = 0; int is_dir = 0;
    if (vfs_stat(path, &sz, &is_dir) != 0) { printf("du: cannot access '%s'\n", path); return; }
    char hb[24];
    if (!is_dir) {
        du_human((uint64_t)sz, hb, sizeof(hb));
        printf("%s\t%s (%u bytes)\n", hb, path, sz);
        return;
    }
    int files = 0, dirs = 0, trunc = 0;
    uint64_t total = du_walk(path, du_vfs_enum, 0, &files, &dirs, &trunc);
    du_human(total, hb, sizeof(hb));
    printf("%s\t%s\n", hb, path);
    printf("  %u bytes across %d file(s) in %d director(y/ies)\n", (uint32_t)total, files, dirs);
    if (trunc) printf("  (note: subtree exceeds %d pending dirs - total is a floor)\n", DU_MAXPEND);
}

// ---------------------------------------------------------------------------
// disks / lsblk (v6.4.248) — first nyxinstall rung. Enumerates ATA disks
// (IDENTIFY -> model + size) and parses each disk's MBR partition table. The
// MBR parse is pure and KAT'd; the size formatter is du_human (above). Read-only
// — the write side (partition + mkfs + install) comes in later increments.
// ---------------------------------------------------------------------------
typedef struct { uint8_t status; uint8_t type; uint32_t lba_start; uint32_t sectors; } mbr_part_t;

// Parse the 4 primary entries of an MBR boot sector (offset 0x1BE, 16 B each).
// Returns the count of non-empty (type != 0) partitions written to out[4], or
// -1 if the 0x55AA boot signature is absent. Pure — no I/O.
static int mbr_parse(const uint8_t* sec, mbr_part_t out[4]) {
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -1;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t* e = sec + 0x1BE + i * 16;
        if (e[4] == 0) continue;                         // empty entry
        out[n].status    = e[0];
        out[n].type      = e[4];
        out[n].lba_start = (uint32_t)e[8]  | ((uint32_t)e[9]  << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        out[n].sectors   = (uint32_t)e[12] | ((uint32_t)e[13] << 8) | ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        n++;
    }
    return n;
}

// Build a 512-byte MBR boot sector from up to 4 partition specs (v6.4.249).
// Zeroes the sector, writes each entry at 0x1BE + i*16 (status, LBA-sentinel
// CHS, type, LBA-start LE, sector-count LE), stamps the 0x55AA signature. Pure —
// KAT'd via a build->parse round-trip. The write side of the partition table.
static void mbr_build(const mbr_part_t* parts, int nparts, uint8_t* sec) {
    for (int i = 0; i < 512; i++) sec[i] = 0;
    if (nparts > 4) nparts = 4;
    for (int i = 0; i < nparts; i++) {
        uint8_t* e = sec + 0x1BE + i * 16;
        e[0] = parts[i].status;
        e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;           // CHS start: "too large, use LBA" sentinel
        e[4] = parts[i].type;
        e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;           // CHS end: same
        e[8]  = (uint8_t)(parts[i].lba_start);
        e[9]  = (uint8_t)(parts[i].lba_start >> 8);
        e[10] = (uint8_t)(parts[i].lba_start >> 16);
        e[11] = (uint8_t)(parts[i].lba_start >> 24);
        e[12] = (uint8_t)(parts[i].sectors);
        e[13] = (uint8_t)(parts[i].sectors >> 8);
        e[14] = (uint8_t)(parts[i].sectors >> 16);
        e[15] = (uint8_t)(parts[i].sectors >> 24);
    }
    sec[510] = 0x55; sec[511] = 0xAA;
}

static const char* mbr_type_name(uint8_t t) {
    switch (t) {
        case 0x83: return "Linux";
        case 0x82: return "Linux swap";
        case 0x07: return "NTFS/exFAT";
        case 0x0B: case 0x0C: return "FAT32";
        case 0x06: case 0x0E: return "FAT16";
        case 0xEF: return "EFI System";
        case 0xEE: return "GPT protective";
        default:   return "unknown";
    }
}

// Total 512-byte sectors from an ATA IDENTIFY buffer: prefer the 48-bit count
// (words 100-103), fall back to the 28-bit count (words 60-61).
static uint64_t ata_id_sectors(const uint16_t* id) {
    uint64_t s48 = (uint64_t)id[100] | ((uint64_t)id[101] << 16) |
                   ((uint64_t)id[102] << 32) | ((uint64_t)id[103] << 48);
    if (s48) return s48;
    return (uint64_t)id[60] | ((uint64_t)id[61] << 16);
}

// Model string (IDENTIFY words 27-46, byte-swapped within each word), trimmed.
static void ata_id_model(const uint16_t* id, char* out, int cap) {
    int o = 0;
    for (int w = 27; w <= 46 && o < cap - 2; w++) {
        out[o++] = (char)(id[w] >> 8);
        out[o++] = (char)(id[w] & 0xFF);
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\0')) o--;
    out[o] = '\0';
}

int mbr_selftest(void) {
    static uint8_t sec[512];
    mbr_part_t p[4];
    for (int i = 0; i < 512; i++) sec[i] = 0;
    if (mbr_parse(sec, p) != -1) return 1;               // no 0x55AA -> -1
    sec[510] = 0x55; sec[511] = 0xAA;
    if (mbr_parse(sec, p) != 0) return 2;                // sig present, empty table -> 0
    uint8_t* e = sec + 0x1BE;
    e[0] = 0x80; e[4] = 0x83; e[8] = 0x00; e[9] = 0x08; e[12] = 0x00; e[13] = 0x70;   // Linux, LBA 2048, 28672 sec
    uint8_t* e2 = e + 16;
    e2[4] = 0xEF; e2[8] = 0x00; e2[9] = 0x80; e2[12] = 0x00; e2[13] = 0x10;           // EFI, LBA 32768, 4096 sec
    int n = mbr_parse(sec, p);
    if (n != 2) return 3;
    if (p[0].type != 0x83 || p[0].status != 0x80 || p[0].lba_start != 2048 || p[0].sectors != 28672) return 4;
    if (p[1].type != 0xEF || p[1].lba_start != 32768 || p[1].sectors != 4096) return 5;
    return 0;
}

int mbr_build_selftest(void) {
    static uint8_t sec[512];
    mbr_part_t in[2] = {
        { .status = 0x80, .type = 0x83, .lba_start = 2048,  .sectors = 20480 },
        { .status = 0x00, .type = 0xEF, .lba_start = 24576, .sectors = 6144  },
    };
    mbr_build(in, 2, sec);
    if (sec[510] != 0x55 || sec[511] != 0xAA) return 1;  // signature stamped
    mbr_part_t out[4];
    int n = mbr_parse(sec, out);                         // round-trip through the parser
    if (n != 2) return 2;
    for (int i = 0; i < 2; i++)
        if (out[i].status != in[i].status || out[i].type != in[i].type ||
            out[i].lba_start != in[i].lba_start || out[i].sectors != in[i].sectors) return 3 + i;
    // a fresh build over a dirty sector must clear stale entries
    mbr_part_t one = { .status = 0x80, .type = 0x83, .lba_start = 2048, .sectors = 100 };
    mbr_build(&one, 1, sec);
    if (mbr_parse(sec, out) != 1) return 5;
    return 0;
}

// Parse + print the MBR partition table in a just-read 512-byte sector 0. Shared
// by the ATA and NVMe branches of `disks`.
static void print_mbr_parts(const uint8_t* sec) {
    mbr_part_t p[4];
    int np = mbr_parse(sec, p);
    if (np < 0) { printf("  no MBR table (raw / GPT / unpartitioned)\n"); return; }
    if (np == 0) { printf("  MBR present, empty partition table\n"); return; }
    for (int i = 0; i < np; i++) {
        char pb[24];
        du_human((uint64_t)p[i].sectors * 512ULL, pb, sizeof(pb));
        printf("  p%d  0x%02x %-14s start %-9u %s%s\n", i + 1, p[i].type, mbr_type_name(p[i].type),
               p[i].lba_start, pb, (p[i].status & 0x80) ? "  [boot]" : "");
    }
}

static void cmd_disks(int argc, char** argv) {
    (void)argc; (void)argv;
    ata_init();
    static uint16_t id[256];
    static uint8_t sec[512];
    int found = 0;
    for (int d = 0; d < 2; d++) {                        // primary master / slave
        if (ata_identify((uint8_t)d, id) != 0) continue;
        found++;
        char model[42];
        ata_id_model(id, model, sizeof(model));
        uint64_t sectors = ata_id_sectors(id);
        char hb[24];
        du_human(sectors * 512ULL, hb, sizeof(hb));
        printf("Disk %d: %s  %s (%u sectors)\n", d, model[0] ? model : "(unknown)", hb, (uint32_t)sectors);
        if (ata_read_sectors((uint8_t)d, 0, 1, sec) < 0) { printf("  (cannot read sector 0)\n"); continue; }
        print_mbr_parts(sec);
        gpt_list((uint8_t)d);                                 // if GPT, list its partitions too
    }
    // NVMe namespace — a PCIe device, invisible to the ATA/IDE probe above. Bring
    // the controller up (idempotent), then list model + capacity + its MBR table.
    if (nvme_init() == 0) {
        nvme_identify();
        nvme_create_io_queues();
        if (nvme_io_ready()) {
            found++;
            uint64_t blks = nvme_capacity_blocks();
            uint32_t bs   = nvme_block_size();
            char hb[24];
            du_human(blks * (uint64_t)bs, hb, sizeof(hb));
            printf("Disk n0: %s  %s (%u blocks x %u B) [NVMe]\n",
                   nvme_model_str(), hb, (uint32_t)blks, bs);
            static uint8_t nsec[4096] __attribute__((aligned(4096)));   // page-aligned NVMe DMA target
            if (nvme_read_blocks(0, 1, nsec) == 0) print_mbr_parts(nsec);
            else printf("  (cannot read LBA 0)\n");
            gpt_list(BLK_NVME0);                              // if GPT, list its partitions too
        }
    }
    if (!found) printf("disks: no disks detected\n");
}

// lspci — enumerate PCI devices. On a modern UEFI machine the disk is an NVMe
// controller on PCIe (invisible to the ATA/IDE `disks` probe), so this is the
// recon step that finds it (and reads its BAR0) before a real storage driver
// exists. Storage controllers get their class triple + BAR0 spelled out.
static void cmd_lspci(int argc, char** argv) {
    (void)argc; (void)argv;
    static pci_dev_t devs[96];
    int n = pci_enumerate(devs, 96);
    if (n <= 0) { printf("lspci: no PCI devices found\n"); return; }
    printf("PCI devices (%d):\n", n);
    for (int i = 0; i < n; i++) {
        pci_dev_t* d = &devs[i];
        printf("  %02x:%02x.%u  %04x:%04x  %s", d->bus, d->slot, d->func,
               d->vendor, d->device, pci_class_name(d->class_code, d->subclass, d->prog_if));
        if (d->class_code == 0x01)                          // storage: dump the recon detail
            printf("  [class %02x:%02x:%02x rev %02x BAR0=%08x]",
                   d->class_code, d->subclass, d->prog_if, d->revision, d->bar0);
        printf("\n");
    }
}

// nvme — probe the NVMe controller, map its BAR, reset + enable it, and stand up
// the admin queue (rung 1 of the NVMe driver; the real-hardware disk is NVMe).
// All the reporting lives in nvme_init(); this is just the shell entry point.
static void cmd_nvme(int argc, char** argv) {
    if (nvme_init() != 0) return;
    nvme_identify();
    nvme_create_io_queues();
    // `nvme read <lba>` — READ-ONLY hex dump of one block. There is deliberately
    // NO write path in the shell command: NyxOS must never write a real disk
    // except through an explicit, confirmed nyxinstall.
    if (argc >= 2 && strcmp(argv[1], "read") == 0)
        nvme_dump_lba((uint64_t)(uint32_t)atoi(argc >= 3 ? argv[2] : "0"));
    // `nvme writetest confirm` — DIAGNOSTIC: write a scratch region of the NVMe with a
    // stamped pattern (one single-block write each, exactly like ext2), flush, then read
    // it all back and report how many blocks survived. Reveals whether bulk single-block
    // writes actually land on a real controller. WRITES — only on a disposable disk.
    if (argc >= 2 && strcmp(argv[1], "writetest") == 0) {
        if (argc < 3 || strcmp(argv[2], "confirm") != 0) {
            printf("Usage: nvme writetest confirm\n");
            printf("  Writes+reads scratch LBA 800000..802047 on the NVMe (disposable disk only).\n");
            return;
        }
        static uint8_t wb[512], rb[512];
        uint32_t base = 800000u, count = 2048u, ok = 0, bad = 0, shown = 0;
        printf("nvme writetest: writing %u single blocks @ LBA %u..\n", count, base);
        for (uint32_t i = 0; i < count; i++) {
            for (int j = 0; j < 512; j++) wb[j] = (uint8_t)(i * 3 + j);
            wb[0] = 0x5A; wb[1] = 0xA5; wb[2] = (uint8_t)(i & 0xFF); wb[3] = (uint8_t)((i >> 8) & 0xFF);
            if (blk_write1(BLK_NVME0, base + i, wb) != 0) { printf("  WRITE returned error at block %u\n", i); break; }
        }
        blk_flush(BLK_NVME0);
        printf("nvme writetest: reading back..\n");
        for (uint32_t i = 0; i < count; i++) {
            __builtin_memset(rb, 0, 512);
            blk_read1(BLK_NVME0, base + i, rb);
            int good = rb[0] == 0x5A && rb[1] == 0xA5 && rb[2] == (uint8_t)(i & 0xFF) && rb[3] == (uint8_t)((i >> 8) & 0xFF);
            if (good) ok++;
            else {
                bad++;
                if (shown < 4) {
                    printf("  MISMATCH block %u (LBA %u): got %02x %02x %02x %02x, want 5a a5 %02x %02x\n",
                           i, base + i, rb[0], rb[1], rb[2], rb[3], (uint8_t)(i & 0xFF), (uint8_t)((i >> 8) & 0xFF));
                    shown++;
                }
            }
        }
        printf("nvme writetest: %u/%u blocks OK, %u BAD\n", ok, count, bad);
        return;
    }
    // `nvme persisttest write|verify` — CROSS-POWER-CYCLE diagnostic. `writetest`
    // reads back in the SAME boot (sees the controller's write cache); the real
    // install failed only AFTER a reboot. This proves whether single-block writes
    // (exactly ext2's path) survive a full power-off. Two phases:
    //   write : stamp+flush N blocks, same-boot read-back (write path + volume),
    //           then the user POWER-CYCLES the machine.
    //   verify: after the power cycle, read them back -> real NAND persistence.
    // WRITES a scratch region (LBA 800000..) — disposable disk only.
    if (argc >= 2 && strcmp(argv[1], "persisttest") == 0) {
        const uint32_t base = 800000u, count = 8192u;      // 4 MiB, ~kernel-sized
        int do_write  = (argc >= 3 && strcmp(argv[2], "write")  == 0);
        int do_verify = (argc >= 3 && strcmp(argv[2], "verify") == 0);
        if (!do_write && !do_verify) {
            printf("Usage: nvme persisttest write | verify\n");
            printf("  write : stamp+flush %u blocks @ LBA %u, then FULLY POWER OFF the machine.\n", count, base);
            printf("  verify: after the power cycle + reboot, read them back (real NAND persistence).\n");
            return;
        }
        static uint8_t wb[512], rb[512];
        if (do_write) {
            printf("nvme persisttest: controller VWC present: %s; write cache: %s; writes use FUA (force-to-media)\n",
                   nvme_vwc() ? "YES" : "no",
                   nvme_wce_disabled() ? "DISABLED" : "ENABLED");
            printf("nvme persisttest: writing %u stamped blocks @ LBA %u..\n", count, base);
            for (uint32_t i = 0; i < count; i++) {
                for (int j = 0; j < 512; j++) wb[j] = (uint8_t)(i * 7 + j);
                wb[0] = 0x4E; wb[1] = 0x59;                 // "NY" magic
                wb[2] = (uint8_t)(i & 0xFF); wb[3] = (uint8_t)((i >> 8) & 0xFF);
                if (blk_write1(BLK_NVME0, base + i, wb) != 0) { printf("  WRITE error at block %u\n", i); return; }
            }
            int fl = blk_flush(BLK_NVME0);
            printf("nvme persisttest: flush returned %d (0=ok)\n", fl);
            uint32_t ok = 0;                                // same-boot read-back = write-path + volume check
            for (uint32_t i = 0; i < count; i++) {
                __builtin_memset(rb, 0, 512);
                blk_read1(BLK_NVME0, base + i, rb);
                int good = 1;
                for (int j = 0; j < 512; j++) {
                    uint8_t want = j==0 ? 0x4E : j==1 ? 0x59 : j==2 ? (uint8_t)(i & 0xFF)
                                 : j==3 ? (uint8_t)((i >> 8) & 0xFF) : (uint8_t)(i * 7 + j);
                    if (rb[j] != want) { good = 0; break; }
                }
                if (good) ok++;
            }
            printf("nvme persisttest: same-boot read-back %u/%u OK\n", ok, count);
            printf("\n  >>> NOW FULLY POWER OFF the laptop (hold the power button until it's dark),\n");
            printf("      then boot the NyxOS USB again and run:  nvme persisttest verify\n");
            return;
        }
        printf("nvme persisttest: reading back %u blocks @ LBA %u (post power-cycle)..\n", count, base);
        uint32_t ok = 0, bad = 0, shown = 0;
        for (uint32_t i = 0; i < count; i++) {
            __builtin_memset(rb, 0, 512);
            blk_read1(BLK_NVME0, base + i, rb);
            int good = 1;
            for (int j = 0; j < 512; j++) {
                uint8_t want = j==0 ? 0x4E : j==1 ? 0x59 : j==2 ? (uint8_t)(i & 0xFF)
                             : j==3 ? (uint8_t)((i >> 8) & 0xFF) : (uint8_t)(i * 7 + j);
                if (rb[j] != want) { good = 0; break; }
            }
            if (good) ok++;
            else {
                bad++;
                if (shown < 6) {
                    printf("  LOST block %u (LBA %u): got %02x %02x %02x %02x (want 4e 59 %02x %02x)\n",
                           i, base + i, rb[0], rb[1], rb[2], rb[3], (uint8_t)(i & 0xFF), (uint8_t)((i >> 8) & 0xFF));
                    shown++;
                }
            }
        }
        printf("nvme persisttest: %u/%u survived the power cycle, %u LOST\n", ok, count, bad);
        if (bad == 0) printf("  -> writes PERSIST: the install bug is in ext2/copy logic, not NVMe persistence.\n");
        else          printf("  -> writes DON'T persist across power-off: write-cache/flush issue on this controller.\n");
        return;
    }
}

// Parse a drive selector for the installer commands: "n0"/"nvme"/"nvme0" -> the
// NVMe namespace, else an ATA drive index. Lets nyxpart/mkfs/mount/nyxinstall
// target either disk (block I/O routes through blockdev.c).
static uint8_t parse_blk_dev(const char* s) {
    if (s[0] == 'n') return BLK_NVME0;
    return (uint8_t)atoi(s);
}

// Bring the selected device up (idempotent for NVMe) and return its total 512-B
// sector count, or 0 on failure. NVMe must present 512-B logical blocks.
static uint64_t blk_dev_sectors(uint8_t dev) {
    if (dev == BLK_NVME0) {
        if (nvme_init() != 0) { printf("  no NVMe controller\n"); return 0; }
        nvme_identify();
        nvme_create_io_queues();
        if (!nvme_io_ready()) { printf("  NVMe I/O queue not up\n"); return 0; }
        if (nvme_block_size() != 512) { printf("  NVMe LBA size %u B unsupported (need 512)\n", nvme_block_size()); return 0; }
        return nvme_capacity_blocks();
    }
    ata_init();
    static uint16_t id[256];
    if (ata_identify(dev, id) != 0) { printf("  no disk at drive %u\n", dev); return 0; }
    return ata_id_sectors(id);
}

// nyxpart <drive> new [size_MB] — write a fresh MBR with one bootable Linux
// (0x83) partition (whole disk, or size_MB), 1 MiB-aligned. ERASES the drive's
// existing partition table. First WRITE rung of nyxinstall; the installer TUI
// will drive this later. Destructive, so it needs the explicit `new` keyword.
// <drive> is an ATA index (0/1) or `n0` for the NVMe namespace.
static void cmd_nyxpart(int argc, char** argv) {
    if (argc < 3 || strcmp(argv[2], "new") != 0) {
        printf("Usage: nyxpart <drive> new [size_MB]\n");
        printf("  Writes a fresh MBR: one bootable Linux (0x83) partition.\n");
        printf("  WARNING: this ERASES the drive's existing partition table.\n");
        return;
    }
    uint8_t dev = parse_blk_dev(argv[1]);
    uint64_t total = blk_dev_sectors(dev);
    if (total == 0) { printf("nyxpart: %s not available\n", argv[1]); return; }
    uint32_t start = 2048;                               // 1 MiB alignment
    if (total <= start) { printf("nyxpart: disk too small\n"); return; }
    uint32_t avail = (total - start) > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)(total - start);
    uint32_t want = avail;
    if (argc >= 4) {
        uint32_t s = (uint32_t)atoi(argv[3]) * 2048u;    // MB -> 512-byte sectors
        if (s > 0 && s < avail) want = s;
    }
    mbr_part_t p = { .status = 0x80, .type = 0x83, .lba_start = start, .sectors = want };
    static uint8_t sec[512];
    mbr_build(&p, 1, sec);
    if (blk_write1(dev, 0, sec) != 0) { printf("nyxpart: write to %s failed\n", argv[1]); return; }
    blk_flush(dev);                   // commit the partition table to the medium (ATA + NVMe)
    char hb[24];
    du_human((uint64_t)want * 512ULL, hb, sizeof(hb));
    printf("nyxpart: %s partitioned - 1x Linux %s at LBA %u [boot]\n", argv[1], hb, start);
    printf("  run 'disks' to confirm.\n");
}

// nyxgpt <drive> confirm — write a fresh GUID Partition Table: an EFI System
// Partition (FAT, a later rung) + a NyxOS partition. This is the UEFI-boot disk
// layout the real laptop needs (vs the legacy MBR nyxpart writes). Destructive;
// the on-disk GPT is validated on the host with `sgdisk -v` / `gdisk -l`.
static void cmd_nyxgpt(int argc, char** argv) {
    if (argc < 3 || strcmp(argv[2], "confirm") != 0) {
        printf("Usage: nyxgpt <drive> confirm\n");
        printf("  Writes a fresh GPT: 1x EFI System Partition + 1x NyxOS partition.\n");
        printf("  WARNING: this ERASES the drive's existing partition table.\n");
        return;
    }
    uint8_t dev = parse_blk_dev(argv[1]);
    uint64_t total = blk_dev_sectors(dev);
    if (total == 0) { printf("nyxgpt: %s not available\n", argv[1]); return; }

    uint64_t last_usable = (total > 34) ? total - 34 : 0;
    uint64_t esp_start = 2048;                                // 1 MiB alignment
    if (last_usable <= esp_start + 4096) { printf("nyxgpt: disk too small for a GPT layout\n"); return; }
    uint64_t usable = last_usable - esp_start + 1;
    uint64_t esp_sectors = 131072;                            // 64 MiB ESP
    if (esp_sectors > usable / 2) esp_sectors = (usable / 4) & ~2047ULL;
    if (esp_sectors < 2048) esp_sectors = 2048;
    uint64_t nyx_start = esp_start + esp_sectors;
    if (nyx_start > last_usable || (last_usable - nyx_start + 1) < 2048) {
        printf("nyxgpt: disk too small for a NyxOS partition\n"); return;
    }
    uint64_t nyx_sectors = last_usable - nyx_start + 1;

    // Unique GUIDs: a fixed template varied by a boot-time seed + slot (the kernel
    // has no RNG; a GPT only needs non-zero uniqueness, not randomness).
    uint32_t seed = get_ticks();
    uint8_t dg[16], eg[16], ng[16];
    for (int i = 0; i < 16; i++) { dg[i] = (uint8_t)(0x40 + i); eg[i] = (uint8_t)(0x50 + i); ng[i] = (uint8_t)(0x60 + i); }
    dg[0] ^= (uint8_t)seed; eg[0] ^= (uint8_t)(seed >> 8); ng[0] ^= (uint8_t)(seed >> 16); dg[1] ^= (uint8_t)(seed >> 24);

    printf("=== nyxgpt: target %s (%u sectors) ===\n", argv[1], (uint32_t)total);
    int r = gpt_write_disk(dev, total, esp_start, esp_sectors, nyx_start, nyx_sectors, dg, eg, ng);
    if (r != 0) { printf("nyxgpt: write failed (%d)\n", r); return; }
    printf("  GPT written: ESP %u MiB @ LBA %u, NyxOS %u MiB @ LBA %u\n",
           (uint32_t)((esp_sectors * 512) >> 20), (uint32_t)esp_start,
           (uint32_t)((nyx_sectors * 512) >> 20), (uint32_t)nyx_start);
    gpt_list(dev);
}

// nyxfat <drive> confirm — format the disk's EFI System Partition (located via its
// GPT) as FAT32 and populate /EFI/BOOT with BOOTX64.EFI + grub.cfg. A UEFI firmware
// boots \EFI\BOOT\BOOTX64.EFI, so this is rung E2 of the UEFI-install path (the real
// grub-efi loader replaces the placeholder in the next rung). Destructive; the
// written FAT is host-validated with fsck.fat.
static void cmd_nyxfat(int argc, char** argv) {
    if (argc < 3 || strcmp(argv[2], "confirm") != 0) {
        printf("Usage: nyxfat <drive> confirm\n");
        printf("  Formats the ESP (from the GPT) FAT32 + writes /EFI/BOOT/{BOOTX64.EFI,grub.cfg}.\n");
        printf("  WARNING: erases the EFI System Partition. Run 'nyxgpt <drive> confirm' first.\n");
        return;
    }
    uint8_t dev = parse_blk_dev(argv[1]);
    if (blk_dev_sectors(dev) == 0) { printf("nyxfat: %s not available\n", argv[1]); return; }
    uint64_t esp_start = 0, esp_sectors = 0;
    if (gpt_find_esp(dev, &esp_start, &esp_sectors) != 0) {
        printf("nyxfat: no EFI System Partition found (run 'nyxgpt %s confirm' first)\n", argv[1]);
        return;
    }
    static const char* gcfg =
        "set timeout=3\nset default=0\n"
        "insmod part_gpt\ninsmod ext2\ninsmod all_video\n"
        "menuentry 'NyxOS' {\n"
        "    search --no-floppy --set=root --file /boot/nyx-kernel.bin\n"
        "    multiboot2 /boot/nyx-kernel.bin\n"
        "    boot\n}\n";
    // The real GRUB x86_64-efi loader, embedded as a byte array (grubefi_blob.h);
    // a UEFI firmware runs it from \EFI\BOOT\BOOTX64.EFI to load the multiboot2 kernel.
    printf("=== nyxfat: ESP @ LBA %u (%u sectors) on %s ===\n",
           (uint32_t)esp_start, (uint32_t)esp_sectors, argv[1]);
    int r = fat_format_esp(dev, (uint32_t)esp_start, (uint32_t)esp_sectors,
                           grubefi_bootx64, GRUBEFI_BOOTX64_SIZE, gcfg, (uint32_t)strlen(gcfg));
    if (r != 0) { printf("nyxfat: format failed (%d)\n", r); return; }
    printf("  ESP formatted FAT32 - /EFI/BOOT/BOOTX64.EFI (%u B, GRUB-EFI) + grub.cfg (%u B) written.\n",
           GRUBEFI_BOOTX64_SIZE, (uint32_t)strlen(gcfg));
}

// mkfs <drive> [part_lba] [size_MB] — format a fresh ext2 (single block group,
// 1024-byte blocks) onto a disk at part_lba (default LBA 2048, matching nyxpart).
// Destructive. 3rd nyxinstall rung; pairs with nyxpart + mount.
static void cmd_mkfs(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: mkfs <drive> [part_lba] [size_MB]\n");
        printf("  Formats a fresh ext2 filesystem. WARNING: erases the target area.\n");
        return;
    }
    uint8_t dev = parse_blk_dev(argv[1]);
    uint32_t part_lba = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 2048;
    uint64_t total_sectors = blk_dev_sectors(dev);
    if (total_sectors == 0) { printf("mkfs: %s not available\n", argv[1]); return; }
    if (total_sectors <= part_lba) { printf("mkfs: part_lba past end of disk\n"); return; }
    uint64_t avail_blocks = (total_sectors - part_lba) / 2;         // 1024-byte blocks
    uint32_t blocks = avail_blocks > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)avail_blocks;
    if (argc >= 4) {
        uint32_t b = (uint32_t)atoi(argv[3]) * 1024u;               // MB -> 1024-byte blocks
        if (b > 0 && b < blocks) blocks = b;
    }
    if (blocks < 64) { printf("mkfs: target too small\n"); return; }
    if (blocks > 32u * 8192 + 1) { blocks = 32u * 8192 + 1; printf("mkfs: capping to 256 MB (32 block groups)\n"); }
    char hb[24];
    du_human((uint64_t)blocks * 1024ULL, hb, sizeof(hb));
    printf("mkfs: formatting %s @ LBA %u as ext2 (%s)...\n", argv[1], part_lba, hb);
    if (ext2_format(dev, part_lba, blocks) != 0) { printf("mkfs: format failed\n"); return; }
    printf("mkfs: done. mount it with:  mount %s %u\n", argv[1], part_lba);
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

// ---- shell variable expansion ($VAR / ${VAR}) — used by execute_command ----
static int sh_name_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
static int sh_name_cont(char c)  { return sh_name_start(c) || (c >= '0' && c <= '9'); }

// Value of shell variable `name` (length `namelen`), or NULL if unset. Reads the same
// env_vars[] table `export`/`env` populate ("NAME=value" strings).
static const char* shell_lookup_var(const char* name, int namelen) {
    for (int e = 0; e < env_count; e++) {
        const char* eq = strchr(env_vars[e], '=');
        if (eq && (int)(eq - env_vars[e]) == namelen && strncmp(env_vars[e], name, (size_t)namelen) == 0)
            return eq + 1;
    }
    return 0;
}

// Expand $NAME and ${NAME} references in `in` into `out` (bounded by outsz). NAME is
// [A-Za-z_][A-Za-z0-9_]*, looked up in the shell env; an undefined variable expands to
// empty (bash semantics). A '$' that does NOT begin a valid reference — end of string, a
// following digit / other non-name char, or an unclosed '${' — is copied literally, as is
// every other character. Pure logic; execute_command only calls it for tokens of a command
// line that actually contains a '$'. KAT: shell_expand_selftest.
static void shell_expand_vars(const char* in, char* out, int outsz) {
    int o = 0;
    for (int i = 0; in[i] && o < outsz - 1; ) {
        if (in[i] == '$' && (sh_name_start(in[i + 1]) || in[i + 1] == '{')) {
            int braced = (in[i + 1] == '{');
            int s = i + 1 + braced, e = s;
            while (in[e] && sh_name_cont(in[e])) e++;
            if (braced && in[e] != '}') { out[o++] = in[i++]; continue; }   // unclosed ${... -> literal '$'
            int namelen = e - s;
            if (namelen == 0) { out[o++] = in[i++]; continue; }             // "$" / "${}"    -> literal '$'
            const char* val = shell_lookup_var(in + s, namelen);
            if (val) for (int k = 0; val[k] && o < outsz - 1; k++) out[o++] = val[k];   // unset -> empty
            i = e + braced;                                                 // consume name (+ closing brace)
        } else {
            out[o++] = in[i++];
        }
    }
    out[o] = '\0';
}

// KAT (`shellvar`): $VAR / ${VAR}, embedded, adjacent, undefined-to-empty, and the literal
// cases ('$' before a digit, at end of string, or an unclosed '${'). Injects two temporary
// env vars, restores env_count after. Returns 0 on pass, else the failing case number.
static int shell_expand_selftest(void) {
    int saved = env_count;
    static char v1[] = "FOO=bar";
    static char v2[] = "HOME=/mnt/home/nyx";
    if (env_count + 2 > 16) return 99;
    env_vars[env_count++] = v1;
    env_vars[env_count++] = v2;
    char out[256];
    int rc = 0, n = 0;
    #define SX(inp, want) do { n++; shell_expand_vars((inp), out, sizeof(out)); \
        if (strcmp(out, (want)) != 0) { rc = n; goto done; } } while (0)
    SX("$FOO",         "bar");            // 1  bare
    SX("${FOO}",       "bar");            // 2  braced
    SX("pre$FOO/post", "prebar/post");    // 3  embedded, delimited by '/'
    SX("$HOME/x",      "/mnt/home/nyx/x");// 4  value with slashes
    SX("${HOME}s",     "/mnt/home/nyxs"); // 5  braced lets a name char follow
    SX("$FOO$FOO",     "barbar");         // 6  adjacent
    SX("$UNDEF",       "");               // 7  undefined -> empty
    SX("a${UNDEF}b",   "ab");             // 8  undefined braced -> empty
    SX("literal",      "literal");        // 9  no '$'
    SX("cost$5",       "cost$5");         // 10 '$' before a digit -> literal
    SX("end$",         "end$");           // 11 trailing '$'       -> literal
    SX("${FOO",        "${FOO");          // 12 unclosed '${'      -> literal
    #undef SX
done:
    env_count = saved;
    return rc;
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

// Substring test for grep: plain strstr, or a case-folding scan when -i is set
// (the kernel libc has no strcasestr). ASCII-only lowercasing.
static int grep_line_match(const char* hay, const char* needle, int ignore_case) {
    if (!ignore_case) return strstr(hay, needle) != NULL;
    if (!needle[0]) return 1;
    for (const char* h = hay; *h; h++) {
        const char* a = h; const char* b = needle;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static void cmd_grep(int argc, char** argv) {
    int ignore_case = 0, show_lineno = 0, invert = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'i') ignore_case = 1;
            else if (*f == 'n') show_lineno = 1;
            else if (*f == 'v') invert = 1;
            else { printf("grep: invalid option -%c\n", *f); return; }
        }
    if (ai + 1 >= argc) { printf("Usage: grep [-inv] <pattern> <file>\n"); return; }
    const char* pat = argv[ai];
    const char* path = argv[ai + 1];
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) { printf("grep: cannot open '%s'\n", path); return; }
    static char buf[8192];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    int line = 1;
    char* p = buf;
    while (*p) {
        char* nl = p;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = '\0';
        int hit = grep_line_match(p, pat, ignore_case);
        if (invert) hit = !hit;
        if (hit) {
            if (show_lineno) printf("%d:%s\n", line, p);
            else            printf("%s\n", p);
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

// Leading-integer key for `sort -n` (sign-aware); a non-numeric line keys as 0,
// so "10" orders after "9" instead of lexically before it.
static long sort_numkey(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static void cmd_sort(int argc, char** argv) {
    int reverse = 0, numeric = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'r') reverse = 1;
            else if (*f == 'n') numeric = 1;
            else { printf("sort: invalid option -%c\n", *f); return; }
        }
    if (ai >= argc) { printf("Usage: sort [-r] [-n] <file>\n"); return; }
    int fd = vfs_open(argv[ai], 0, 0);
    if (fd < 0) { printf("sort: cannot open '%s'\n", argv[ai]); return; }
    static char buf[8192];
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes <= 0) return;
    buf[bytes] = '\0';
    char* lines[256];
    int lc = 0;
    lines[lc++] = buf;
    for (int i = 0; buf[i] && lc < 256; i++) {
        if (buf[i] == '\n') { buf[i] = '\0'; if (buf[i+1]) lines[lc++] = &buf[i+1]; }
    }
    for (int i = 0; i < lc - 1; i++) {
        for (int j = 0; j < lc - i - 1; j++) {
            int cmp;
            if (numeric) {
                long a = sort_numkey(lines[j]), b = sort_numkey(lines[j+1]);
                cmp = (a > b) - (a < b);
            } else {
                cmp = strcmp(lines[j], lines[j+1]);
            }
            if (reverse) cmp = -cmp;
            if (cmp > 0) { char* tmp = lines[j]; lines[j] = lines[j+1]; lines[j+1] = tmp; }
        }
    }
    for (int i = 0; i < lc; i++) printf("%s\n", lines[i]);
}

static void cmd_wc(int argc, char** argv) {
    // -l/-w/-c (or -m) select which of the line/word/byte counts to print, in
    // that fixed order; with no flag all three are shown (the classic default).
    int want_l = 0, want_w = 0, want_c = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'l') want_l = 1;
            else if (*f == 'w') want_w = 1;
            else if (*f == 'c' || *f == 'm') want_c = 1;
            else { printf("wc: invalid option -%c\n", *f); return; }
        }
    }
    if (!want_l && !want_w && !want_c) { want_l = want_w = want_c = 1; }
    if (ai >= argc) { printf("Usage: wc [-l] [-w] [-c] <file>\n"); return; }

    const char* path = argv[ai];
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) { printf("wc: cannot open '%s'\n", path); return; }
    static char buf[8192];                       // static: kept off the kernel stack
    int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (bytes < 0) bytes = 0;
    int lines = 0, words = 0, chars = bytes, in_word = 0;
    for (int i = 0; i < bytes; i++) {
        char c = buf[i];
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') in_word = 0;
        else if (!in_word) { in_word = 1; words++; }
    }

    int first = 1;
    if (want_l) { printf(first ? "%d" : " %d", lines); first = 0; }
    if (want_w) { printf(first ? "%d" : " %d", words); first = 0; }
    if (want_c) { printf(first ? "%d" : " %d", chars); first = 0; }
    printf(" %s\n", path);
}

static void cmd_write(int argc, char** argv) {
    if (argc < 3) { printf("Usage: write <file> <text>\n"); return; }
    if (!path_last_component_ok(argv[1])) { printf("write: invalid file name '%s'\n", argv[1]); return; }
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

#include "../drivers/audio/sb16.h"
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

#include "../proc/elf.h"
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
    if (argc < 2) { printf("Usage: exec <file> [args...]\n"); return; }
    // Forward the rest of the line as the program's argv (argv[0] = the path), so
    // `exec /wget.elf http://...` runs a userspace tool with arguments straight from
    // the kernel shell. The foreground-wait machinery lives in run_foreground_elf(),
    // shared with the auto-exec fallback (a bare `wget …` line).
    run_foreground_elf(argv[1], &argv[1], argc - 1);
}

// `cc [-c] <in.c/.o ...> [-o <out>] [tcc-flags...]` — compile and/or link C in-OS by
// driving the ported tcc (/tcc.elf). Default (link) mode turns one or more sources
// and/or objects into a runnable NyxOS executable with the NyxOS runtime defaults: a
// freestanding static link against the initramfs crt0.o + libc.o, and NO -Ttext (tcc
// then bases the image at its default 0x400000, which elf_load accepts — see the
// tccelf.c static-PLT fix at v6.3.0). With -c, it compiles each source to a .o and
// does NOT link (crt0.o/libc.o are omitted), enabling separate compilation
// (`cc -c a.c -o a.o` then `cc a.o b.o -o prog`). Any argument that is not `-o <out>`
// is forwarded to tcc verbatim, so extra inputs join the command and flags like -D/-I
// pass through. Runs tcc in the foreground (blocks until it exits, like `exec`,
// printing its exit code). With no -o, <out> is the first input minus ".c" (plus ".o"
// in -c mode). Use absolute paths (e.g. cc /mnt/a.c /mnt/b.c -o /mnt/prog).
/* Ensure /mnt/tcc_self.elf exists — the tcc built by the in-OS tcc. If it is not there
 * yet, build it on demand from the shipped tcc source tree and cache it on the persistent
 * mount: the in-OS tcc compiles tcc's own ONE_SOURCE (tcc.c) to an object, then links it
 * with tcc's OS adapter (nyxshim.o) + runtime (libtcc1.o) into a runnable tcc. The FIRST
 * `cc --self` therefore takes a while (~minutes); later ones reuse the cached executable.
 * Returns 1 if /mnt/tcc_self.elf is available afterwards, 0 if the build failed. */
static int tcc_self_ready(void) {
    int fd = vfs_open("/mnt/tcc_self.elf", 0, 0);
    if (fd >= 0) { vfs_close(fd); return 1; }             // already built — use the cache
    printf("cc: --self: building the self-hosted tcc (first use; this takes a while)...\n");
    execute_command("cc -c /usr/src/nyx/tcc/tcc.c -I/usr/src/nyx/tcc -I/usr/src/nyx/tcc/nyxshim -o /mnt/tcc_self.o");
    execute_command("cc /mnt/tcc_self.o /nyxshim.o /libtcc1.o -o /mnt/tcc_self.elf");
    fd = vfs_open("/mnt/tcc_self.elf", 0, 0);
    if (fd >= 0) { vfs_close(fd); return 1; }
    return 0;                                              // build failed — caller reports it
}

static void cmd_cc(int argc, char** argv) {
    if (argc < 2) { printf("Usage: cc [-c] [--self] [--self-libc] <in.c/.o ...> [-o <out>]\n"); return; }
    int compile_only = 0;                        // -c: compile a .c to a .o, do NOT link
    int self_libc = 0;                           // --self-libc: link against a tcc-built libc
    int self_host = 0;                           // --self: compile with the self-built tcc, not /tcc.elf
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0)          compile_only = 1;
        else if (strcmp(argv[i], "--self-libc") == 0) self_libc = 1;
        else if (strcmp(argv[i], "--self") == 0) self_host = 1;
    }
    if (self_libc && compile_only) {
        printf("cc: --self-libc has no effect with -c (compile-only does not link)\n"); return;
    }
    // Resolve the output path and the first source first (both drive later steps).
    const char* out = 0;
    const char* first_src = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out = argv[++i]; continue; }
        if (strcmp(argv[i], "--self-libc") == 0) continue;
        if (argv[i][0] != '-' && !first_src) first_src = argv[i];
    }
    if (!first_src) { printf("cc: no input files\n"); return; }
    char outbuf[128];
    if (!out) {                                  // default output: first input minus ".c"
        int ln = (int)strlen(first_src);         // (+ ".o" in compile-only mode)
        if (ln > 2 && first_src[ln - 2] == '.' && first_src[ln - 1] == 'c'
            && ln + 1 < (int)sizeof(outbuf)) {
            memcpy(outbuf, first_src, (size_t)(ln - 2)); outbuf[ln - 2] = '\0';
            if (compile_only) { outbuf[ln - 2] = '.'; outbuf[ln - 1] = 'o'; outbuf[ln] = '\0'; }
        } else {
            strncpy(outbuf, compile_only ? "a.o" : "a.out", sizeof(outbuf) - 1);
            outbuf[sizeof(outbuf) - 1] = '\0';
        }
        out = outbuf;
    }
    // --self routes every tcc invocation below through the self-built compiler
    // (/mnt/tcc_self.elf, built + cached on demand) instead of the GCC-cross-built /tcc.elf.
    const char* ccpath = "/tcc.elf";
    if (self_host) {
        if (!tcc_self_ready()) { printf("cc: --self: the self-hosted tcc is unavailable\n"); return; }
        ccpath = "/mnt/tcc_self.elf";
    }
    // --self-libc: FIRST build the runtime from source with tcc, then link the program
    // against THOSE objects instead of the prebuilt (GCC-built) /libc.o + /va_list.o. This
    // proves the tcc-compiled runtime actually runs — the program's printf/malloc/strlen and
    // its varargs support are served by code NyxOS's own compiler produced. We self-compile
    // BOTH the OS's own libc AND tcc's own va-runtime (libtcc1's va_list.c), so the linked
    // program's whole C runtime is tcc-built (only tiny crt0.o remains GCC-built). The self-
    // built objects are written next to the output (<out>.slo / <out>.vlo) so they land in
    // the same writable directory the user chose.
    char libcobj[160], vlistobj[160], ltccobj[160];
    if (self_libc) {
        int ol = (int)strlen(out);
        if (ol + 5 >= (int)sizeof(libcobj)) { printf("cc: output path too long for --self-libc\n"); return; }
        memcpy(libcobj,  out, (size_t)ol); memcpy(libcobj  + ol, ".slo", 5);  // "<out>.slo"
        memcpy(vlistobj, out, (size_t)ol); memcpy(vlistobj + ol, ".vlo", 5);  // "<out>.vlo"
        memcpy(ltccobj,  out, (size_t)ol); memcpy(ltccobj  + ol, ".tlo", 5);  // "<out>.tlo"
        // The whole tcc runtime, self-compiled: the OS libc, tcc's va-runtime, and tcc's
        // main runtime library libtcc1.c (float <-> 64-bit-int helpers the codegen emits).
        const char* srcs[3] = { "/usr/src/nyx/libc.c", "/usr/src/nyx/va_list.c", "/usr/src/nyx/libtcc1.c" };
        char* objs[3] = { libcobj, vlistobj, ltccobj };
        for (int p = 0; p < 3; p++) {
            char* cav[8]; int cn = 0;
            cav[cn++] = "tcc";
            cav[cn++] = "-I/usr/lib/tcc/include";
            cav[cn++] = "-c";
            cav[cn++] = (char*)srcs[p];          // OS libc / tcc's own va-runtime, from the initramfs
            cav[cn++] = "-o"; cav[cn++] = objs[p];
            cav[cn] = 0;
            printf("cc: self-libc: tcc -c %s -> %s\n", srcs[p], objs[p]);
            int rc = run_foreground_elf(ccpath, cav, cn);
            if (rc != 0) { printf("cc: self-libc build of %s failed (code %d)\n", srcs[p], rc); return; }
        }
    }
    // Build the compile/link argv.
    char* av[64];
    int n = 0;
    av[n++] = "tcc";
    av[n++] = "-I/usr/lib/tcc/include";          // tcc's freestanding headers (shipped in the initramfs)
    av[n++] = "-I/usr/src/nyx";                  // the OS's own libc.h/syscall.h, so real NyxOS
                                                 // sources that `#include "libc.h"` compile as-is
    if (!compile_only) {                         // link mode: freestanding static exe over our runtime
        av[n++] = "-nostdlib"; av[n++] = "-static";
        av[n++] = "/crt0.o";
        av[n++] = self_libc ? libcobj  : (char*)"/libc.o";     // tcc-built libc vs prebuilt libc
        av[n++] = self_libc ? vlistobj : (char*)"/va_list.o";  // tcc-built va-runtime vs prebuilt
        if (self_libc) av[n++] = ltccobj;                      // tcc-built libtcc1 float/int64 runtime
    }
    for (int i = 1; i < argc && n < 60; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { i++; continue; }  // -o <out> consumed above
        if (strcmp(argv[i], "--self-libc") == 0) continue;                  // consumed above
        if (strcmp(argv[i], "--self") == 0) continue;                       // consumed above (compiler-select flag)
        av[n++] = argv[i];                       // forward sources/objects + any tcc flags (incl -c)
    }
    av[n++] = "-o"; av[n++] = (char*)out; av[n] = 0;
    printf("cc: %s (%s) -> %s\n", compile_only ? "compiling object" : "compiling",
           self_host ? "self-hosted tcc" : "tcc", out);
    run_foreground_elf(ccpath, av, n);           // blocks until the compiler exits; prints its exit code
}

/* Read a `key:` line's value from a recipe buffer into `out` (leading/trailing spaces
 * trimmed; first match wins). Returns 1 if the key was found with a non-empty value. */
static int pkg_recipe_value(const char* buf, const char* key, char* out, int outsz) {
    int klen = (int)strlen(key);
    for (const char* p = buf; *p; ) {
        const char* s = p;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, key, klen) == 0 && s[klen] == ':') {
            const char* v = s + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            int i = 0;
            while (*v && *v != '\n' && *v != '\r' && i < outsz - 1) out[i++] = *v++;
            while (i > 0 && (out[i-1] == ' ' || out[i-1] == '\t')) i--;   // trim trailing
            out[i] = '\0';
            return out[0] != '\0';
        }
        while (*p && *p != '\n') p++;                // advance to the next line
        if (*p == '\n') p++;
    }
    return 0;
}

// A package name / recipe filename token must be a PLAIN name: non-empty, not
// "."/".." and containing no '/' or whitespace. Values interpolated into the paths
// and the `cc` command line — the CLI `name` (/usr/pkg/<name>/recipe) and the
// recipe's `source:`/`bin:` fields (compile input + /mnt/bin/<bin> output) — are
// validated with this, so a crafted name or recipe can't traverse out of /usr/pkg
// or /mnt/bin (arbitrary read/write) nor inject extra `cc` arguments via a space.
static int pkg_valid_name(const char* s) {
    if (!s || !s[0]) return 0;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return 0;
    for (const char* p = s; *p; p++)
        if (*p == '/' || *p == ' ' || *p == '\t') return 0;
    return 1;
}

/* xbm — the NyxOS package manager (our pacman/apt), built on the in-OS `cc` toolchain.
 * A package lives at /usr/pkg/<name>/ with a `recipe` manifest (dead-simple `key: value`
 * lines: `source:` = the .c to build, `bin:` = the installed program name) plus its source.
 * `xbm install <name>` reads the recipe, compiles the source with the in-OS `cc`, and
 * installs the binary to /mnt/bin on the persistent disk; since /mnt/bin is on the shell's
 * PATH-like search (resolve_user_elf), the installed program then runs by bare name.
 * `xbm remove <name>` unlinks the installed binary (its name comes from the recipe's
 * `bin:`); `xbm search <substr>` greps the repo by name; `xbm list` shows available
 * packages and `xbm list --installed` shows what's in /mnt/bin. `pkg` is a hidden alias.
 * (No network fetch / deps / versions yet — see the package-manager roadmap.) */
// Parse an `http://host[:port]/path` URL into host/port/path (bounded). Returns 0 on
// success, -1 if it is not an http:// URL. (https is a later step — it needs the TLS
// path; for now xbm fetches over plain HTTP.) Mirrors the parser in cmd_httpget.
static int xbm_parse_url(const char* url, char* host, int hostsz, uint16_t* port, char* path, int pathsz) {
    int https = 0;
    if (url_parse(url, host, hostsz, port, path, pathsz, &https) != 0) return -1;
    if (https) return -1;                    // xbm fetches over plain HTTP only (no TLS)
    return 0;
}

// Given an ALREADY-PARSED HTTP response, save its body to `outpath` (truncating any
// existing file). Requires a 200 with a body. This is the deterministic core of the
// network fetch — it takes an http_response_t, not a socket, so it is unit-testable
// by feeding a canned response through http_parse_response. Returns 0 on success.
static int xbm_save_resp_body(const http_response_t* r, const char* outpath) {
    if (!r || r->status_code != 200 || !r->body) return -1;
    vfs_unlink(outpath);                          // start clean so no stale tail survives
    int fd = vfs_open(outpath, 1, 0);             // O_CREAT
    if (fd < 0) return -1;
    int wrote = (r->body_len > 0) ? vfs_write(fd, r->body, r->body_len) : 0;
    vfs_close(fd);
    return (wrote == (int)r->body_len) ? 0 : -1;
}

// Fetch an http:// URL and save its body to `outpath`. Returns 0 on success. The
// socket half (http_get) can't be exercised headless; the parse + save half is what
// the self-test covers via xbm_save_resp_body on a canned response.
static int xbm_fetch_url(const char* url, const char* outpath) {
    char host[128], path[256]; uint16_t port;
    if (xbm_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) return -1;
    int iface = -1;
    for (int i = 0; i < 8; i++)
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) { iface = i; break; }
    http_response_t resp;
    if (http_get(host, port, path, &resp, iface) < 0) return -1;
    int rc = xbm_save_resp_body(&resp, outpath);
    http_free(&resp);
    return rc;
}

// ---- xbm package integrity: SHA-256 manifests (apt/pacman-style tamper detection) ----------
static void pkg_hex_encode(const uint8_t* in, int n, char* out) {   // lowercase hex, out[2n]='\0'
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < n; i++) { out[2 * i] = H[in[i] >> 4]; out[2 * i + 1] = H[in[i] & 15]; }
    out[2 * n] = '\0';
}
// SHA-256 a whole VFS file into out_hex[65] (64 lowercase-hex + NUL). vfs_read has no per-fd
// offset (it returns the file from the start), so the file is hashed in one read via a static
// off-stack buffer — the 4 KB kernel task stack is untouched. 0 = ok, -1 = unreadable/missing,
// -2 = larger than the hash buffer (refuse rather than hash a truncated prefix).
static uint8_t pkg_hash_buf[262144];
static int pkg_sha256_file(const char* path, char out_hex[65]) {
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) return -1;
    int n = vfs_read(fd, pkg_hash_buf, sizeof(pkg_hash_buf));
    vfs_close(fd);
    if (n < 0) return -1;
    if (n >= (int)sizeof(pkg_hash_buf)) return -2;
    sha256_ctx_t c; sha256_init(&c);
    sha256_update(&c, pkg_hash_buf, (uint32_t)n);
    uint8_t dg[32]; sha256_final(&c, dg);
    pkg_hex_encode(dg, 32, out_hex);
    return 0;
}
// KAT for the integrity feature: hashes real VFS files through the exact path `xbm verify` uses,
// checking a known SHA-256 answer (validates the hex encoder + digest), that a one-byte change
// flips the digest (tamper is detectable), and that a missing file reports -1.
int pkg_hash_selftest(void) {
    vfs_write_file("/tmp/pkgkat.bin", "NyxOS-pkg", 9);
    char hex[65];
    if (pkg_sha256_file("/tmp/pkgkat.bin", hex) != 0) return 1;
    static const char* want = "980d716507be4aa72aaf3209229103692285a67136626bf8cabad208c6666410";
    for (int i = 0; i < 64; i++) if (hex[i] != want[i]) return 2;
    vfs_write_file("/tmp/pkgkat.bin", "NyxOT-pkg", 9);          // flip one byte
    char hex2[65];
    if (pkg_sha256_file("/tmp/pkgkat.bin", hex2) != 0) return 3;
    if (strncmp(hex, hex2, 64) == 0) return 4;                  // digest MUST change
    char hx[65];
    if (pkg_sha256_file("/tmp/nope-xyz-404", hx) != -1) return 5;
    return 0;
}

// ---- xbm dependency resolution: topological install order + cycle detection ------------------
// A recipe may declare `deps: <pkg> <pkg> ...`; `xbm deps <name>` prints the install order (each
// dependency before the package that needs it, target last), or reports a cycle / a missing
// package. Iterative DFS with an explicit STATIC stack (off the 4 KB task stack); the static
// scratch makes it non-reentrant, which is fine — xbm runs serially in the shell.
#define XBM_MAXP    64
#define XBM_MAXD    16
#define XBM_NAMELEN 32
enum { XBM_OK = 0, XBM_EMISSING = -1, XBM_ECYCLE = -2, XBM_EOVERFLOW = -3 };
typedef int (*xbm_deps_fn)(const char* name, char deps[][XBM_NAMELEN], int maxd, void* ctx);

static char xr_name[XBM_MAXP][XBM_NAMELEN];
static struct { int node; int di; char deps[XBM_MAXD][XBM_NAMELEN]; int nd; } xr_stk[XBM_MAXP];
static unsigned char xr_color[XBM_MAXP];   // 0 white, 1 gray (on stack), 2 black (done)

static int xbm_resolve(const char* target, xbm_deps_fn get, void* ctx, char order[][XBM_NAMELEN], int* pn) {
    int nn = 0;
    #define XR_INTERN(s, outidx) do {                                          \
        int _f = -1;                                                           \
        for (int _i = 0; _i < nn; _i++) if (strcmp(xr_name[_i], s) == 0) { _f = _i; break; } \
        if (_f < 0) { if (nn >= XBM_MAXP) return XBM_EOVERFLOW;                \
            strncpy(xr_name[nn], s, XBM_NAMELEN - 1); xr_name[nn][XBM_NAMELEN - 1] = 0; \
            xr_color[nn] = 0; _f = nn++; }                                     \
        outidx = _f;                                                           \
    } while (0)
    int ti; XR_INTERN(target, ti);
    int on = 0, sp = 0;
    int d = get(xr_name[ti], xr_stk[sp].deps, XBM_MAXD, ctx);
    if (d < 0) return XBM_EMISSING;
    xr_stk[sp].node = ti; xr_stk[sp].di = 0; xr_stk[sp].nd = d; xr_color[ti] = 1; sp++;
    while (sp > 0) {
        int top = sp - 1, node = xr_stk[top].node;
        if (xr_stk[top].di < xr_stk[top].nd) {
            char* dep = xr_stk[top].deps[xr_stk[top].di++];
            int ci; XR_INTERN(dep, ci);
            if (xr_color[ci] == 1) return XBM_ECYCLE;      // back-edge to a node on the stack
            if (xr_color[ci] == 2) continue;               // already resolved
            if (sp >= XBM_MAXP) return XBM_EOVERFLOW;
            int dd = get(xr_name[ci], xr_stk[sp].deps, XBM_MAXD, ctx);
            if (dd < 0) return XBM_EMISSING;
            xr_stk[sp].node = ci; xr_stk[sp].di = 0; xr_stk[sp].nd = dd; xr_color[ci] = 1; sp++;
        } else {
            xr_color[node] = 2;                            // post-order: deps all done
            if (on >= XBM_MAXP) return XBM_EOVERFLOW;
            strncpy(order[on], xr_name[node], XBM_NAMELEN - 1); order[on][XBM_NAMELEN - 1] = 0; on++;
            sp--;
        }
    }
    *pn = on;
    return XBM_OK;
    #undef XR_INTERN
}

// VFS-backed dependency source: read /usr/pkg/<name>/recipe, split its `deps:` line into names.
// Returns the direct-dep count, 0 if the recipe has no `deps:`, or -1 if the package has no recipe.
static int xbm_vfs_get_deps(const char* name, char deps[][XBM_NAMELEN], int maxd, void* ctx) {
    (void)ctx;
    if (!pkg_valid_name(name)) return -1;
    char rpath[160];
    snprintf(rpath, sizeof(rpath), "/usr/pkg/%s/recipe", name);
    int fd = vfs_open(rpath, 0, 0);
    if (fd < 0) return -1;
    char rbuf[512];
    int n = vfs_read(fd, rbuf, sizeof(rbuf) - 1);
    vfs_close(fd);
    if (n < 0) return -1;
    rbuf[n] = '\0';
    char dline[256];
    if (!pkg_recipe_value(rbuf, "deps", dline, sizeof(dline))) return 0;   // recipe exists, no deps
    int nd = 0;
    for (char* p = dline; *p && nd < maxd; ) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ' && *p != '\t' && k < XBM_NAMELEN - 1) deps[nd][k++] = *p++;
        deps[nd][k] = '\0';
        while (*p && *p != ' ' && *p != '\t') p++;   // skip any overflow of an over-long name
        nd++;
    }
    return nd;
}

// KAT: resolve fixed dependency graphs from an in-memory table (deterministic order + errors).
struct xbm_tabpkg { const char* name; const char* deps[XBM_MAXD]; int nd; };
static const struct xbm_tabpkg* g_xbm_tab; static int g_xbm_ntab;
static int xbm_tab_get(const char* name, char deps[][XBM_NAMELEN], int maxd, void* ctx) {
    (void)ctx;
    for (int i = 0; i < g_xbm_ntab; i++) if (strcmp(g_xbm_tab[i].name, name) == 0) {
        int n = g_xbm_tab[i].nd; if (n > maxd) n = maxd;
        for (int j = 0; j < n; j++) { strncpy(deps[j], g_xbm_tab[i].deps[j], XBM_NAMELEN - 1); deps[j][XBM_NAMELEN - 1] = 0; }
        return n;
    }
    return -1;
}
int xbm_deps_selftest(void) {
    static char order[XBM_MAXP][XBM_NAMELEN];
    int pn;
    static const struct xbm_tabpkg chain[] = {{"a",{"b"},1},{"b",{"c"},1},{"c",{0},0}};
    g_xbm_tab = chain; g_xbm_ntab = 3;
    if (xbm_resolve("a", xbm_tab_get, 0, order, &pn) != XBM_OK) return 1;
    if (pn != 3 || strcmp(order[0],"c") || strcmp(order[1],"b") || strcmp(order[2],"a")) return 2;
    static const struct xbm_tabpkg diamond[] = {{"a",{"b","c"},2},{"b",{"d"},1},{"c",{"d"},1},{"d",{0},0}};
    g_xbm_tab = diamond; g_xbm_ntab = 4;
    if (xbm_resolve("a", xbm_tab_get, 0, order, &pn) != XBM_OK) return 3;
    if (pn != 4 || strcmp(order[0],"d") || strcmp(order[1],"b") || strcmp(order[2],"c") || strcmp(order[3],"a")) return 4;
    static const struct xbm_tabpkg cyc[] = {{"a",{"b"},1},{"b",{"a"},1}};
    g_xbm_tab = cyc; g_xbm_ntab = 2;
    if (xbm_resolve("a", xbm_tab_get, 0, order, &pn) != XBM_ECYCLE) return 5;
    static const struct xbm_tabpkg self[] = {{"a",{"a"},1}};
    g_xbm_tab = self; g_xbm_ntab = 1;
    if (xbm_resolve("a", xbm_tab_get, 0, order, &pn) != XBM_ECYCLE) return 6;
    static const struct xbm_tabpkg miss[] = {{"a",{"x"},1}};
    g_xbm_tab = miss; g_xbm_ntab = 1;
    if (xbm_resolve("a", xbm_tab_get, 0, order, &pn) != XBM_EMISSING) return 7;
    static const struct xbm_tabpkg leaf[] = {{"a",{0},0}};
    g_xbm_tab = leaf; g_xbm_ntab = 1;
    if (xbm_resolve("a", xbm_tab_get, 0, order, &pn) != XBM_OK || pn != 1 || strcmp(order[0],"a")) return 8;
    return 0;
}

// Build + install ONE package (no dependency handling): read its recipe, optionally fetch a
// remote `url:` source, compile it with the in-OS `cc`, and record a SHA-256 manifest. Returns 0
// on success, -1 on any failure. `xbm install` calls this once per resolved dependency, in order.
static int xbm_install_one(const char* name, const char* prog) {
    if (!pkg_valid_name(name)) { printf("%s: invalid package name '%s'\n", prog, name); return -1; }
    char rpath[128];
    snprintf(rpath, sizeof(rpath), "/usr/pkg/%s/recipe", name);
    int fd = vfs_open(rpath, 0, 0);
    if (fd < 0) { printf("%s: package '%s' not found\n", prog, name); return -1; }
    char rbuf[512];
    int n = vfs_read(fd, rbuf, sizeof(rbuf) - 1);
    vfs_close(fd);
    if (n <= 0) { printf("%s: empty recipe for '%s'\n", prog, name); return -1; }
    rbuf[n] = '\0';
    char source[64], bin[64];
    if (!pkg_recipe_value(rbuf, "source", source, sizeof(source))) { printf("%s: recipe for '%s' is missing 'source'\n", prog, name); return -1; }
    if (!pkg_recipe_value(rbuf, "bin", bin, sizeof(bin)))          { printf("%s: recipe for '%s' is missing 'bin'\n", prog, name); return -1; }
    if (!pkg_valid_name(source) || !pkg_valid_name(bin)) { printf("%s: recipe for '%s' has an unsafe 'source'/'bin' name\n", prog, name); return -1; }
    char url[256];
    if (pkg_recipe_value(rbuf, "url", url, sizeof(url))) {
        char srcpath[160];
        snprintf(srcpath, sizeof(srcpath), "/usr/pkg/%s/%s", name, source);
        printf("%s: fetching %s ...\n", prog, url);
        if (xbm_fetch_url(url, srcpath) != 0) { printf("%s: fetch failed for '%s'\n", prog, name); return -1; }
        printf("%s: fetched source -> %s\n", prog, srcpath);
    }
    vfs_mkdir("/mnt/bin", 0755);
    char cmd[256], outp[128];
    snprintf(outp, sizeof(outp), "/mnt/bin/%s", bin);
    snprintf(cmd, sizeof(cmd), "cc /usr/pkg/%s/%s -o %s", name, source, outp);
    printf("%s: building %s (%s) ...\n", prog, name, source);
    execute_command(cmd);
    int ofd = vfs_open(outp, 0, 0);
    if (ofd < 0) { printf("%s: build of '%s' failed\n", prog, name); return -1; }
    vfs_close(ofd);
    char hex[65];
    if (pkg_sha256_file(outp, hex) == 0) {
        char mpath[160];
        snprintf(mpath, sizeof(mpath), "/usr/pkg/%s/sha256", name);
        vfs_write_file(mpath, hex, 64);
        printf("%s: installed %s -> %s (run it: %s; sha256 %c%c%c%c%c%c%c%c...)\n",
               prog, name, outp, bin, hex[0],hex[1],hex[2],hex[3],hex[4],hex[5],hex[6],hex[7]);
    } else {
        printf("%s: installed %s -> %s (run it: %s)\n", prog, name, outp, bin);
    }
    return 0;
}

static void cmd_xbm(int argc, char** argv) {
    const char* prog = argv[0];   // "xbm" (or the "pkg" alias) — echo whatever was typed
    if (argc < 2) { printf("Usage: %s install|remove|verify|deps <name> | %s search <str> | %s list [--installed]\n", prog, prog, prog); return; }

    if (strcmp(argv[1], "list") == 0) {
        // `xbm list` = packages available in the repo; `xbm list --installed` = binaries in /mnt/bin.
        int installed = (argc >= 3 && strcmp(argv[2], "--installed") == 0);
        const char* dir = installed ? "/mnt/bin" : "/usr/pkg";
        int fd = vfs_open(dir, 0, 0);
        if (fd < 0) {
            if (installed) printf("No packages installed (%s is empty).\n", dir);
            else           printf("%s: no package repository at /usr/pkg\n", prog);
            return;
        }
        printf(installed ? "Installed packages:\n" : "Available packages:\n");
        int count = 0;
        dirent_t* de = vfs_readdir(fd);
        while (de) {
            if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0) { printf("  %s\n", de->name); count++; }
            de = vfs_readdir(fd);
        }
        vfs_close(fd);
        if (installed && count == 0) printf("  (none)\n");
        return;
    }

    if (strcmp(argv[1], "search") == 0) {
        if (argc < 3) { printf("Usage: %s search <substring>\n", prog); return; }
        const char* q = argv[2];
        int fd = vfs_open("/usr/pkg", 0, 0);
        if (fd < 0) { printf("%s: no package repository at /usr/pkg\n", prog); return; }
        int hits = 0;
        dirent_t* de = vfs_readdir(fd);
        while (de) {
            if (strcmp(de->name, ".") != 0 && strcmp(de->name, "..") != 0 && strstr(de->name, q)) {
                printf("  %s\n", de->name); hits++;
            }
            de = vfs_readdir(fd);
        }
        vfs_close(fd);
        if (hits == 0) printf("%s: no packages match '%s'\n", prog, q);
        return;
    }

    if (strcmp(argv[1], "deps") == 0) {
        // Print the topological install order for <name> from recipe `deps:` fields.
        if (argc < 3) { printf("Usage: %s deps <name>\n", prog); return; }
        const char* name = argv[2];
        if (!pkg_valid_name(name)) { printf("%s: invalid package name '%s'\n", prog, name); return; }
        static char order[XBM_MAXP][XBM_NAMELEN];
        int pn = 0;
        int r = xbm_resolve(name, xbm_vfs_get_deps, 0, order, &pn);
        if (r == XBM_OK) {
            printf("%s: install order for '%s' (%d):", prog, name, pn);
            for (int i = 0; i < pn; i++) printf(" %s", order[i]);
            printf("\n");
        } else if (r == XBM_EMISSING) printf("%s: '%s' or one of its dependencies has no recipe\n", prog, name);
        else if (r == XBM_ECYCLE)     printf("%s: dependency cycle detected involving '%s'\n", prog, name);
        else                          printf("%s: dependency graph too large (> %d packages)\n", prog, XBM_MAXP);
        return;
    }

    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { printf("Usage: %s install <name>\n", prog); return; }
        const char* name = argv[2];
        if (!pkg_valid_name(name)) { printf("%s: invalid package name '%s'\n", prog, name); return; }

        // Resolve the dependency order (deps before dependents, target last), then build each in
        // turn — the apt/pacman "pull the whole tree" install. A package with no `deps:` resolves
        // to just itself, so this is a superset of the old single-package install.
        static char order[XBM_MAXP][XBM_NAMELEN];
        int pn = 0;
        int r = xbm_resolve(name, xbm_vfs_get_deps, 0, order, &pn);
        if (r == XBM_ECYCLE)   { printf("%s: dependency cycle involving '%s' - aborting\n", prog, name); return; }
        if (r == XBM_EMISSING) { printf("%s: '%s' or one of its dependencies has no recipe - aborting\n", prog, name); return; }
        if (r != XBM_OK)       { printf("%s: dependency graph too large - aborting\n", prog); return; }
        if (pn > 1) {
            printf("%s: %s pulls in %d package(s):", prog, name, pn);
            for (int i = 0; i < pn; i++) printf(" %s", order[i]);
            printf("\n");
        }
        for (int i = 0; i < pn; i++) {
            if (xbm_install_one(order[i], prog) != 0) {
                printf("%s: aborting - '%s' failed to install\n", prog, order[i]);
                return;
            }
        }
        return;
    }

    if (strcmp(argv[1], "verify") == 0) {
        // Re-hash the installed binary and compare against the manifest recorded at install time.
        if (argc < 3) { printf("Usage: %s verify <name>\n", prog); return; }
        const char* name = argv[2];
        if (!pkg_valid_name(name)) { printf("%s: invalid package name '%s'\n", prog, name); return; }

        char bin[64]; bin[0] = '\0';
        char rpath[128];
        snprintf(rpath, sizeof(rpath), "/usr/pkg/%s/recipe", name);
        int rfd = vfs_open(rpath, 0, 0);
        if (rfd >= 0) {
            char rbuf[512];
            int rn = vfs_read(rfd, rbuf, sizeof(rbuf) - 1);
            vfs_close(rfd);
            if (rn > 0) { rbuf[rn] = '\0'; pkg_recipe_value(rbuf, "bin", bin, sizeof(bin)); }
        }
        if (bin[0] == '\0') { strncpy(bin, name, sizeof(bin) - 1); bin[sizeof(bin) - 1] = '\0'; }
        if (!pkg_valid_name(bin)) { printf("%s: recipe for '%s' has an unsafe 'bin' name\n", prog, name); return; }

        char mpath[160];
        snprintf(mpath, sizeof(mpath), "/usr/pkg/%s/sha256", name);
        int mfd = vfs_open(mpath, 0, 0);
        if (mfd < 0) { printf("%s: no integrity manifest for '%s' (reinstall to record one)\n", prog, name); return; }
        char rec[80];
        int mn = vfs_read(mfd, rec, sizeof(rec) - 1);
        vfs_close(mfd);
        if (mn < 64) { printf("%s: corrupt manifest for '%s'\n", prog, name); return; }
        rec[64] = '\0';

        char outp[128];
        snprintf(outp, sizeof(outp), "/mnt/bin/%s", bin);
        char cur[65];
        int hr = pkg_sha256_file(outp, cur);
        if (hr == -1) { printf("%s: '%s' is not installed (%s missing)\n", prog, name, outp); return; }
        if (hr == -2) { printf("%s: '%s' is too large to verify\n", prog, name); return; }
        if (strncmp(rec, cur, 64) == 0) printf("%s: %s OK - sha256 matches (%s)\n", prog, name, outp);
        else printf("%s: %s MODIFIED - %s does not match the recorded sha256!\n", prog, name, outp);
        return;
    }

    if (strcmp(argv[1], "remove") == 0 || strcmp(argv[1], "uninstall") == 0) {
        if (argc < 3) { printf("Usage: %s remove <name>\n", prog); return; }
        const char* name = argv[2];
        if (!pkg_valid_name(name)) { printf("%s: invalid package name '%s'\n", prog, name); return; }

        // The installed binary's name comes from the recipe's `bin:` field; if the recipe
        // is missing, fall back to assuming the binary is named after the package.
        char bin[64];
        char rpath[128];
        snprintf(rpath, sizeof(rpath), "/usr/pkg/%s/recipe", name);
        int fd = vfs_open(rpath, 0, 0);
        bin[0] = '\0';
        if (fd >= 0) {
            char rbuf[512];
            int n = vfs_read(fd, rbuf, sizeof(rbuf) - 1);
            vfs_close(fd);
            if (n > 0) { rbuf[n] = '\0'; pkg_recipe_value(rbuf, "bin", bin, sizeof(bin)); }
        }
        if (bin[0] == '\0') { strncpy(bin, name, sizeof(bin) - 1); bin[sizeof(bin) - 1] = '\0'; }
        if (!pkg_valid_name(bin)) { printf("%s: recipe for '%s' has an unsafe 'bin' name\n", prog, name); return; }

        char outp[128];
        snprintf(outp, sizeof(outp), "/mnt/bin/%s", bin);
        int ofd = vfs_open(outp, 0, 0);            // is it actually installed?
        if (ofd < 0) { printf("%s: '%s' is not installed\n", prog, name); return; }
        vfs_close(ofd);
        if (vfs_unlink(outp) == 0) printf("%s: removed %s (%s)\n", prog, name, outp);
        else                       printf("%s: failed to remove %s (%s)\n", prog, name, outp);
        return;
    }

    printf("%s: unknown subcommand '%s' (expected install|remove|verify|deps|search|list)\n", prog, argv[1]);
}

// Run an ELF as a BACKGROUND job: spawn it and return immediately. It runs
// preemptively alongside the desktop; 'ps' lists it and it's reaped when it exits.
// `doom` — run DOOM inside a compositor window (v5.9.37). The heavy lifting is in the
// compositor (launch_doom_windowed opens the window + foreground-runs /doom.elf).
static void cmd_doom(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_doom_windowed();
}

// `pong` — open a Pong game window (in-kernel, compositor.c).
static void cmd_pong(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_pong();
}

// `voxel` — open the Nyx Voxels window (a static isometric voxel scene; in-kernel,
// compositor.c). First step toward the voxel-sandbox north star.
static void cmd_voxel(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_voxel();
}

// `fire` — open the Nyx Fire window (animated doom-fire effect, compositor.c).
static void cmd_fire(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_fire();
}

// `matrix` — open the Nyx Matrix window (green code-rain effect, compositor.c).
static void cmd_matrix(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_matrix();
}

// `lava` — open the Nyx Lava window (animated plasma effect, compositor.c).
static void cmd_lava(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_lava();
}

// `fractal` — open the Nyx Fractal window (Mandelbrot render-perf demo, compositor.c).
static void cmd_fractal(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_mandel();
}

// `julia` — open the Nyx Julia window (morphing Julia-set render-perf demo, compositor.c).
static void cmd_julia(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_julia();
}

// `particles` — open the Nyx Particles window (particle-fountain render-perf demo, compositor.c).
static void cmd_particles(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_particles();
}

// `rotor` — open the Nyx Rotor window (spinning 3D point-lattice render-perf demo, compositor.c).
static void cmd_rotor(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_rotor();
}

// `fill` — open the Nyx Fill window (2D fill-rate / overdraw render-perf demo, compositor.c).
static void cmd_fill(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_fill();
}

// `life` — open the Nyx Life window (Conway's Game of Life compute/render-perf demo, compositor.c).
static void cmd_life(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_life();
}

// `blobs` — open the Nyx Blobs window (metaballs render-perf demo, compositor.c).
static void cmd_blobs(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_blobs();
}

// ---- screenshot: capture the framebuffer to a binary PPM (P6) image file ----------------------
// Encode w*h XRGB (0xFFRRGGBB) pixels as a binary PPM (P6): "P6\n<w> <h>\n255\n" then 3 bytes
// (R,G,B) per pixel. Returns bytes written, or 0 if dst is too small. The output opens as a
// standard Netpbm image on any host (verified against file(1) + a parser in scratchpad/ppm_proto.c).
static uint32_t ppm_encode(const uint32_t* px, uint32_t w, uint32_t h, uint8_t* dst, uint32_t cap) {
    char hdr[32];
    int hn = snprintf(hdr, sizeof(hdr), "P6\n%u %u\n255\n", w, h);
    if (hn < 0) return 0;
    uint32_t need = (uint32_t)hn + w * h * 3;
    if (need > cap) return 0;
    for (int i = 0; i < hn; i++) dst[i] = (uint8_t)hdr[i];
    uint32_t o = (uint32_t)hn, n = w * h;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = px[i];
        dst[o++] = (p >> 16) & 0xFF;   // R
        dst[o++] = (p >> 8) & 0xFF;    // G
        dst[o++] = p & 0xFF;           // B
    }
    return o;
}

// Off-stack capture buffers, sized for the largest supported mode (1280x720).
#define SHOT_MAXW 1280u
#define SHOT_MAXH 720u
#define SHOT_RAW  (SHOT_MAXH * (1u + SHOT_MAXW * 3u))          // filtered scanlines / raw RGB bytes
static uint8_t shot_buf[SHOT_RAW + 64];                        // PPM output, or PNG raw scratch
static uint8_t png_out[SHOT_RAW + SHOT_RAW / 8 + 1024];        // PNG file (worst-case deflate slack)

static int str_ends_png(const char* s) {
    int n = 0; while (s[n]) n++;
    return n >= 4 && s[n-4] == '.' && (s[n-3]|0x20) == 'p' && (s[n-2]|0x20) == 'n' && (s[n-1]|0x20) == 'g';
}

// Capture the framebuffer the compositor last presented (fb_get_addr = the contiguous back buffer
// on the desktop) to `path`: a real PNG when the path ends in ".png" (compact, universally
// viewable), else a raw PPM. Returns bytes written (>0), -1 on write failure, -2 if the mode is
// larger than the capture buffers.
static int screenshot_save(const char* path) {
    uint32_t w = fb_get_width(), h = fb_get_height();
    const uint32_t* px = (const uint32_t*)fb_get_addr();
    if (!px || w == 0 || h == 0) return -1;
    int is_png = str_ends_png(path);
    uint32_t n = is_png ? png_encode(px, w, h, png_out, sizeof(png_out), shot_buf, sizeof(shot_buf))
                        : ppm_encode(px, w, h, shot_buf, sizeof(shot_buf));
    if (n == 0) return -2;
    if (vfs_write_file(path, is_png ? png_out : shot_buf, n) != (int)n) return -1;
    return (int)n;
}

// KAT: encode a fixed 2x2 image and require the exact PPM byte stream (validates header + the
// R/G/B channel order), plus that a too-small buffer returns 0.
int ppm_selftest(void) {
    static const uint32_t px[4] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF};
    uint8_t out[64];
    uint32_t n = ppm_encode(px, 2, 2, out, sizeof(out));
    static const uint8_t want[] = {
        'P','6','\n','2',' ','2','\n','2','5','5','\n',
        0xFF,0x00,0x00, 0x00,0xFF,0x00, 0x00,0x00,0xFF, 0xFF,0xFF,0xFF
    };
    if (n != (uint32_t)sizeof(want)) return 1;
    for (uint32_t i = 0; i < n; i++) if (out[i] != want[i]) return 2;
    if (ppm_encode(px, 2, 2, out, 10) != 0) return 3;   // too-small cap -> 0
    return 0;
}

// `screenshot [path]` — save a snapshot of the screen (default /tmp/screenshot.png; a real PNG
// when the path ends .png, else a raw PPM).
static void cmd_screenshot(int argc, char** argv) {
    const char* path = (argc >= 2) ? argv[1] : "/tmp/screenshot.png";
    int r = screenshot_save(path);
    if (r > 0) printf("screenshot: saved %ux%u -> %s (%d bytes)\n", fb_get_width(), fb_get_height(), path, r);
    else if (r == -2) printf("screenshot: resolution too large to capture\n");
    else printf("screenshot: cannot write '%s'\n", path);
}

// ---- system clipboard: an in-kernel text clipboard (pbcopy/pbpaste-style) --------------------
// A single shared text buffer. `clip <text>` copies, `clip` (no args) pastes, `clip -c` clears.
// The clipboard_* API is exposed (kernel.h) so the GUI terminal/editor can later wire Ctrl+C/V to
// the same buffer. Content is opaque bytes; the +1 slot lets paste NUL-terminate for printf.
#define CLIP_MAX 4096
static char clip_buf[CLIP_MAX + 1];
static uint32_t clip_len_v;

void clipboard_set(const char* data, uint32_t len) {
    if (len > CLIP_MAX) len = CLIP_MAX;
    for (uint32_t i = 0; i < len; i++) clip_buf[i] = data[i];
    clip_len_v = len;
}
uint32_t clipboard_get(char* out, uint32_t cap) {
    uint32_t n = clip_len_v; if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; i++) out[i] = clip_buf[i];
    return n;
}
uint32_t clipboard_len(void) { return clip_len_v; }
void clipboard_clear(void) { clip_len_v = 0; }

// KAT: set/get round-trip, overwrite, truncation at CLIP_MAX, capped get, clear.
int clipboard_selftest(void) {
    char out[64];
    clipboard_set("hello", 5);
    if (clipboard_len() != 5) return 1;
    if (clipboard_get(out, sizeof(out)) != 5 ||
        out[0] != 'h' || out[1] != 'e' || out[2] != 'l' || out[3] != 'l' || out[4] != 'o') return 2;
    clipboard_set("xy", 2);                                      // overwrite shrinks
    if (clipboard_len() != 2 || clipboard_get(out, sizeof(out)) != 2 || out[0] != 'x' || out[1] != 'y') return 3;
    static char big[CLIP_MAX + 100];
    for (int i = 0; i < CLIP_MAX + 100; i++) big[i] = 'A';
    clipboard_set(big, CLIP_MAX + 100);                          // over-long input truncates
    if (clipboard_len() != CLIP_MAX) return 4;
    if (clipboard_get(out, 3) != 3) return 5;                    // small cap limits the copy
    clipboard_clear();
    if (clipboard_len() != 0) return 6;
    return 0;
}

// `clip [text...] | -c` — copy args to the clipboard, paste it (no args), or clear it (-c).
static void cmd_clip(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "-c") == 0) { clipboard_clear(); printf("clip: cleared\n"); return; }
    if (argc < 2) {                                              // paste
        uint32_t n = clipboard_len();
        if (n == 0) { printf("clip: (empty)\n"); return; }
        clip_buf[n] = '\0';
        printf("%s\n", clip_buf);
        return;
    }
    uint32_t o = 0;                                              // copy: join argv[1..] with spaces
    for (int a = 1; a < argc && o < CLIP_MAX; a++) {
        if (a > 1 && o < CLIP_MAX) clip_buf[o++] = ' ';
        for (const char* p = argv[a]; *p && o < CLIP_MAX; p++) clip_buf[o++] = *p;
    }
    clip_len_v = o;
    printf("clip: copied %u byte%s\n", o, o == 1 ? "" : "s");
}

// `notify <title> [body...]` — pop a desktop toast (body joined from argv[2..]).
static void cmd_notify(int argc, char** argv) {
    if (argc < 2) { printf("Usage: notify <title> [message...]\n"); return; }
    char body[96];
    int o = 0;
    for (int a = 2; a < argc && o < (int)sizeof(body) - 1; a++) {
        if (a > 2 && o < (int)sizeof(body) - 1) body[o++] = ' ';
        for (const char* p = argv[a]; *p && o < (int)sizeof(body) - 1; p++) body[o++] = *p;
    }
    body[o] = '\0';
    notify_push(argv[1], body);
    printf("notify: posted '%s'\n", argv[1]);
}

// `stackcheck` — sweep every task's kernel-stack overflow canary (process.c) and report.
static void cmd_stackcheck(int argc, char** argv) {
    (void)argc; (void)argv;
    int smashed = stack_canary_sweep();
    if (smashed == 0) printf("stackcheck: all task kernel-stack canaries intact\n");
    else              printf("stackcheck: %d task kernel-stack(s) SMASHED\n", smashed);
}

// `snake` — open a Snake game window (in-kernel, compositor.c).
static void cmd_snake(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_snake();
}

// `tetris` — open a Tetris game window (in-kernel, compositor.c).
static void cmd_tetris(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_tetris();
}

// `selene` — open the Selene web browser window (in-kernel, compositor.c).
static void cmd_selene(int argc, char** argv) {
    (void)argc; (void)argv;
    launch_selene();
}

static void cmd_imageview(int argc, char** argv) {
    launch_imageview(argc >= 2 ? argv[1] : NULL);
    if (argc >= 2)
        printf("[imageview] opening %s\n", argv[1]);
    else
        printf("[imageview] opened (test pattern; pass a path, e.g. imageview /mnt/pic.gif)\n");
}

static void cmd_files(int argc, char** argv) {
    launch_fileman(argc >= 2 ? argv[1] : NULL);
    if (argc >= 2)
        printf("[files] File Manager opened at %s\n", argv[1]);
    else
        printf("[files] File Manager opened\n");
}

static void cmd_spawn(int argc, char** argv) {
    if (argc < 2) { printf("Usage: spawn <file> [args...]\n"); return; }
    int pid = spawn_user_path_args(argv[1], &argv[1], argc - 1);   // forward argv
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
int spawn_user_path_args(const char* path, char* const* argv, int argc) {
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
    int r = elf_load_args(copy, size, argv, argc, &proc);   // seeds argv on the stack
    kfree(copy);
    if (r != 0 || !proc) return -5;
    proc_set_comm(proc, path); // name it after the file (elf_load hardcodes "elf")
    // cmdline = space-joined argv (what ps / /proc show); fall back to the path.
    if (argc > 0 && argv) {
        int c = 0;
        for (int i = 0; i < argc && c < (int)sizeof(proc->cmdline) - 1; i++) {
            if (i) proc->cmdline[c++] = ' ';
            for (const char* s = argv[i]; *s && c < (int)sizeof(proc->cmdline) - 1; s++)
                proc->cmdline[c++] = *s;
        }
        proc->cmdline[c] = '\0';
    } else {
        strncpy(proc->cmdline, path, sizeof(proc->cmdline) - 1);
        proc->cmdline[sizeof(proc->cmdline) - 1] = '\0';
    }
    proc->sched_managed = 1;   // scheduler now round-robins this ring-3 process
    extern process_t* get_current_process(void);
    process_t* parent = get_current_process();
    proc->ppid = parent ? parent->pid : 0;   // so kwait() can find/wake the parent
    sched_enable();
    return (int)proc->pid;
}

int spawn_user_path(const char* path) {
    return spawn_user_path_args(path, (char* const*)0, 0);
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

extern int ipv4_parse(const char* s, uint32_t* out);   // strict dotted-quad (kernel/net/ipaddr.c)

static void cmd_setip(int argc, char** argv) {
    if (argc < 2) { printf("Usage: setip <ip> [mask] [gw]\n"); return; }
    // Numeric config: validate strictly so a typo (e.g. `setip 300.1.1.1`) is rejected
    // rather than silently masked to a wrong address like the lenient parse_ip() would.
    uint32_t ip, mask = 0x00FFFFFF, gw = 0;
    if (ipv4_parse(argv[1], &ip) != 0) { printf("setip: invalid IP address: %s\n", argv[1]); return; }
    if (argc >= 3 && ipv4_parse(argv[2], &mask) != 0) { printf("setip: invalid netmask: %s\n", argv[2]); return; }
    if (argc >= 4 && ipv4_parse(argv[3], &gw)   != 0) { printf("setip: invalid gateway: %s\n", argv[3]); return; }
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            net_interfaces[i].ip = ip;
            net_interfaces[i].netmask = mask;
            net_interfaces[i].gateway = gw;
            printf("Set %s IP to %d.%d.%d.%d\n", net_interfaces[i].name,
                IP4_OCTETS(ip));
            return;
        }
    }
    printf("No interface found\n");
}

static void cmd_mount(int argc, char** argv) {
    (void)argc; (void)argv;
    uint8_t dev = (argc >= 2) ? parse_blk_dev(argv[1]) : 0;
    uint32_t part_lba = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 0;

    if (ext2_fs.sb.magic == EXT2_SUPER_MAGIC) {
        printf("EXT2 already mounted.\n");
        return;
    }

    if (dev == BLK_NVME0) { if (blk_dev_sectors(dev) == 0) { printf("mount: NVMe not available\n"); return; } }
    else ata_init();
    if (ext2_mount(dev, part_lba) < 0) {
        printf("EXT2: no EXT2 filesystem found at LBA %u\n", part_lba);
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

// df — disk usage of the mounted persistent (ext2) filesystem, from its superblock.
static void cmd_df(int argc, char** argv) {
    (void)argc; (void)argv;
    if (ext2_fs.sb.magic != EXT2_SUPER_MAGIC) {
        printf("df: no persistent filesystem mounted.\n");
        printf("    Attach an ext2 disk (`make -C kernel disk`, then qemu ... -hda ext2-test.img).\n");
        return;
    }
    uint32_t bs_kb = ext2_fs.block_size / 1024;
    if (bs_kb == 0) bs_kb = 1;
    uint32_t total = ext2_fs.sb.total_blocks;
    uint32_t freeb = ext2_fs.sb.free_blocks;
    uint32_t used  = (total >= freeb) ? total - freeb : 0;
    uint32_t pct   = total ? (used * 100u) / total : 0;
    printf("Filesystem   1K-blocks      Used  Available  Use%%  Mounted on\n");
    printf("/dev/hda     %9u  %8u  %9u  %3u%%  /mnt\n",
           total * bs_kb, used * bs_kb, freeb * bs_kb, pct);
    printf("Inodes: %u total, %u free\n",
           ext2_fs.sb.total_inodes, ext2_fs.sb.free_inodes);
    uint64_t chits = 0, cmisses = 0;
    ext2_cache_stats(&chits, &cmisses);
    uint64_t ctot = chits + cmisses;
    uint32_t hitpct = ctot ? (uint32_t)((chits * 100u) / ctot) : 0;
    printf("Block cache: %u hits, %u misses (%u%% hit rate)\n",
           (uint32_t)chits, (uint32_t)cmisses, hitpct);
}

// strict, range-checked unsigned parser (kernel/core/numparse.c) — used for ports so a
// typo or an out-of-range value is rejected instead of silently truncated to a uint16.
extern int parse_u32_max(const char* s, uint32_t max, uint32_t* out);

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
        uint32_t pval;
        if (parse_u32_max(argv[2], 65535, &pval) != 0) {
            printf("tcptest: invalid port '%s' (expected 0..65535)\n", argv[2]);
            return;
        }
        dst_port = (uint16_t)pval;
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
           IP4_OCTETS(dst_ip), dst_port);
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
                for (int i = 0; i < hn; i++) printf(" %02x", buf[i]);
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
    uint16_t port = 80;
    if (argc >= 2) {
        uint32_t pval;
        if (parse_u32_max(argv[1], 65535, &pval) != 0) {
            printf("tcpserve: invalid port '%s' (expected 0..65535)\n", argv[1]);
            return;
        }
        port = (uint16_t)pval;
    }
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

// `tls <host>` — start a TLS handshake with host:443 and report what the server sends
// back (ServerHello / Certificate / …). Step 1 of the https arc; no page fetch yet.
static void cmd_tls(int argc, char** argv) {
    if (argc < 2) { printf("Usage: tls <host>   (e.g. tls example.com)\n"); return; }
    const char* host = argv[1];
    if (strncmp(host, "https://", 8) == 0) host += 8;
    else if (strncmp(host, "http://", 7) == 0) host += 7;
    int iface_idx = -1;
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) { iface_idx = i; break; }
    }
    tls_hello(host, iface_idx);
}

// `posttest` — live POST self-test over TLS: POST a small form body to https://httpbin.org and
// confirm the server echoes it back. Exercises P-256 ECDHE (httpbin negotiates secp256r1) AND the
// TLS POST request/body path end to end.
static void cmd_posttest(int argc, char** argv) {
    (void)argc; (void)argv;
    int iface = -1;
    for (int i = 0; i < 8; i++)
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) { iface = i; break; }
    if (iface < 0) { printf("posttest: no network interface (boot with -nic)\n"); return; }
    const char* body = "nyxos=works&user=alice&msg=hello+world";
    uint8_t* buf = (uint8_t*)kmalloc(64 * 1024);
    if (!buf) { printf("posttest: out of memory\n"); return; }
    printf("posttest: POST https://httpbin.org/post  body=\"%s\"\n", body);
    int n = tls_https_request("httpbin.org", "/post", "POST", (const uint8_t*)body,
                              (uint32_t)strlen(body), iface, buf, 64 * 1024 - 1, 1);
    if (n < 0) { printf("posttest: request failed (retry — the first DNS lookup after boot can miss)\n"); kfree(buf); return; }
    buf[n] = '\0';
    http_response_t resp; int echoed = 0;
    if (http_parse_response(buf, (uint32_t)n, &resp) == 0 && resp.body) {
        if (strstr((char*)resp.body, "nyxos") && strstr((char*)resp.body, "works") &&
            strstr((char*)resp.body, "hello world")) echoed = 1;    // httpbin echoes the posted form
        printf("posttest: HTTPS %d %s, %u-byte reply\n", resp.status_code, resp.status_text, resp.body_len);
        http_free(&resp);
    }
    printf("posttest: %s — httpbin %s the posted form fields (over TLS)\n",
           echoed ? "PASS" : "CHECK", echoed ? "echoed back" : "did not clearly echo");
    kfree(buf);
}

// `tlsstrict [on|off]` — toggle strict TLS certificate enforcement. When on, a handshake is
// refused unless the chain anchors to a pinned root, the hostname matches, and the cert is in date.
static void cmd_tlsstrict(int argc, char** argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "on") == 0)       tls_set_strict(1);
        else if (strcmp(argv[1], "off") == 0) tls_set_strict(0);
        else { printf("Usage: tlsstrict [on|off]\n"); return; }
    }
    printf("TLS strict certificate enforcement: %s\n",
           tls_get_strict() ? "ON (untrusted certificates are refused)" : "OFF (report only)");
}

// `x25519test` — run the RFC 7748 known-answer vectors for X25519 (Curve25519 ECDH),
// the ephemeral key exchange TLS negotiated. Pure computation; no network needed.
static void cmd_x25519test(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running X25519 (Curve25519) known-answer tests (RFC 7748)...\n");
    curve25519_selftest();
}

// `prftest` — run the HMAC-SHA256 (RFC 4231) and TLS 1.2 PRF (P_SHA256) known-answer
// vectors. This is the key-derivation function TLS uses to turn the ECDHE secret into
// the master secret and the AES keys. Pure computation; no network needed.
static void cmd_prftest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running TLS 1.2 PRF known-answer tests (RFC 4231 HMAC + P_SHA256)...\n");
    tls_prf_selftest();
}

// `tlskeytest` — run the TLS 1.2 key-schedule known-answer test: derive the master secret
// and the AES-128-GCM key block from a fixed pre-master secret + randoms, exactly as the
// live handshake does. Pure computation; no network needed.
static void cmd_tlskeytest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running TLS 1.2 key-schedule known-answer test (master secret + key block)...\n");
    tls_keyschedule_selftest();
}

// `gcmtest` — run the AES-128-GCM known-answer tests (FIPS-197 block + NIST/McGrew-Viega
// GCM vectors + a tamper check). This is the AEAD that protects every TLS record. Pure
// computation; no network needed.
static void cmd_gcmtest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running AES-128-GCM known-answer tests (FIPS-197 + NIST GCM)...\n");
    aes_gcm_selftest();
}

// `tlsrectest` — run the TLS 1.2 record-layer known-answer test: seal/open an AES-128-GCM
// record with the RFC 5288 nonce/AAD framing, and derive a Finished verify_data from a
// transcript hash. The plumbing the live handshake's Finished exchange rides on.
static void cmd_tlsrectest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running TLS 1.2 record + Finished known-answer test...\n");
    tls_record_selftest();
}

// `csprngtest` — self-test the CSPRNG (HMAC_DRBG-SHA256) against a NIST vector and report
// which hardware entropy sources the CPU exposes. This is what now generates the TLS
// ephemeral key and ClientHello random. Pure computation; no network needed.
static void cmd_csprngtest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running CSPRNG self-test (HMAC_DRBG-SHA256, NIST known-answer)...\n");
    csprng_selftest();
}

// `dertest` — self-test the DER/ASN.1 reader, including extracting the EC public key from a
// real X.509 certificate. This is the first step of TLS certificate verification. Pure
// computation; no network needed.
static void cmd_dertest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running DER/ASN.1 reader self-test (X.509 public-key extraction)...\n");
    der_selftest();
}

// `p256test` — self-test the NIST P-256 curve arithmetic against known base-point multiples.
// This is the curve the server certificate signs with (ECDSA-P256). Pure computation.
static void cmd_p256test(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running NIST P-256 arithmetic self-test (base-point multiples)...\n");
    p256_selftest();
}

// `p384test` — self-test the NIST P-384 curve arithmetic against known base-point multiples
// and an ECDSA-P384/SHA-384 signature. This is the curve the certificate chain's CA links sign
// with (ECDSA-P384). Pure computation.
static void cmd_p384test(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running NIST P-384 arithmetic self-test (base-point multiples + ECDSA)...\n");
    p384_selftest();
}

// `skp384test` — self-test ECDSA-P384/SHA-384 ServerKeyExchange verification: extract a P-384 key
// from a real leaf certificate and check a known signature (valid accepted, tampered rejected).
static void cmd_skp384test(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running ECDSA-P384 ServerKeyExchange verification self-test (leaf key + known signature)...\n");
    tls_ske_p384_selftest();
}

// `deflatetest` — self-test DEFLATE (RFC 1951) + zlib decompression, the core PNG's IDAT needs.
static void cmd_deflatetest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running DEFLATE/zlib decompression self-test (fixed/dynamic/stored + Adler-32)...\n");
    inflate_selftest();
}

// `pngtest` — self-test the PNG decoder: RGB / RGBA / palette / grayscale + all 5 scanline filters.
static void cmd_pngtest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running PNG decoder self-test (color types 0/2/3/6 + filters, vs expected RGBA)...\n");
    png_selftest();
}

// `bmptest` — self-test the BMP decoder: 24-bit / 32-bit / 8-bit palette / top-down, vs expected RGBA.
static void cmd_bmptest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running BMP decoder self-test (24/32/8-bit + top-down, vs expected RGBA)...\n");
    bmp_selftest();
}

// `giftest` — self-test the GIF decoder: LZW + interlacing + transparency, vs expected RGBA.
static void cmd_giftest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running GIF decoder self-test (LZW, interlace, transparency, vs expected RGBA)...\n");
    gif_selftest();
}

// `jpegtest` — self-test the baseline JPEG decoder: Huffman + IDCT + YCbCr, vs Pillow within tolerance.
static void cmd_jpegtest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running JPEG decoder self-test (grayscale + 4:4:4 + 4:2:0, vs reference within tolerance)...\n");
    jpeg_selftest();
}

static void cmd_imgreject(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running image-decoder REJECT self-test (png/bmp/gif/jpeg must refuse hostile input)...\n");
    image_reject_selftest();
}

static void cmd_httptest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running HTTP-parser robustness self-test (untrusted responses: chunked, hostile sizes, truncated)...\n");
    int f = http_parse_selftest();
    printf("httptest: %s\n", f == 0 ? "PASS" : "FAIL");
}

static void cmd_ext2test(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running ext2 directory-scanner self-test (untrusted dir blocks: bad rec_len/name_len)...\n");
    int f = ext2_dir_selftest();
    printf("ext2test: %s\n", f == 0 ? "PASS" : "FAIL");
}

// `rsatest` — self-test RSA PKCS#1 v1.5 SHA-256 signature verification, needed to check the
// RSA CA signatures in a real certificate chain. Pure computation; no network needed.
static void cmd_rsatest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running RSA PKCS#1 v1.5 SHA-256 signature-verify self-test...\n");
    rsa_selftest();
}

// `sha512test` — self-test SHA-512 and SHA-384, needed for the SHA-384 signatures in real
// certificate chains. Pure computation; no network needed.
static void cmd_sha512test(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running SHA-512 / SHA-384 self-test (FIPS 180-4)...\n");
    sha512_selftest();
}

// `chaintest` — self-test X.509 certificate-chain verification: a real captured chain must be
// accepted and anchored to the pinned root, a tampered copy must be rejected as forged, and a
// root-less prefix must be reported as not-anchored. This is the TLS trust model's final check.
static void cmd_chaintest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running X.509 certificate-chain verification self-test (pinned trust anchor)...\n");
    x509_selftest();
}

// `formtest` — self-test Selene's HTML-forms support: parse a known <form> + its <input>s and
// build the GET submission URL (URL-encoding + hidden fields), all without a window or network.
static void cmd_formtest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Running Selene HTML-forms self-test (parse + GET submission URL)...\n");
    selene_form_selftest();
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

static int hx_all_digits(const char* s) {
    if (!*s) return 0;
    while (*s) { if (*s < '0' || *s > '9') return 0; s++; }
    return 1;
}

// bash-style history expansion of a WHOLE-line reference into `out` (verbatim copy for
// anything else, so a normal command is untouched):
//   !!   -> the most recent command
//   !N   -> command N as numbered by `history` (1-based within the retained window)
//   !-N  -> the Nth command counting back from the most recent (!-1 == !!)
// Returns 1 if a reference was expanded, else 0. `hist` is the ring buffer, `count` the
// monotonic entry count (live index = i % HIST_MAX). Pure given (hist,count) — KAT'd.
static int history_expand(const char* line, char hist[][256], int count, char* out, int outsz) {
    int target = -1;
    if (line[0] == '!' && count > 0) {
        int start = count > HIST_MAX ? count - HIST_MAX : 0;
        if (line[1] == '!' && line[2] == '\0') {                 // !!
            target = count - 1;
        } else if (line[1] == '-' && hx_all_digits(line + 2)) {  // !-N
            int k = atoi(line + 2);
            if (k >= 1) target = count - k;
        } else if (hx_all_digits(line + 1)) {                    // !N
            int k = atoi(line + 1);
            if (k >= 1) target = start + (k - 1);
        }
        if (target < start || target >= count) target = -1;      // out of the retained window
    }
    const char* src = (target >= 0) ? hist[target % HIST_MAX] : line;
    int i = 0;
    for (; src[i] && i < outsz - 1; i++) out[i] = src[i];
    out[i] = '\0';
    return target >= 0 ? 1 : 0;
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
    printf("CPU  ROLE  APIC  SELF  STATE    CLOCK  TICKS\n");
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        // APIC = id the BSP assigned; SELF = id the core read for itself.
        // TICKS is the proof of life: each AP counts its OWN LAPIC timer, so the
        // columns must climb independently across two runs of this command.
        printf("%-3u  %-4s  %-4u  %-4u  %-7s  %-5s  %lu\n",
               i, i == 0 ? "BSP" : "AP",
               cpu_info[i].apic_id, cpu_info[i].apic_id_self,
               cpu_info[i].running ? "online" : "offline",
               i == 0 ? "PIT" : "LAPIC",
               i == 0 ? (uint64_t)tick_count : cpu_info[i].ticks);
    }
    if (cpu_count > 1)
        printf("APs run a kernel idle loop only (no user processes yet).\n");
}

// Prove the allocator spinlocks under REAL cross-CPU contention. Every online
// core runs the same alloc/verify/free loop at once — the APs from their idle
// loop, this thread for the BSP — which is the only way locks that are never
// contended can be said to work at all.
//
// Two independent checks: each core verifies the memory it was handed still
// holds what it wrote (nobody else got the same frame), and the machine-wide
// free-page count must come back to where it started (every operation is paired,
// so a torn bitmap shows as drift even if no single operation looked wrong).
static void cmd_smpstress(int argc, char** argv) {
    uint32_t secs = (argc >= 2) ? (uint32_t)atoi(argv[1]) : 3;
    if (secs == 0 || secs > 30) secs = 3;

    for (uint32_t i = 0; i < MAX_CPUS; i++) {
        cpu_info[i].stress_ops = 0;
        cpu_info[i].stress_bad = 0;
    }

    uint32_t free_before = get_free_pages();
    printf("smpstress: %u CPU(s) hammering alloc_page/kmalloc for %us...\n", cpu_count, secs);

    smp_stress_active = 1;
    uint32_t deadline = tick_count + secs * 1000;      // PIT runs at 1000 Hz
    while ((int32_t)(tick_count - deadline) < 0) smp_stress_iteration(&cpu_info[0]);
    smp_stress_active = 0;
    sleep(200);                                        // let each AP finish its iteration

    uint32_t free_after = get_free_pages();
    uint64_t total_ops = 0, total_bad = 0;
    printf("CPU  ROLE  OPS         BAD\n");
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        printf("%-3u  %-4s  %-10lu  %lu\n", i, i == 0 ? "BSP" : "AP",
               cpu_info[i].stress_ops, cpu_info[i].stress_bad);
        total_ops += cpu_info[i].stress_ops;
        total_bad += cpu_info[i].stress_bad;
    }
    printf("smpstress: %lu ops, %lu integrity failures\n", total_ops, total_bad);
    // Drift means frames went missing or were double-freed. One expected
    // exception: a COLD slab cache keeps the first page it grows into (slab pages
    // are never returned to the page allocator), so the first run after boot is
    // legitimately one page short. Run it twice — the second must balance exactly.
    printf("smpstress: free pages %u -> %u (delta %d) -> %s\n",
           free_before, free_after, (int)free_after - (int)free_before,
           free_before == free_after ? "BALANCED OK" : "check (cold slab keeps its first page)");
}

// Prove the per-CPU scheduler: each AP has a kernel thread pinned to it, and
// this wakes them all for a few seconds while the BSP just sleeps.
//
// The counters are the proof, and they cannot be faked: a worker counts into
// cpu_self()->work_ops, i.e. the core that ACTUALLY executed it. If pinning were
// broken and the BSP picked up a worker, the work would land in the BSP's row —
// so "every AP row climbs and the BSP's stays at zero" is a real statement about
// which core ran which thread, not about which core was supposed to.
static void cmd_smpthreads(int argc, char** argv) {
    uint32_t secs = (argc >= 2) ? (uint32_t)atoi(argv[1]) : 3;
    if (secs == 0 || secs > 30) secs = 3;
    if (cpu_count < 2) {
        printf("smpthreads: single CPU — no APs to schedule on (boot with -smp N)\n");
        return;
    }

    for (uint32_t i = 0; i < MAX_CPUS; i++) cpu_info[i].work_ops = 0;
    printf("smpthreads: waking %u pinned kernel thread(s) for %us "
           "(the BSP only sleeps)...\n", cpu_count - 1, secs);

    smp_work_active = 1;
    sleep(secs * 1000);          // the BSP blocks here — anything counted is an AP
    smp_work_active = 0;
    sleep(100);

    printf("CPU  ROLE  THREAD      WORK\n");
    int aps_working = 0;
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        process_t* c = (process_t*)cpu_info[i].sched_cur;
        printf("%-3u  %-4s  %-10s  %lu\n", i, i == 0 ? "BSP" : "AP",
               c ? c->comm : "(idle)", cpu_info[i].work_ops);
        if (i > 0 && cpu_info[i].work_ops > 0) aps_working++;
    }
    printf("smpthreads: %d of %u AP(s) executed their thread; BSP counted %lu -> %s\n",
           aps_working, cpu_count - 1, cpu_info[0].work_ops,
           (aps_working == (int)(cpu_count - 1) && cpu_info[0].work_ops == 0)
               ? "PARALLEL OK" : "check");
}

// Stage 3b: run RING-3 processes on the application processors.
//
// Spawns n copies of /spin.elf with balancing on, so create_user_process hands
// them out round-robin, then reports where each one actually is. The proof that
// matters is `cpus`-style: an AP whose sched_cur is a process with a page
// directory is a core that took a ring3 -> ring0 transition through its OWN TSS
// and came back — none of which was possible before this stage.
static void cmd_smpuser(int argc, char** argv) {
    if (cpu_count < 2) {
        printf("smpuser: single CPU — nothing to spread (boot with -smp N)\n");
        return;
    }
    int n = (argc >= 2) ? atoi(argv[1]) : 4;
    if (n < 1 || n > 16) n = 4;

    smp_user_balance = 1;
    printf("smpuser: spawning %d user process(es) with balancing on...\n", n);
    int pids[16];
    for (int i = 0; i < n; i++) pids[i] = spawn_user_path("/spin.elf");
    sleep(2000);                    // let every core pick its process up

    printf("PID   CPU  STATE\n");
    int on_ap = 0;
    for (int i = 0; i < n; i++) {
        if (pids[i] <= 0) { printf("%-5d  --   spawn failed\n", pids[i]); continue; }
        process_t* p = find_process((uint64_t)pids[i]);
        if (!p) { printf("%-5d  --   gone\n", pids[i]); continue; }
        printf("%-5d %-4d %s\n", pids[i], p->sched_cpu,
               p->state == PROC_RUN ? "running" : "not running");
        if (p->sched_cpu > 0) on_ap++;
    }

    // What each core is actually executing right now.
    printf("CPU  ROLE  RUNNING\n");
    int aps_on_user = 0;
    for (uint32_t i = 0; i < cpu_count && i < MAX_CPUS; i++) {
        process_t* c = (process_t*)cpu_info[i].sched_cur;
        int is_user = (i > 0) && c && c->page_directory;
        printf("%-3u  %-4s  %s%s\n", i, i == 0 ? "BSP" : "AP",
               c ? c->comm : "(idle)", is_user ? "   <- ring 3 on an AP" : "");
        if (is_user) aps_on_user++;
    }
    for (int i = 0; i < n; i++) if (pids[i] > 0) do_kill(pids[i], SIGKILL);
    smp_user_balance = 0;
    printf("smpuser: %d/%d placed on APs, %d AP(s) executing ring 3 -> %s\n",
           on_ap, n, aps_on_user, aps_on_user > 0 ? "USERSPACE ON APs OK" : "check");
}

// Prove cross-CPU TLB shootdown, with a test built to be able to FAIL.
//
// A shootdown test that only ever checks the good path proves nothing: if the
// machine never cached stale translations to begin with, it would pass either
// way. So this runs the remap TWICE — once WITHOUT a shootdown, where CPU 1
// must still read the OLD value out of its own TLB, and once WITH, where it
// must read the new one. The first read is what gives the second its meaning.
static int tlb_wait_ack(int want) {
    for (int spin = 0; spin < 600 && tlb_test_ack < want; spin++) sleep(5);
    return tlb_test_ack >= want;
}

static void cmd_tlbtest(int argc, char** argv) {
    (void)argc; (void)argv;
    if (cpu_count < 2) { printf("tlbtest: needs more than one CPU (boot with -smp N)\n"); return; }

    // Allocated once and kept: the VA below stays mapped at these frames, so
    // re-running the command must not hand them back to the allocator.
    static void* pa = NULL; static void* pb = NULL;
    if (!pa) { pa = alloc_page(); pb = alloc_page(); }
    if (!pa || !pb) { printf("tlbtest: out of memory\n"); return; }
    *(volatile uint64_t*)pa = 0xAAAAAAAAAAAAAAAAULL;
    *(volatile uint64_t*)pb = 0xBBBBBBBBBBBBBBBBULL;

    const uint64_t va = 0xFFFFB00000000000ULL;   // an otherwise unused kernel slot
    uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_NX;

    map_page(pa, (void*)va, flags);
    tlb_shootdown(va);                            // start from a known-clean state

    tlb_test_v1 = tlb_test_v2 = tlb_test_v3 = 0;
    tlb_test_ack = 0;
    tlb_test_va  = va;
    tlb_test_req = 1;                             // CPU 1: cli, read, then hold
    if (!tlb_wait_ack(1)) { printf("tlbtest: CPU 1 never answered\n"); return; }

    // Remap to B and invalidate ONLY locally. CPU 1 is sitting with interrupts
    // off, so nothing has cleared its cached translation.
    map_page(pb, (void*)va, flags);
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
    tlb_test_req = 2;                             // CPU 1: read again, still blind
    if (!tlb_wait_ack(2)) { printf("tlbtest: CPU 1 stalled at phase 2\n"); return; }

    // Now tell it properly. CPU 1 has re-enabled interrupts and can hear us.
    uint64_t before = tlb_shootdowns;
    tlb_shootdown(va);
    tlb_test_req = 3;
    if (!tlb_wait_ack(3)) { printf("tlbtest: CPU 1 stalled at phase 3\n"); return; }

    uint64_t r0 = tlb_test_v1, r1 = tlb_test_v2, r2 = tlb_test_v3;
    printf("tlbtest: CPU1 baseline      = 0x%lx  (expect 0xAAAA...)\n", r0);
    printf("tlbtest: CPU1 after remap   = 0x%lx  (no shootdown yet, IRQs off)\n", r1);
    printf("tlbtest: CPU1 after IPI     = 0x%lx  (expect 0xBBBB...)\n", r2);
    printf("tlbtest: shootdowns sent    = %lu\n", tlb_shootdowns - before);

    int baseline_ok = (r0 == 0xAAAAAAAAAAAAAAAAULL);
    int fresh_ok    = (r2 == 0xBBBBBBBBBBBBBBBBULL);
    int was_stale   = (r1 == 0xAAAAAAAAAAAAAAAAULL);
    // Reported separately and honestly: without a stale read in the middle the
    // machine never needed the shootdown, so the pass says nothing about it.
    printf("tlbtest: stale-without-IPI  = %s\n",
           was_stale ? "YES (so the test could have failed)"
                     : "NO — this core saw the remap unaided, result is INCONCLUSIVE");
    printf("tlbtest: %s\n",
           (baseline_ok && fresh_ok)
               ? (was_stale ? "SHOOTDOWN PROVEN" : "shootdown path works, but unproven")
               : "FAILED");
}

// Opt-in multi-core placement for both processes and threads. Threads only
// became safe to spread once unmap, mprotect and the COW remap all shoot down
// the other cores — before that a sibling on another core could keep using a
// translation this one had already replaced.
static void cmd_smpbalance(int argc, char** argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "on") == 0)       smp_user_balance = 1;
        else if (strcmp(argv[1], "off") == 0) smp_user_balance = 0;
    }
    printf("smpbalance: %s (%u CPU(s) online, %lu shootdowns so far)\n",
           smp_user_balance ? "on — new processes spread over all CPUs"
                            : "off — everything stays on CPU 0",
           cpu_count, tlb_shootdowns);
    if (smp_user_balance)
        printf("smpbalance: WARNING — fork/exec-heavy workloads (e.g. pstorm) wedge with\n"
               "            this on. Long-lived processes are fine; churn is not, and the\n"
               "            cause is not the page tables (TLB shootdown is live). Threads\n"
               "            of one group stay on one core regardless.\n");
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
    rtc_time_t t;
    rtc_read_time(&t);
    // date +FORMAT — strftime-style output (e.g. `date +%A` or `date +%Y-%m-%d`).
    if (argc >= 2 && argv[1][0] == '+') {
        char buf[256];
        date_format(buf, sizeof(buf), argv[1] + 1, &t);
        printf("%s\n", buf);
        return;
    }
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
    int show_all = 0, long_fmt = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'a') show_all = 1;
            else if (*f == 'l') long_fmt = 1;
            else { printf("ls: invalid option -%c\n", *f); return; }
        }
    const char* path = (ai < argc) ? argv[ai] : NULL;
    // No flags: keep the classic colored lister (output byte-identical to before).
    if (!show_all && !long_fmt) { vfs_list_dir(path); return; }
    // -a / -l: iterate the directory ourselves via the backend-agnostic readdir.
    // -a shows dot-entries (hidden by default); -l prints a type char + byte size.
    const char* dpath = path ? path : vfs_getcwd();
    int fd = vfs_open(dpath, 0, 0);
    if (fd < 0) { printf("ls: %s: No such directory\n", dpath); return; }
    for (dirent_t* de = vfs_readdir(fd); de; de = vfs_readdir(fd)) {
        const char* nm = de->name;
        if (!show_all && nm[0] == '.') continue;
        int isd = (de->type == 1);
        if (long_fmt) {
            uint32_t sz = 0;
            if (!isd) {
                char ep[320];
                if (strcmp(dpath, "/") == 0) snprintf(ep, sizeof(ep), "/%s", nm);
                else                          snprintf(ep, sizeof(ep), "%s/%s", dpath, nm);
                int d2; vfs_stat(ep, &sz, &d2);
            }
            printf("%c %8u  %s\n", isd ? 'd' : '-', sz, nm);
        } else if (isd) {
            printf("%s/\n", nm);
        } else {
            printf("%s\n", nm);
        }
    }
    vfs_close(fd);
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

// Open a file in the GUI Text Editor window. Runs on the compositor thread (the
// kernel shell is dispatched from there), so it may create a window directly via
// compositor_open_editor. Pairs with `wget -O <file> <url>` for a download-and-read
// flow. The path is resolved to absolute against the shell's cwd.
static void cmd_open(int argc, char** argv) {
    if (argc < 2) { printf("Usage: open <file>\n"); return; }
    char abs[256];
    if (argv[1][0] == '/') {
        strncpy(abs, argv[1], sizeof(abs) - 1); abs[sizeof(abs) - 1] = '\0';
    } else {
        const char* cwd = vfs_getcwd();
        if (strcmp(cwd, "/") == 0) snprintf(abs, sizeof(abs), "/%s", argv[1]);
        else                       snprintf(abs, sizeof(abs), "%s/%s", cwd, argv[1]);
    }
    int fd = vfs_open(abs, 0, 0);          // fail early with a clear message
    if (fd < 0) { printf("open: cannot open %s\n", abs); return; }
    vfs_close(fd);
    if (compositor_open_editor(abs))
        printf("Opened %s in the editor.\n", abs);
    else
        printf("open: could not launch the editor\n");
}

static void cmd_touch(int argc, char** argv) {
    if (argc < 2) { printf("Usage: touch <file>\n"); return; }
    if (!path_last_component_ok(argv[1])) { printf("touch: invalid file name '%s'\n", argv[1]); return; }
    if (vfs_touch(argv[1]) < 0) printf("touch: failed to create %s\n", argv[1]);
}

static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { printf("Usage: mkdir <dir>\n"); return; }
    if (!path_last_component_ok(argv[1])) { printf("mkdir: invalid directory name '%s'\n", argv[1]); return; }
    if (vfs_mkdir(argv[1], 0755) < 0) printf("mkdir: failed to create %s\n", argv[1]);
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) { printf("Usage: rm <path>\n"); return; }
    if (vfs_unlink(argv[1]) < 0) printf("rm: failed to remove %s\n", argv[1]);
}

// ---------------------------------------------------------------------------
// cp -r — recursive directory copy (v6.4.252), the copy-OS-to-disk installer
// rung. Iterative (bounded off-stack frontier — the 4 KB kernel task stack
// forbids recursion): pop a (src,dst) pair, mkdir the dst, enumerate the src,
// push subdirs / copy files (via vfs_cp, which crosses filesystems). Enumerate/
// mkdir/copy are injected so the traversal is KAT'd against a synthetic tree.
// ---------------------------------------------------------------------------
#define CPT_MAX 64
static char cpt_ss[CPT_MAX][256], cpt_ds[CPT_MAX][256];   // off-stack frontier
static char cpt_fs[256], cpt_fd[256];                     // file src/dst scratch (off-stack)

typedef void (*cpt_child_fn)(void* cctx, const char* name, int is_dir);
typedef void (*cpt_enum_fn)(void* ectx, const char* path, cpt_child_fn child, void* cctx);
typedef int  (*cpt_mkdir_fn)(void* ctx, const char* path);
typedef int  (*cpt_copy_fn)(void* ctx, const char* src, const char* dst);

typedef struct {
    int n, files, errors;
    const char* cur_src; const char* cur_dst;
    cpt_copy_fn cp; void* opctx;
} cpt_state_t;

static void cpt_on_child(void* cctx, const char* name, int is_dir) {
    cpt_state_t* st = (cpt_state_t*)cctx;
    if (is_dir) {
        if (st->n >= CPT_MAX) { st->errors++; return; }      // frontier full
        du_join(st->cur_src, name, cpt_ss[st->n], 256);
        du_join(st->cur_dst, name, cpt_ds[st->n], 256);
        st->n++;
    } else {
        du_join(st->cur_src, name, cpt_fs, 256);
        du_join(st->cur_dst, name, cpt_fd, 256);
        if (st->cp(st->opctx, cpt_fs, cpt_fd) == 0) st->files++; else st->errors++;
    }
}

static int cptree_walk(const char* src, const char* dst, cpt_enum_fn enumf,
                       cpt_mkdir_fn mk, cpt_copy_fn cp, void* opctx,
                       int* out_dirs, int* out_files, int* out_err) {
    cpt_state_t st; st.n = 0; st.files = 0; st.errors = 0; st.cp = cp; st.opctx = opctx;
    strncpy(cpt_ss[0], src, 255); cpt_ss[0][255] = '\0';
    strncpy(cpt_ds[0], dst, 255); cpt_ds[0][255] = '\0';
    st.n = 1;
    int dirs = 0;
    char s[256], d[256];
    while (st.n > 0) {
        st.n--;                                              // pop; copy out so pushes can't clobber it
        strncpy(s, cpt_ss[st.n], 255); s[255] = '\0';
        strncpy(d, cpt_ds[st.n], 255); d[255] = '\0';
        dirs++;
        mk(opctx, d);                                        // ensure dst dir exists (best-effort)
        st.cur_src = s; st.cur_dst = d;
        enumf(opctx, s, cpt_on_child, &st);
    }
    if (out_dirs)  *out_dirs = dirs;
    if (out_files) *out_files = st.files;
    if (out_err)   *out_err = st.errors;
    return 0;
}

// Real VFS backing for cp -r.
static void cpt_vfs_enum(void* ec, const char* path, cpt_child_fn child, void* cc) {
    (void)ec;
    int fd = vfs_open(path, 0, 0);
    if (fd < 0) return;
    static dirent_t ents[256];
    int n = 0;
    dirent_t* de = vfs_readdir(fd);
    while (de && n < 256) { ents[n++] = *de; de = vfs_readdir(fd); }
    vfs_close(fd);
    for (int i = 0; i < n; i++) {
        const char* nm = ents[i].name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        child(cc, nm, ents[i].type == 1);
    }
}
static int cpt_vfs_mkdir(void* c, const char* path) { (void)c; vfs_mkdir(path, 0755); return 0; }  // ok if exists
static int cpt_vfs_cp(void* c, const char* s, const char* d) { (void)c; return vfs_cp(s, d); }

// KAT: drive cptree_walk over a synthetic tree, logging the mkdir/copy ops.
//   /a -> file f1, dir sub ; /a/sub -> file f2   ==>  copy to /b
static char cpt_log[512];
static void cpt_log_append(const char* a, const char* b, const char* c) {
    int L = 0; while (cpt_log[L]) L++;
    const char* parts[3] = { a, b, c };
    for (int p = 0; p < 3; p++) { const char* q = parts[p]; if (!q) continue; while (*q && L < 510) cpt_log[L++] = *q++; }
    if (L < 511) cpt_log[L++] = '|'; cpt_log[L] = '\0';
}
static void cpt_test_enum(void* ec, const char* path, cpt_child_fn child, void* cc) {
    (void)ec;
    if (strcmp(path, "/a") == 0)          { child(cc, "f1", 0); child(cc, "sub", 1); }
    else if (strcmp(path, "/a/sub") == 0) { child(cc, "f2", 0); }
}
static int cpt_test_mkdir(void* c, const char* p) { (void)c; cpt_log_append("M:", p, 0); return 0; }
static int cpt_test_cp(void* c, const char* s, const char* d) { (void)c; cpt_log_append("C:", s, d); return 0; }
int cptree_walk_selftest(void) {
    cpt_log[0] = '\0';
    int dirs = 0, files = 0, err = 0;
    cptree_walk("/a", "/b", cpt_test_enum, cpt_test_mkdir, cpt_test_cp, 0, &dirs, &files, &err);
    if (dirs != 2 || files != 2 || err != 0) return 1;       // dirs /a,/a/sub ; files f1,f2
    if (!strstr(cpt_log, "M:/b|"))                 return 2;
    if (!strstr(cpt_log, "M:/b/sub|"))             return 3;
    if (!strstr(cpt_log, "C:/a/f1/b/f1|"))         return 4;
    if (!strstr(cpt_log, "C:/a/sub/f2/b/sub/f2|")) return 5;
    return 0;
}

static void cmd_cp(int argc, char** argv) {
    if (argc >= 4 && strcmp(argv[1], "-r") == 0) {           // recursive directory copy
        int dirs = 0, files = 0, err = 0;
        cptree_walk(argv[2], argv[3], cpt_vfs_enum, cpt_vfs_mkdir, cpt_vfs_cp, 0, &dirs, &files, &err);
        printf("cp -r: copied %d file(s) across %d director(y/ies)%s\n", files, dirs, err ? " (some errors)" : "");
        return;
    }
    if (argc < 3) { printf("Usage: cp [-r] <src> <dst>\n"); return; }
    if (!path_last_component_ok(argv[2])) { printf("cp: invalid destination name '%s'\n", argv[2]); return; }
    if (vfs_cp(argv[1], argv[2]) < 0) printf("cp: failed to copy %s to %s\n", argv[1], argv[2]);
}

// ---- in-OS GRUB bootloader install ----------------------------------------
// Makes an installed NyxOS disk boot standalone (no host addgrub.sh). Embeds the
// real GRUB boot.img + core.img (grub_blobs.h) and writes them the way the proven
// host recipe does — patched core.img to the MBR gap, boot code to sector 0.
#define GRUB_CORE_SECTORS ((GRUB_CORE_IMG_SIZE + 511) / 512)

// Patch core.img's diskboot blocklist (@0x1f4): the rest of core.img is at disk
// LBA 2, length = core_sectors-1 (512-B sectors), load segment 0x0820. Pure (KAT'd).
static void grub_set_blocklist(uint8_t* core, uint32_t core_sectors) {
    uint32_t len = core_sectors - 1;
    for (int i = 0; i < 8; i++) core[0x1f4 + i] = (i == 0) ? 2 : 0;   // start LBA = 2 (8-B LE)
    core[0x1fc] = (uint8_t)(len & 0xFF);
    core[0x1fd] = (uint8_t)((len >> 8) & 0xFF);                       // length (2-B LE)
    core[0x1fe] = 0x20; core[0x1ff] = 0x08;                           // segment 0x0820
}
// Patch boot.img (@0x5c) with the LBA of core.img's first sector (1). Pure (KAT'd).
static void grub_set_core_lba(uint8_t* boot) {
    for (int i = 0; i < 8; i++) boot[0x5c + i] = (i == 0) ? 1 : 0;
}

// Write GRUB's boot sectors to `dev`: the patched core.img to the MBR gap (LBA
// 1..), then boot.img's boot code to sector 0 preserving the partition table.
// Routes through blockdev (ATA or NVMe). Returns 0 on success.
static int install_grub_to(uint8_t dev) {
    static uint8_t core_work[GRUB_CORE_SECTORS * 512];
    __builtin_memset(core_work, 0, sizeof(core_work));
    __builtin_memcpy(core_work, grub_core_img, GRUB_CORE_IMG_SIZE);
    grub_set_blocklist(core_work, GRUB_CORE_SECTORS);
    if (blk_write(dev, 1, GRUB_CORE_SECTORS, core_work) != 0) return -1;

    static uint8_t boot_work[512];
    __builtin_memcpy(boot_work, grub_boot_img, 512);
    grub_set_core_lba(boot_work);
    static uint8_t sec0[512];
    if (blk_read1(dev, 0, sec0) != 0) return -2;
    __builtin_memcpy(sec0, boot_work, 446);            // boot code only; keep the MBR table [446..511]
    if (blk_write1(dev, 0, sec0) != 0) return -3;
    blk_flush(dev);
    return 0;
}

// nyxgrub <drive> — install GRUB's boot sectors so an installed disk boots on its
// own. Non-destructive to the partition table; pair it after nyxinstall.
static void cmd_nyxgrub(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: nyxgrub <drive>\n");
        printf("  Installs GRUB boot sectors so an installed NyxOS disk boots standalone.\n");
        printf("  Writes sector 0's boot code + LBA 1..%u (keeps the partition table).\n", GRUB_CORE_SECTORS);
        return;
    }
    uint8_t dev = parse_blk_dev(argv[1]);
    if (blk_dev_sectors(dev) == 0) { printf("nyxgrub: %s not available\n", argv[1]); return; }
    printf("nyxgrub: installing GRUB to %s (core.img %u sectors -> LBA 1..)...\n", argv[1], GRUB_CORE_SECTORS);
    int r = install_grub_to(dev);
    if (r != 0) { printf("nyxgrub: install failed (rc=%d)\n", r); return; }
    printf("nyxgrub: done. The disk should boot standalone (BIOS/CSM). Partition table preserved.\n");
}

// KAT the pure blocklist / core-LBA patch math against the proven byte layout.
static int nyxgrub_selftest(void) {
    static uint8_t c[512];
    for (int i = 0; i < 512; i++) c[i] = 0xAA;
    grub_set_blocklist(c, 303);
    if (c[0x1f4] != 2) return 1;
    for (int i = 0x1f5; i <= 0x1fb; i++) if (c[i] != 0) return 2;
    if (c[0x1fc] != 0x2E || c[0x1fd] != 0x01) return 3;   // len 302 = 0x012E (LE)
    if (c[0x1fe] != 0x20 || c[0x1ff] != 0x08) return 4;   // seg 0x0820 (LE)
    static uint8_t b[512];
    for (int i = 0; i < 512; i++) b[i] = 0xAA;
    grub_set_core_lba(b);
    if (b[0x5c] != 1) return 5;
    for (int i = 0x5d; i <= 0x63; i++) if (b[i] != 0) return 6;
    return 0;
}

// nyxinstall <drive> confirm [srcdir] — the guided installer. Ties together the
// disk pieces into one flow: partition (MBR) -> mkfs ext2 -> mount /mnt -> copy
// the OS tree (srcdir, default /bin) onto it -> install GRUB. DESTRUCTIVE: wipes
// the drive, so it demands the literal `confirm` keyword. Calls the primitives
// directly (no nested execute_command).
// Shared ext2-populate step for both installers: mkfs -> mount /mnt -> cp -r the OS
// tree -> write /boot/grub/grub.cfg + copy the kernel to /boot/nyx-kernel.bin. The
// embedded initramfs makes /boot/nyx-kernel.bin self-sufficient to boot. 0 on success.
static int install_populate_ext2(uint8_t dev, uint32_t part_lba, uint32_t blocks,
                                 const char* src, int* have_kernel_out) {
    char hb[24];
    du_human((uint64_t)blocks * 1024ULL, hb, sizeof(hb));
    printf("  Formatting ext2 (%s)...\n", hb);
    if (ext2_format(dev, part_lba, blocks) != 0) { printf("  mkfs failed\n"); return -1; }
    printf("  Mounting at /mnt...\n");
    if (ext2_mount(dev, part_lba) != 0) { printf("  mount failed\n"); return -1; }
    if (vfs_mount("/mnt", FS_TYPE_EXT2, NULL) < 0) { printf("  VFS mount failed\n"); return -1; }
    printf("  Copying %s -> /mnt ...\n", src);
    int dirs = 0, files = 0, err = 0;
    cptree_walk(src, "/mnt", cpt_vfs_enum, cpt_vfs_mkdir, cpt_vfs_cp, 0, &dirs, &files, &err);
    vfs_mkdir("/mnt/boot", 0755);
    vfs_mkdir("/mnt/boot/grub", 0755);
    static const char* gcfg =
        "set timeout=3\nset default=0\ninsmod all_video\n"
        "menuentry 'NyxOS' {\n    multiboot2 /boot/nyx-kernel.bin\n    boot\n}\n";
    vfs_write_file("/mnt/boot/grub/grub.cfg", gcfg, (uint32_t)strlen(gcfg));
    uint32_t ksz = 0; int kisd = 0, have_kernel = 0;
    if (vfs_stat("/nyxkernel.bin", &ksz, &kisd) == 0 && !kisd && ksz > 0) {
        if (vfs_cp("/nyxkernel.bin", "/mnt/boot/nyx-kernel.bin") == 0) {
            have_kernel = 1;
            printf("  kernel image (%u bytes) -> /boot/nyx-kernel.bin\n", ksz);
        } else printf("  (kernel copy failed)\n");
    } else printf("  (no /nyxkernel.bin module; add /boot/nyx-kernel.bin manually)\n");
    printf("  %d file(s) across %d director(y/ies)%s\n", files, dirs, err ? " (with errors)" : "");
    blk_flush(dev);          // final commit: force the whole write cache to the medium before we return
    if (have_kernel_out) *have_kernel_out = have_kernel;
    return 0;
}

// nyxinstall --uefi <drive> confirm [srcdir] — the full UEFI install: GPT (nyxgpt) +
// FAT ESP with GRUB-EFI (nyxfat) + an ext2 NyxOS partition (mkfs + the OS + kernel).
// The written disk boots on a UEFI machine: firmware -> \EFI\BOOT\BOOTX64.EFI (GRUB) ->
// grub.cfg `search --file /boot/nyx-kernel.bin` -> multiboot2 the kernel off the ext2.
static void cmd_nyxinstall_uefi(int argc, char** argv) {
    // argv: [0]=nyxinstall [1]=--uefi [2]=drive [3]=confirm [4]=srcdir?
    if (argc < 4 || strcmp(argv[3], "confirm") != 0) {
        printf("Usage: nyxinstall --uefi <drive> confirm [srcdir]\n");
        printf("  UEFI install: GPT + FAT ESP (GRUB-EFI) + ext2 NyxOS partition. ERASES the drive.\n");
        return;
    }
    uint8_t dev = parse_blk_dev(argv[2]);
    const char* src = (argc >= 5) ? argv[4] : "/bin";
    if (strcmp(src, "/") == 0 || strncmp(src, "/mnt", 4) == 0) {
        printf("nyxinstall: refusing srcdir '%s' (would recurse into the target)\n", src); return;
    }
    if (blk_dev_sectors(dev) == 0) { printf("nyxinstall: %s not available\n", argv[2]); return; }
    printf("=== nyxinstall --uefi: target %s ===\n", argv[2]);

    printf("[1/3] GPT (ESP + NyxOS)...\n");
    char* gv[] = { "nyxgpt", (char*)argv[2], "confirm" }; cmd_nyxgpt(3, gv);
    printf("[2/3] ESP FAT32 + GRUB-EFI loader...\n");
    char* fv[] = { "nyxfat", (char*)argv[2], "confirm" }; cmd_nyxfat(3, fv);

    uint64_t nx_start = 0, nx_sectors = 0;
    if (gpt_find_nyx(dev, &nx_start, &nx_sectors) != 0) { printf("nyxinstall: no NyxOS partition in the GPT\n"); return; }
    uint32_t blocks = (uint32_t)(nx_sectors / 2);
    if (blocks > 32u * 8192 + 1) blocks = 32u * 8192 + 1;               // mkfs single-drive cap
    printf("[3/3] ext2 NyxOS partition @ LBA %u...\n", (uint32_t)nx_start);
    int have_kernel = 0;
    if (install_populate_ext2(dev, (uint32_t)nx_start, blocks, src, &have_kernel) != 0) { printf("nyxinstall: populate failed\n"); return; }

    printf("=== nyxinstall --uefi done ===\n");
    printf("  ESP: /EFI/BOOT/BOOTX64.EFI (GRUB-EFI) + grub.cfg; NyxOS ext2 %s.\n",
           have_kernel ? "has /boot/nyx-kernel.bin" : "(NO kernel — boot will fail)");
    printf("  Boot this disk on a UEFI machine (Secure Boot off) to run NyxOS.\n");
}

static void cmd_nyxinstall(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "--uefi") == 0) { cmd_nyxinstall_uefi(argc, argv); return; }
    if (argc < 3 || strcmp(argv[2], "confirm") != 0) {
        printf("Usage: nyxinstall <drive> confirm [srcdir]      (legacy MBR/BIOS install)\n");
        printf("       nyxinstall --uefi <drive> confirm [srcdir]  (GPT + ESP + GRUB-EFI, UEFI)\n");
        printf("  Installs NyxOS to a disk: partition -> mkfs ext2 -> mount -> copy the OS tree.\n");
        printf("  WARNING: this ERASES all data on the target drive.\n");
        return;
    }
    uint8_t dev = parse_blk_dev(argv[1]);
    const char* src = (argc >= 4) ? argv[3] : "/bin";
    if (strcmp(src, "/") == 0 || strncmp(src, "/mnt", 4) == 0) {
        printf("nyxinstall: refusing srcdir '%s' (would recurse into the install target)\n", src);
        return;
    }
    uint64_t total = blk_dev_sectors(dev);
    if (total == 0) { printf("nyxinstall: %s not available\n", argv[1]); return; }
    const char* model = (dev == BLK_NVME0) ? nvme_model_str() : "disk";
    char hb[24]; du_human(total * 512ULL, hb, sizeof(hb));
    printf("=== nyxinstall: target %s  [%s  %s] ===\n", argv[1], model[0] ? model : "disk", hb);

    uint32_t part_lba = 2048;
    if (total <= part_lba + 128) { printf("nyxinstall: disk too small\n"); return; }
    uint32_t part_sectors = (total - part_lba) > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)(total - part_lba);
    uint32_t blocks = part_sectors / 2;
    if (blocks > 32u * 8192 + 1) blocks = 32u * 8192 + 1;               // mkfs single-drive cap

    printf("[1/4] Partitioning (MBR: 1x bootable Linux @ LBA %u)...\n", part_lba);
    mbr_part_t p = { .status = 0x80, .type = 0x83, .lba_start = part_lba, .sectors = part_sectors };
    static uint8_t sec[512];
    mbr_build(&p, 1, sec);
    if (blk_write1(dev, 0, sec) != 0) { printf("nyxinstall: partition write failed\n"); return; }
    blk_flush(dev);

    printf("[2/4] ext2 + OS tree + kernel...\n");
    int have_kernel = 0;
    if (install_populate_ext2(dev, part_lba, blocks, src, &have_kernel) != 0) { printf("nyxinstall: populate failed\n"); return; }

    printf("[3/4] Installing GRUB boot sectors (core.img %u sectors -> gap)...\n", GRUB_CORE_SECTORS);
    int grub_ok = (have_kernel && install_grub_to(dev) == 0);
    if (grub_ok) printf("  GRUB installed to the MBR/gap - the disk boots standalone.\n");
    else if (!have_kernel) printf("  (skipped: no kernel on disk to boot)\n");
    else printf("  (GRUB install failed)\n");

    printf("[4/4] === nyxinstall done: written to %s ===\n", argv[1]);
    if (grub_ok) printf("  Reboot from this disk (BIOS/CSM) to run the installed NyxOS.\n");
    else printf("  Run 'nyxgrub %s' to install the bootloader for standalone boot.\n", argv[1]);
}

// nyxverify <drive> — mount the installed NyxOS partition and read /boot/nyx-kernel.bin
// back, checking for the ELF + multiboot2 header both via the ext2 file path AND by a
// RAW read of the file's first on-disk block. Diagnoses whether an installed kernel is
// intact on the medium (run it right after install, and again after a power cycle to
// tell a write bug from a persistence bug).
static void cmd_nyxverify(int argc, char** argv) {
    if (argc < 2) { printf("Usage: nyxverify <drive>\n  Check the installed /boot/nyx-kernel.bin (ELF + multiboot2, ext2 + raw).\n"); return; }
    uint8_t dev = parse_blk_dev(argv[1]);
    if (blk_dev_sectors(dev) == 0) { printf("nyxverify: %s not available\n", argv[1]); return; }
    uint64_t nx_start = 0, nx_sectors = 0;
    if (gpt_find_nyx(dev, &nx_start, &nx_sectors) != 0) { printf("nyxverify: no NyxOS partition in the GPT\n"); return; }
    if (ext2_mount(dev, (uint32_t)nx_start) != 0) { printf("nyxverify: ext2 mount failed\n"); return; }
    printf("=== nyxverify: NyxOS ext2 @ LBA %u (block size %u) ===\n", (uint32_t)nx_start, ext2_fs.block_size);
    uint32_t ino = ext2_resolve("/boot/nyx-kernel.bin");
    if (!ino) { printf("  /boot/nyx-kernel.bin: NOT FOUND in the ext2\n"); return; }
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) < 0) { printf("  inode %u read failed\n", ino); return; }
    printf("  /boot/nyx-kernel.bin: inode %u  size %u  block[0]=%u block[1]=%u\n",
           ino, inode.size, inode.block[0], inode.block[1]);

    // (a) ext2 file path: read the first block through the ext2 block walker
    static uint8_t b0[4096];
    __builtin_memset(b0, 0, sizeof(b0));
    ext2_read_inode_block(&inode, 0, b0);
    int elf_e = (b0[0] == 0x7f && b0[1] == 'E' && b0[2] == 'L' && b0[3] == 'F');
    printf("  ext2 first 8: %02x %02x %02x %02x %02x %02x %02x %02x  ELF=%s\n",
           b0[0], b0[1], b0[2], b0[3], b0[4], b0[5], b0[6], b0[7], elf_e ? "OK" : "NO");

    // (b) RAW read of the first data block's LBA, bypassing the ext2 block logic
    uint32_t raw_lba = ext2_fs.part_start_lba + inode.block[0] * (ext2_fs.block_size / 512);
    static uint8_t rr[512];
    __builtin_memset(rr, 0, sizeof(rr));
    blk_read1(dev, raw_lba, rr);
    int elf_r = (rr[0] == 0x7f && rr[1] == 'E' && rr[2] == 'L' && rr[3] == 'F');
    printf("  raw LBA %u first 8: %02x %02x %02x %02x %02x %02x %02x %02x  ELF=%s\n",
           raw_lba, rr[0], rr[1], rr[2], rr[3], rr[4], rr[5], rr[6], rr[7], elf_r ? "OK" : "NO");

    // (c) multiboot2 magic (0xE85250D6, 8-byte aligned) somewhere in the first 32 KB
    static uint8_t buf[32768];
    int n = ext2_read_file("/boot/nyx-kernel.bin", buf, sizeof(buf));
    int mb2 = 0;
    for (int i = 0; i + 4 <= n; i += 8) {
        uint32_t m = (uint32_t)buf[i] | ((uint32_t)buf[i+1] << 8) | ((uint32_t)buf[i+2] << 16) | ((uint32_t)buf[i+3] << 24);
        if (m == 0xE85250D6u) { mb2 = 1; break; }
    }
    printf("  multiboot2 magic in first %d B: %s\n", n, mb2 ? "FOUND" : "MISSING");
    printf("  => on-disk kernel looks %s\n", (elf_r && mb2) ? "VALID" : "CORRUPT/EMPTY -- this is why GRUB fails");
}

static void cmd_mv(int argc, char** argv) {
    if (argc < 3) { printf("Usage: mv <src> <dst>\n"); return; }
    if (!path_last_component_ok(argv[2])) { printf("mv: invalid destination name '%s'\n", argv[2]); return; }
    vfs_rename(argv[1], argv[2]);
}

static void cmd_useradd(int argc, char** argv) {
    if (argc < 3) { printf("Usage: useradd <username> <password> [avatar 0-3]\n"); return; }
    // Persisted to /etc/passwd on the ext2 disk (PBKDF2-HMAC-SHA256), so the
    // account survives a reboot; falls back to an in-memory user with no disk.
    // Optional profile-picture id (0-3); users normally pick it at the login screen.
    int avatar = (argc >= 4) ? atoi(argv[3]) : 0;
    auth_add_user(argv[1], argv[2], avatar);
}

static void cmd_users(int argc, char** argv) {
    (void)argc; (void)argv;
    auth_list_users();
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
                IP4_OCTETS(net_interfaces[i].ip));
        }
    }
}

// arp — show the ARP cache (resolved IPv4 -> MAC neighbours), like `arp -a`. Filled as the
// stack resolves hosts: the DHCP gateway, a ping/DNS/HTTP target. Read-only; the table is
// static in net/arp.c and reached through its arp_cache_size/arp_cache_entry accessors.
static void cmd_arp(int argc, char** argv) {
    (void)argc; (void)argv;
    extern int arp_cache_size(void);
    extern int arp_cache_entry(int idx, uint32_t* ip, uint8_t* mac);
    printf("Address");
    for (int p = 7; p < 18; p++) putchar(' ');
    printf("HWaddress\n");
    int shown = 0;
    for (int i = 0; i < arp_cache_size(); i++) {
        uint32_t ip; uint8_t mac[6];
        if (!arp_cache_entry(i, &ip, mac)) continue;
        char ips[16]; snprintf(ips, sizeof(ips), "%d.%d.%d.%d", IP4_OCTETS(ip));
        printf("%s", ips);
        for (int p = (int)strlen(ips); p < 18; p++) putchar(' ');
        printf("%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        shown++;
    }
    if (!shown) printf("(arp cache empty)\n");
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
        printf("%s -> %d.%d.%d.%d\n", argv[1], IP4_OCTETS(ip));
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
    printf("PING %d.%d.%d.%d: 56 data bytes\n", IP4_OCTETS(ip));

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
// Full-screen graphical panic (a NyxOS "sad-face" stop screen). Best-effort: it
// only draws when a framebuffer is up, and it renders straight to the hardware
// LFB (fb_use_lfb_direct) because by panic time the compositor's double-buffer
// present loop is dead — anything drawn to the back buffer would never appear.
static void panic_screen(const char* msg, uint64_t cr0, uint64_t cr2,
                         uint64_t cr3, uint64_t caller, const uint64_t* bt, int btn) {
    if (!fb_get_addr() || fb_get_width() == 0) return;   // no framebuffer: text only
    fb_use_lfb_direct();
    uint32_t w = fb_get_width(), h = fb_get_height();
    uint32_t bg  = fb_rgb(58, 26, 92);      // deep NyxOS purple (brand)
    uint32_t fg  = fb_rgb(245, 240, 252);
    uint32_t dim = fb_rgb(198, 178, 232);

    fb_fill_rect(0, 0, w, h, bg);

    int margin = 80;
    int y = 76;

    font_draw_string_scaled(margin, y, ":(", fg, bg, 7);     // big sad face
    y += 7 * (int)font_get_height() + 28;

    font_draw_string_scaled(margin, y, "KERNEL PANIC", fg, bg, 3);
    y += 3 * (int)font_get_height() + 16;

    font_draw_string(margin, y, "NyxOS ran into a problem and had to stop.", dim, bg);
    y += (int)font_get_height() + 22;

    font_draw_string(margin, y, "Reason:", dim, bg);
    y += (int)font_get_height() + 6;

    // The panic message, wrapped to the screen width at 2x scale.
    int mscale = 2;
    int maxc = (int)(w - 2 * margin) / (int)(font_get_width() * mscale);
    if (maxc < 1) maxc = 1;
    int len = 0; while (msg && msg[len]) len++;
    if (len == 0) {
        font_draw_string_scaled(margin, y, "(unknown)", fg, bg, mscale);
        y += (int)font_get_height() * mscale + 6;
    }
    for (int i = 0; i < len; ) {
        char line[160];
        int n = 0;
        while (n < maxc && n < 159 && msg[i + n]) { line[n] = msg[i + n]; n++; }
        line[n] = '\0';
        font_draw_string_scaled(margin, y, line, fg, bg, mscale);
        y += (int)font_get_height() * mscale + 6;
        i += n;
    }

    // Register / fault context.
    y += 18;
    char rb[96];
    snprintf(rb, sizeof(rb), "CR0=0x%lx   CR2=0x%lx", cr0, cr2);
    font_draw_string(margin, y, rb, dim, bg); y += (int)font_get_height() + 4;
    snprintf(rb, sizeof(rb), "CR3=0x%lx   from=0x%lx", cr3, caller);
    font_draw_string(margin, y, rb, dim, bg);

    // Best-effort backtrace (stack-scan .text hits): the call trail to run through
    // addr2line — the only debug signal on real hardware, which has no serial.
    if (btn > 0) {
        y += (int)font_get_height() + 12;
        font_draw_string(margin, y, "Backtrace (stack scan):", dim, bg);
        y += (int)font_get_height() + 4;
        for (int i = 0; i < btn; ) {
            char line[96]; int p = 0;
            for (int k = 0; k < 4 && i < btn; k++, i++) {
                char one[20]; snprintf(one, sizeof(one), "0x%lx ", bt[i]);
                for (int z = 0; one[z] && p < 95; z++) line[p++] = one[z];
            }
            line[p] = '\0';
            font_draw_string(margin, y, line, dim, bg);
            y += (int)font_get_height() + 4;
        }
    }

    // Footer.
    font_draw_string(margin, (int)h - 58,
                     "The system has been halted to prevent damage.", dim, bg);
    font_draw_string(margin, (int)h - 38,
                     "Please restart your device (power cycle or Ctrl+Alt+Del).", dim, bg);

    // Brand mark — the shared NyxOS purple moon (nyx_logo.h), top-right corner, with
    // a "NyxOS" label centered under it. Same art/tint as the login + boot splash.
    uint32_t moon = fb_rgb(168, 138, 245);
    int moon_w = NYX_LOGO_COLS * (int)font_get_width();
    int moon_x = (int)w - moon_w - margin;
    int moon_y = 34;
    for (int i = 0; i < NYX_LOGO_ROWS; i++)
        font_draw_string(moon_x, moon_y + i * (int)font_get_height(), NYX_LOGO[i], moon, bg);
    font_draw_string_scaled(moon_x + (moon_w - 5 * (int)font_get_width()) / 2,
                            moon_y + NYX_LOGO_ROWS * (int)font_get_height() + 6, "NyxOS", dim, bg, 1);
    fb_present();   // no-op unless rotated (then publishes the rotated panic frame)
}

void kernel_panic(const char* msg, ...) {
    __asm__ volatile("cli");            // stop the scheduler and other IRQs
    uint64_t caller = (uint64_t)__builtin_return_address(0);

    // Format the message once, into static storage (the stack may be exhausted).
    static char pbuf[256];
    va_list args;
    va_start(args, msg);
    vsnprintf(pbuf, sizeof(pbuf), msg, args);
    va_end(args);

    uint64_t cr0, cr2, cr3;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    // Best-effort backtrace: -Os omits the frame pointer, so scan the current stack PAGE
    // (guaranteed mapped — never fault while panicking) and keep the qwords that land in
    // the kernel .text range: the likely return addresses. Heuristic (some false hits),
    // but it turns the panic screen — the only debug signal on real hardware (no serial) —
    // into an addr2line-able call trail.
    extern char _text_start[], _text_end[];
    static uint64_t bt[10];
    int btn = 0;
    {
        uint64_t rsp; __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
        uint64_t ts = (uint64_t)_text_start, te = (uint64_t)_text_end;
        uint64_t page_end = (rsp | 0xFFFULL) + 1;              // end of RSP's 4 KB page
        for (uint64_t a = rsp; a + 8 <= page_end && btn < 10; a += 8) {
            uint64_t v = *(volatile uint64_t*)a;
            if (v >= ts && v < te) bt[btn++] = v;
        }
    }

    // Text/serial log first (kept for debugging; greppable "[KERNEL PANIC]").
    printf("\n\n[KERNEL PANIC] %s\n\nSystem halted.\n", pbuf);
    printf("CR0=0x%lx CR2=0x%lx CR3=0x%lx from=0x%lx\n", cr0, cr2, cr3, caller);
    printf("Backtrace (stack scan, .text hits):");
    for (int i = 0; i < btn; i++) printf(" 0x%lx", bt[i]);
    printf("\n");

    // Then the visual stop screen (overwrites the framebuffer if one is up).
    panic_screen(pbuf, cr0, cr2, cr3, caller, bt, btn);

    while (1) { __asm__ volatile("hlt"); }
}

// ============================================================
// kernel_main
// ============================================================
static void* saved_mboot_ptr = NULL;
static uint64_t saved_mboot_magic = 0;   // 0x2BADB002 = multiboot1, 0x36d76289 = multiboot2

// Firmware physical-memory map, parsed from the multiboot2 type-6 tag and handed to
// init_memory so the allocator frees ONLY available RAM (every reserved hole stays out).
#define MAX_MMAP 64
static mb_mmap_entry_t g_mmap[MAX_MMAP];
static int g_mmap_count = 0;

// The linear framebuffer GRUB set up for us (multiboot2 type-8 tag, filled in by the boot-info
// parse in kernel_main). grub_fb_addr != 0 means the bootloader handed us a real LFB — the path
// that works on physical hardware / UEFI-GOP, where the QEMU-only Bochs-VBE ports are absent.
static uint64_t grub_fb_addr  = 0;
static uint32_t grub_fb_pitch = 0, grub_fb_w = 0, grub_fb_h = 0;
static uint8_t  grub_fb_bpp = 0, grub_fb_type = 0;
// The pixel's RGB channel layout, from the type-8 tag's color_info (RGB framebuffers only).
// NyxOS's renderer writes 0x00RRGGBB into 32-bit little-endian memory, i.e. it assumes
// blue@0, green@8, red@16 (BGRX). Intel GOP / QEMU stdvga report exactly that; we log the
// real numbers so a physical-hardware bringup can spot a swapped-channel display immediately.
static uint8_t  grub_fb_red_pos = 0, grub_fb_red_sz = 0;
static uint8_t  grub_fb_green_pos = 0, grub_fb_green_sz = 0;
static uint8_t  grub_fb_blue_pos = 0, grub_fb_blue_sz = 0;

// Enable the CPU's Supervisor Mode Execution/Access Prevention (SMEP/SMAP), if the
// processor advertises them (CPUID.(EAX=7,ECX=0):EBX bit 7 = SMEP, bit 20 = SMAP).
//  - SMEP (CR4.SMEP, bit 20): a #GP if ring 0 tries to EXECUTE a user (U=1) page.
//  - SMAP (CR4.SMAP, bit 21): a #GP if ring 0 READS/WRITES a user page unless RFLAGS.AC
//    is set (via stac/clac).
// NyxOS's kernel never touches user pages directly: it runs on the kernel CR3 (where
// user pages aren't mapped) and reaches user data only by walking the user tables to a
// PHYSICAL address and accessing it through the identity map (a supervisor page). So no
// stac/clac bracketing is needed, and enabling these is a HARDWARE-ENFORCED audit of
// that isolation invariant — any future stray user-VA access from ring 0 now traps
// instead of silently succeeding. We CLAC afterwards so AC starts cleared (the kernel
// never wants it). Called on the BSP after paging is up, and by each AP (smp.c).
// Note: the default QEMU `qemu64` CPU does NOT advertise SMEP/SMAP, so this is a no-op
// there; run with `-cpu qemu64,+smep,+smap` (or a Haswell+/`max` model) to activate it.
// CR4 is per-CPU, so every core applies this itself. Returns bit0=SMEP, bit1=SMAP
// as actually enabled. Silent on purpose: APs call this, and printf is shared
// unlocked state that an AP has no business touching.
int cpu_apply_smep_smap(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    int have_smep = (ebx >> 7)  & 1;
    int have_smap = (ebx >> 20) & 1;
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (have_smep) cr4 |= (1UL << 20);
    if (have_smap) cr4 |= (1UL << 21);
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
    if (have_smap) __asm__ volatile("clac");   // start with AC clear (only legal once SMAP is on)
    return (have_smep ? 1 : 0) | (have_smap ? 2 : 0);
}

// BSP entry point: apply, then report.
void enable_smep_smap(void) {
    int f = cpu_apply_smep_smap();
    printf("[CPU] SMEP=%s SMAP=%s\n", (f & 1) ? "on" : "unavailable",
           (f & 2) ? "on" : "unavailable");
}

// Enable the x87 FPU and SSE so ring-3 programs built with the stock x86_64 codegen —
// which uses XMM registers for float math AND for 16-byte struct/mem copies (movups) —
// can run. The boot path only turns on PAE/long-mode/paging, leaving CR4.OSFXSR clear,
// so any SSE instruction #UDs. NyxOS's own kernel and coreutils are all built -mno-sse,
// so nothing else ever touches XMM/x87: DOOM (the sole SSE user) therefore keeps its
// vector state across context switches for free, without per-task fxsave/fxrstor. If a
// second SSE userspace program is ever added, this must grow into an fxsave-on-switch
// (or lazy-FPU) scheme. Added v5.9.34 for the DOOM port.
static void enable_sse_fpu(void) {
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2);        // CR0.EM = 0: use the real x87 FPU, don't emulate/trap it
    cr0 |=  (1ULL << 1);        // CR0.MP = 1: monitor coprocessor
    write_cr0(cr0);
    uint64_t cr4 = read_cr4();
    cr4 |= (1ULL << 9);         // CR4.OSFXSR:     OS supports fxsave/fxrstor -> enables SSE
    cr4 |= (1ULL << 10);        // CR4.OSXMMEXCPT: unmasked SIMD FP faults raise #XF, not #UD
    write_cr4(cr4);
    __asm__ volatile ("fninit"); // bring the x87 unit to a known state
}

// The offline known-answer self-tests (crypto + image decoders), each returning 0
// on success. Declared here for run_selftests(); the cmd_* wrappers use the same
// functions, so these match their existing header declarations.
extern int sha512_selftest(void);
extern int sha1_selftest(void);
extern int md5_selftest(void);
extern int pbkdf2_selftest(void);
extern int csprng_selftest(void);
extern int aes_gcm_selftest(void);
extern int curve25519_selftest(void);
extern int ed25519_selftest(void);
extern int ed25519_verify_selftest(void);
extern int filetype_selftest(void);
extern int tar_selftest(void);
extern int datefmt_selftest(void);
extern int ini_selftest(void);
extern int factor_selftest(void);
extern int semver_selftest(void);
extern int base85_selftest(void);
extern int fnv_selftest(void);
extern int csv_selftest(void);
extern int base58_selftest(void);
extern int url_codec_selftest(void);
extern int tls_prf_selftest(void);
extern int tls_keyschedule_selftest(void);
extern int tls_record_selftest(void);
extern int tls_ske_p384_selftest(void);
extern int der_selftest(void);
extern int base64_selftest(void);
extern int base32_selftest(void);
extern int base16_selftest(void);
extern int utf8_selftest(void);
extern int totp_selftest(void);
extern int ipv4_parse_selftest(void);
extern int ipv6_parse_selftest(void);
extern int numparse_selftest(void);
extern int hkdf_selftest(void);
extern int chacha20_selftest(void);
extern int siphash_selftest(void);
extern int poly1305_selftest(void);
extern int chacha20poly1305_selftest(void);
extern int blake2s_selftest(void);
extern int aes_cmac_selftest(void);
extern int aes_ctr_selftest(void);
extern int sha3_selftest(void);
extern int ct_selftest(void);
extern int aes_cbc_selftest(void);
extern int aes_kw_selftest(void);
extern int url_parse_selftest(void);
extern int glob_selftest(void);
extern int leb128_selftest(void);
extern int crc16_selftest(void);
extern int namecheck_selftest(void);
extern int ansi_csi_selftest(void);
extern int caldate_selftest(void);
extern int p256_selftest(void);
extern int p384_selftest(void);
extern int rsa_selftest(void);
extern int x509_selftest(void);
extern int inflate_selftest(void);
extern int deflate_selftest(void);
extern int png_selftest(void);
extern int bmp_selftest(void);
extern int gif_selftest(void);
extern int jpeg_selftest(void);
extern int image_reject_selftest(void);
extern int tcp_checksum_selftest(void);
extern int dns_response_selftest(void);
extern int vfs_pathnorm_selftest(void);
extern int dhcp_options_selftest(void);
// http_parse_selftest / ext2_dir_selftest are declared in ../net/http.h / ../fs/ext2.h (included above).

// ---- Advanced math: deterministic primality for any 64-bit integer -----------
// A real number-theory capability (user backlog: "advanced-math tests / capability").
// Deterministic Miller-Rabin: for 64-bit n the twelve witnesses 2..37 give a proven-
// correct answer (they suffice past 3.3e24), so this is EXACT, not probabilistic —
// it even rejects Carmichael numbers that fool a naive Fermat test. Integer only:
// mulmod is the binary (shift-add) method so it never needs a 128-bit intermediate
// (and thus no __int128 / libgcc __umodti3 the freestanding kernel can't link),
// keeping it float-free and dependency-free like the rest of the kernel.
// (is_prime_u64 + the Pollard-rho factor engine moved to kernel/core/factor.c, shared
// via factor.h with the `factor` and `isprime` commands.)
// KAT: edge cases, small primes/composites, Carmichael numbers (composite yet
// Fermat-pseudoprime to every coprime base), and large 64-bit primes/composites.
static int mathx_selftest(void) {
    if (is_prime_u64(0) || is_prime_u64(1)) return 1;
    if (!is_prime_u64(2) || !is_prime_u64(3)) return 2;
    if (is_prime_u64(4) || is_prime_u64(9))  return 3;
    static const uint64_t P[] = {5,7,13,97,7919,104729};
    for (int i = 0; i < 6; i++) if (!is_prime_u64(P[i])) return 10 + i;
    static const uint64_t C[] = {15,100,7917,104730,999983ull*3};
    for (int i = 0; i < 5; i++) if (is_prime_u64(C[i]))  return 20 + i;
    static const uint64_t CARM[] = {561, 1105, 41041, 825265ull};   // must be rejected
    for (int i = 0; i < 4; i++) if (is_prime_u64(CARM[i])) return 30 + i;
    if (!is_prime_u64(2305843009213693951ull))  return 40;  // Mersenne M61
    if (!is_prime_u64(18446744073709551557ull)) return 41;  // largest prime below 2^64
    if (is_prime_u64(18446744073709551614ull))  return 42;  // 2^64-2, even -> composite
    if (is_prime_u64(1000000007ull * 1000000009ull)) return 43; // product of two primes
    return 0;
}

// ---- CRC-32 (IEEE 802.3) data-integrity checksum -----------------------------
// A standard building block a maturing OS should have: PNG chunk validation, zip/
// gzip, an Ethernet FCS check, general file integrity. Reflected CRC-32 (poly
// 0xEDB88320, init/final 0xFFFFFFFF) computed bitwise — no 1 KiB table, no deps,
// float-free like the rest of the kernel. crc32("123456789") == 0xCBF43926.
uint32_t crc32_calc(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return ~crc;
}
// KAT: the canonical check value plus RFC/common vectors, and a distinctness check.
static int crc32_selftest(void) {
    if (crc32_calc((const uint8_t*)"", 0)          != 0x00000000u) return 1;
    if (crc32_calc((const uint8_t*)"123456789", 9) != 0xCBF43926u) return 2;  // canonical
    if (crc32_calc((const uint8_t*)"abc", 3)       != 0x352441C2u) return 3;
    if (crc32_calc((const uint8_t*)"The quick brown fox jumps over the lazy dog", 43)
                                                   != 0x414FA339u) return 4;
    if (crc32_calc((const uint8_t*)"a", 1) == crc32_calc((const uint8_t*)"b", 1)) return 5;
    return 0;
}

// ---- CRC-32C (Castagnoli) data-integrity checksum ----------------------------
// The storage-world CRC: ext4 metadata, Btrfs, iSCSI, SCTP — a different, stronger
// polynomial than the IEEE crc32 above (and the one modern CPUs accelerate with a
// dedicated instruction). Reflected (poly 0x82F63B78 = bit-reversed 0x1EDC6F41,
// init/final 0xFFFFFFFF), computed bitwise, no table. crc32c("123456789") == 0xE3069283.
uint32_t crc32c_calc(const uint8_t* data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0x82F63B78u) : (crc >> 1);
    }
    return ~crc;
}
// KAT: the canonical check value plus common vectors, a distinctness check, and a
// cross-check that it differs from the IEEE CRC-32 (proving the polynomial is distinct).
static int crc32c_selftest(void) {
    if (crc32c_calc((const uint8_t*)"", 0)          != 0x00000000u) return 1;
    if (crc32c_calc((const uint8_t*)"123456789", 9) != 0xE3069283u) return 2;  // canonical
    if (crc32c_calc((const uint8_t*)"abc", 3)       != 0x364B3FB7u) return 3;
    if (crc32c_calc((const uint8_t*)"The quick brown fox jumps over the lazy dog", 43)
                                                    != 0x22620404u) return 4;
    if (crc32c_calc((const uint8_t*)"a", 1) == crc32c_calc((const uint8_t*)"b", 1)) return 5;
    if (crc32c_calc((const uint8_t*)"123456789", 9) == crc32_calc((const uint8_t*)"123456789", 9)) return 6;
    return 0;
}

// KAT for the `strings` extractor: feed a buffer with embedded printable runs and
// junk — split across TWO chunks mid-run — and check the emitted runs exactly. Covers
// the minlen threshold (a run of exactly minlen emits; one shorter is dropped), an
// embedded TAB as printable, the cross-chunk carry of an in-progress run, and the
// EOF tail flush.
static char strings_kat_out[16][STRINGS_MAX_RUN];
static int  strings_kat_n;
static void strings_emit_kat(const char* s, void* ctx) {
    (void)ctx;
    if (strings_kat_n < 16) {
        int i = 0; for (; s[i] && i < STRINGS_MAX_RUN - 1; i++) strings_kat_out[strings_kat_n][i] = s[i];
        strings_kat_out[strings_kat_n][i] = 0; strings_kat_n++;
    }
}
static int strings_selftest(void) {
    static const uint8_t in[] = {
        0x00,0x01,'H','e','l','l','o',0x00,          // "Hello" (5>=4) -> emit
        'a','b',0x00,                                 // "ab"    (2<4)  -> drop
        'W','o','r','l',                              // start of "World123"...
        'd','1','2','3',0xFF,                         // ...ends here -> "World123" (8) -> emit
        'x','\t','y','z',0x00,                        // "x\tyz" (4, TAB counts) -> emit
        'A','B','C',0x00,                             // "ABC"   (3<4)  -> drop
        'e','n','d','!'                               // "end!"  (4) at EOF -> emit on finish
    };
    static strings_state_t st;
    st.run_len = 0; st.minlen = 4; st.emit = strings_emit_kat; st.ctx = 0;
    strings_kat_n = 0;
    strings_feed(&st, in, 15);                       // chunk 1 ends mid-"World123" (run="Worl")
    strings_feed(&st, in + 15, (uint32_t)sizeof(in) - 15);
    strings_finish(&st);
    static const char* const exp[] = { "Hello", "World123", "x\tyz", "end!" };
    if (strings_kat_n != 4) return 1;
    for (int i = 0; i < 4; i++) if (strcmp(strings_kat_out[i], exp[i]) != 0) return 10 + i;
    return 0;
}

// KAT for the `sha256sum` streaming path: hash known messages fed in SMALL chunks
// (as the file streamer does) and check the hex digest. Guards the chunked
// sha256_update loop + sha256_to_hex formatting the command relies on (the one-shot
// hash is already covered by the sha256 KAT). Canonical NIST vectors.
static int sha256sum_selftest(void) {
    uint8_t dg[SHA256_DIGEST_SIZE];
    char hex[SHA256_DIGEST_SIZE * 2 + 1];
    static const uint8_t abc[] = { 'a','b','c' };
    sha256_ctx_t ctx; sha256_init(&ctx);
    sha256_update(&ctx, abc, 1); sha256_update(&ctx, abc + 1, 2);       // two chunks
    sha256_final(&ctx, dg); sha256_to_hex(dg, hex);
    if (strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0) return 1;
    static const uint8_t fox[] = "The quick brown fox jumps over the lazy dog";
    sha256_init(&ctx);
    for (uint32_t i = 0; i < 43; i += 7) { uint32_t c = (43 - i) < 7 ? (43 - i) : 7; sha256_update(&ctx, fox + i, c); }
    sha256_final(&ctx, dg); sha256_to_hex(dg, hex);
    if (strcmp(hex, "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592") != 0) return 2;
    static const uint8_t empty[1] = {0};
    sha256_init(&ctx); sha256_update(&ctx, empty, 0);                    // empty input
    sha256_final(&ctx, dg); sha256_to_hex(dg, hex);
    if (strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") != 0) return 3;
    return 0;
}

// KAT (`hmac` in the self-test battery) — pins HMAC-SHA256 (the primitive behind the `hmac`
// command, and the CSPRNG / HKDF / PBKDF2 auth path) to the RFC 4231 test vectors. Cases 1,
// 2, 4 and 6: a repeated-byte key, a short text key ("Jefe"), a sequential-byte key, and a
// key LARGER than the 64-byte block (which forces HMAC's "hash the key first" branch).
static int hmac_selftest(void) {
    uint8_t mac[SHA256_DIGEST_SIZE];
    char hex[SHA256_DIGEST_SIZE * 2 + 1];
    // TC1: key = 0x0b x20, data = "Hi There"
    static uint8_t k1[20]; for (int i = 0; i < 20; i++) k1[i] = 0x0b;
    hmac_sha256(k1, 20, (const uint8_t*)"Hi There", 8, mac); sha256_to_hex(mac, hex);
    if (strcmp(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") != 0) return 1;
    // TC2: key = "Jefe", data = "what do ya want for nothing?"
    hmac_sha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, mac);
    sha256_to_hex(mac, hex);
    if (strcmp(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") != 0) return 2;
    // TC4: key = 0x01..0x19 (25 bytes), data = 0xcd x50
    static uint8_t k4[25]; for (int i = 0; i < 25; i++) k4[i] = (uint8_t)(i + 1);
    static uint8_t d4[50]; for (int i = 0; i < 50; i++) d4[i] = 0xcd;
    hmac_sha256(k4, 25, d4, 50, mac); sha256_to_hex(mac, hex);
    if (strcmp(hex, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b") != 0) return 3;
    // TC6: key = 0xaa x131 (> 64-byte block -> key hashed first), data = 54-byte string
    static uint8_t k6[131]; for (int i = 0; i < 131; i++) k6[i] = 0xaa;
    static const uint8_t d6[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac_sha256(k6, 131, d6, sizeof(d6) - 1, mac); sha256_to_hex(mac, hex);
    if (strcmp(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54") != 0) return 4;
    return 0;
}

// KAT for the kernel's snprintf/vsnprintf — infrastructure behind 380+ call sites, so a
// formatter regression corrupts output everywhere. Covers signed/unsigned decimal (incl.
// the full 64-bit range that issue #78 truncated), hex, char/string/%%, width zero-pad,
// integer precision, and truncation to the buffer size. Pure (formats into a buffer).
static int snprintf_selftest(void) {
    char b[64];
    snprintf(b, sizeof(b), "%d", -42);                       if (strcmp(b, "-42") != 0) return 1;
    snprintf(b, sizeof(b), "%d", 0);                         if (strcmp(b, "0") != 0) return 2;
    snprintf(b, sizeof(b), "%i", 2147483647);                if (strcmp(b, "2147483647") != 0) return 3;
    // unsigned decimal — the issue #78 cases: must not truncate to 32-bit or show a sign
    snprintf(b, sizeof(b), "%u", 3000000000u);               if (strcmp(b, "3000000000") != 0) return 4;
    snprintf(b, sizeof(b), "%lu", 10000000000ul);            if (strcmp(b, "10000000000") != 0) return 5;
    snprintf(b, sizeof(b), "%llu", 1000000016000000063ull);  if (strcmp(b, "1000000016000000063") != 0) return 6;
    snprintf(b, sizeof(b), "%llu", 18446744073709551615ull); if (strcmp(b, "18446744073709551615") != 0) return 7; // 2^64-1
    snprintf(b, sizeof(b), "%u", 0u);                        if (strcmp(b, "0") != 0) return 8;
    // hex (already 64-bit-safe)
    snprintf(b, sizeof(b), "%x", 0xdeadbeefu);               if (strcmp(b, "deadbeef") != 0) return 9;
    snprintf(b, sizeof(b), "%llx", 0x1122334455667788ull);   if (strcmp(b, "1122334455667788") != 0) return 10;
    // char / string / percent
    snprintf(b, sizeof(b), "%c%c", 'h', 'i');                if (strcmp(b, "hi") != 0) return 11;
    snprintf(b, sizeof(b), "[%s]", "abc");                   if (strcmp(b, "[abc]") != 0) return 12;
    snprintf(b, sizeof(b), "%s", (char*)0);                  if (strcmp(b, "(null)") != 0) return 13;
    snprintf(b, sizeof(b), "100%%");                         if (strcmp(b, "100%") != 0) return 14;
    // width zero-pad + integer precision
    snprintf(b, sizeof(b), "%05u", 42u);                     if (strcmp(b, "00042") != 0) return 15;
    snprintf(b, sizeof(b), "%08x", 0x1234u);                 if (strcmp(b, "00001234") != 0) return 16;
    snprintf(b, sizeof(b), "%.5d", 42);                      if (strcmp(b, "00042") != 0) return 17;
    // a realistic mixed line (nyxfetch-style)
    snprintf(b, sizeof(b), "%s=%d (0x%x)", "n", 255, 255u);  if (strcmp(b, "n=255 (0xff)") != 0) return 18;
    // truncation: a 5-byte buffer holds 4 chars + NUL
    { char s[5]; snprintf(s, sizeof(s), "%d", 123456);       if (strcmp(s, "1234") != 0) return 19; }
    return 0;
}

// KAT for the shell's `!!`/`!N`/`!-N` history expansion: exercise the references, the
// out-of-range/verbatim cases (so a normal command is never mangled), and the ring-buffer
// wrap (count > HIST_MAX). Pure — feeds a synthetic history.
static int history_expand_selftest(void) {
    static char h[HIST_MAX][256];
    char out[256];
    // count <= HIST_MAX: entries at indices 0..2
    strcpy(h[0], "ls"); strcpy(h[1], "pwd"); strcpy(h[2], "echo hi");
    if (history_expand("!!",   h, 3, out, sizeof(out)) != 1 || strcmp(out, "echo hi") != 0) return 1;
    if (history_expand("!1",   h, 3, out, sizeof(out)) != 1 || strcmp(out, "ls") != 0)      return 2;
    if (history_expand("!2",   h, 3, out, sizeof(out)) != 1 || strcmp(out, "pwd") != 0)     return 3;
    if (history_expand("!3",   h, 3, out, sizeof(out)) != 1 || strcmp(out, "echo hi") != 0) return 4;
    if (history_expand("!-1",  h, 3, out, sizeof(out)) != 1 || strcmp(out, "echo hi") != 0) return 5;
    if (history_expand("!-2",  h, 3, out, sizeof(out)) != 1 || strcmp(out, "pwd") != 0)     return 6;
    // out of range / not a reference -> verbatim, return 0 (normal command untouched)
    if (history_expand("!5",    h, 3, out, sizeof(out)) != 0 || strcmp(out, "!5") != 0)      return 7;
    if (history_expand("!0",    h, 3, out, sizeof(out)) != 0 || strcmp(out, "!0") != 0)      return 8;
    if (history_expand("!",     h, 3, out, sizeof(out)) != 0 || strcmp(out, "!") != 0)       return 9;
    if (history_expand("!!x",   h, 3, out, sizeof(out)) != 0 || strcmp(out, "!!x") != 0)     return 10;
    if (history_expand("ls -l", h, 3, out, sizeof(out)) != 0 || strcmp(out, "ls -l") != 0)   return 11;
    if (history_expand("!!",    h, 0, out, sizeof(out)) != 0 || strcmp(out, "!!") != 0)       return 12; // empty history
    // ring wrap: count=12 (> HIST_MAX=10), retained window = entries 2..11, entry e at h[e%10]
    for (int e = 2; e < 12; e++) snprintf(h[e % HIST_MAX], 256, "cmd%d", e);
    if (history_expand("!!", h, 12, out, sizeof(out)) != 1 || strcmp(out, "cmd11") != 0) return 13; // entry 11 -> h[1]
    if (history_expand("!3", h, 12, out, sizeof(out)) != 1 || strcmp(out, "cmd4") != 0)  return 14; // start=2, !3 -> entry 4 -> h[4]
    if (history_expand("!1", h, 12, out, sizeof(out)) != 1 || strcmp(out, "cmd2") != 0)  return 15; // entry 2 -> h[2]
    return 0;
}

// Run the whole offline self-test battery, print a machine-readable summary, and
// halt. Triggered ONLY by the "selftest" multiboot command line (used by CI); a
// normal boot never calls this, so ordinary startup is unaffected. Each test is a
// known-answer check that needs no disk, network, or user — just a warm kernel.
static void run_selftests(void) {
    struct { const char* name; int (*fn)(void); } t[] = {
        {"sha512",       sha512_selftest},        {"pbkdf2",        pbkdf2_selftest},
        {"sha1",         sha1_selftest},           {"md5",           md5_selftest},
        {"csprng",       csprng_selftest},        {"aes_gcm",       aes_gcm_selftest},
        {"curve25519",   curve25519_selftest},     {"ed25519pub",    ed25519_selftest},
        {"ed25519vfy",   ed25519_verify_selftest}, {"filetype",      filetype_selftest},
        {"tar",          tar_selftest},            {"datefmt",       datefmt_selftest},
        {"iniparse",     ini_selftest},
        {"factor",       factor_selftest},
        {"strings",      strings_selftest},
        {"sha256sum",    sha256sum_selftest},
        {"hmac",         hmac_selftest},
        {"shellvar",     shell_expand_selftest},
        {"semver",       semver_selftest},
        {"tls_prf",      tls_prf_selftest},       {"tls_keysched",  tls_keyschedule_selftest},
        {"tls_record",   tls_record_selftest},    {"tls_ske_p384",  tls_ske_p384_selftest},
        {"der",          der_selftest},           {"base64",        base64_selftest},
        {"base32",       base32_selftest},        {"base16",        base16_selftest},
        {"base85",       base85_selftest},
        {"fnv",          fnv_selftest},
        {"csv",          csv_selftest},
        {"base58",       base58_selftest},
        {"bech32",       bech32_selftest},
        {"urlcodec",     url_codec_selftest},
        {"utf8",          utf8_selftest},
        {"p256",          p256_selftest},
        {"p384",         p384_selftest},          {"rsa",           rsa_selftest},
        {"x509",         x509_selftest},
        {"inflate",      inflate_selftest},       {"png",           png_selftest},
        {"deflate",      deflate_selftest},
        {"gzip",         gzip_selftest},
        {"bmp",          bmp_selftest},           {"gif",           gif_selftest},
        {"jpeg",         jpeg_selftest},          {"imgreject",     image_reject_selftest},
        {"httpparse",    http_parse_selftest},    {"ext2dir",       ext2_dir_selftest},
        {"pathnorm",     vfs_pathnorm_selftest},
        {"tcpcksum",     tcp_checksum_selftest},  {"dns",           dns_response_selftest},
        {"dhcpopt",      dhcp_options_selftest},
        {"mathx",        mathx_selftest},         {"crc32",         crc32_selftest},
        {"crc32c",       crc32c_selftest},
        {"fletcher",     fletcher_selftest},
        {"murmur",       murmur3_selftest},
        {"totp",         totp_selftest},          {"ipv4",          ipv4_parse_selftest},
        {"ipv6",         ipv6_parse_selftest},
        {"ipcalc",       ipcalc_selftest},
        {"calc",         calc_selftest},
        {"json",         json_selftest},
        {"json-query",   json_query_selftest},
        {"pkg-hash",     pkg_hash_selftest},
        {"ppm",          ppm_selftest},
        {"png-encode",   png_encode_selftest},
        {"auth-lockout", auth_lockout_selftest},
        {"xbm-deps",     xbm_deps_selftest},
        {"clipboard",    clipboard_selftest},
        {"term-paste",   term_paste_selftest},
        {"du",           du_selftest},
        {"pci",          pci_selftest},
        {"nvme",         nvme_selftest},
        {"nyxgrub",      nyxgrub_selftest},
        {"gpt",          gpt_selftest},
        {"fat",          fat_selftest},
        {"mbr",          mbr_selftest},
        {"mbrbuild",     mbr_build_selftest},
        {"mkfs-ext2",    ext2_format_selftest},
        {"mkfs-mg",      ext2_format_mg_selftest},
        {"cptree",       cptree_walk_selftest},
        {"notify",       notify_selftest},
        {"stack-canary", stack_canary_selftest},
        {"kfree",        kfree_selftest},
        {"elf",          elf_selftest},
        {"numparse",     numparse_selftest},        {"hkdf",          hkdf_selftest},
        {"snprintf",     snprintf_selftest},
        {"histexpand",   history_expand_selftest},
        {"chacha20",     chacha20_selftest},        {"siphash",       siphash_selftest},
        {"poly1305",     poly1305_selftest},        {"chachapoly",    chacha20poly1305_selftest},
        {"blake2s",      blake2s_selftest},         {"cmac",          aes_cmac_selftest},
        {"aesctr",       aes_ctr_selftest},         {"namecheck",     namecheck_selftest},
        {"ansicsi",      ansi_csi_selftest},        {"caldate",       caldate_selftest},
        {"sha3",         sha3_selftest},           {"ct",            ct_selftest},
        {"aescbc",       aes_cbc_selftest},        {"urlparse",      url_parse_selftest},
        {"aeskw",        aes_kw_selftest},
        {"glob",         glob_selftest},
        {"leb128",       leb128_selftest},        {"crc16",         crc16_selftest},
    };
    int n = (int)(sizeof(t) / sizeof(t[0])), passed = 0, failed = 0;
    serial_puts("SELFTEST-BEGIN\n");
    for (int i = 0; i < n; i++) {
        int rc = t[i].fn();
        if (rc == 0) { passed++; printf("[SELFTEST] %-12s PASS\n", t[i].name); }
        else         { failed++; printf("[SELFTEST] %-12s FAIL (rc=%d)\n", t[i].name, rc); }
    }
    printf("SELFTEST-SUMMARY passed=%d failed=%d total=%d\n", passed, failed, n);
    serial_puts("SELFTEST-END\n");
    for (;;) __asm__ volatile ("cli; hlt");   // halt so CI can read the log and stop QEMU
}

void kernel_main(uint64_t magic, void* mboot_ptr) {
    (void)magic;
    saved_mboot_ptr = mboot_ptr;
    saved_mboot_magic = magic;
    int selftest_mode = 0;   // set from the "selftest" multiboot command line (CI)
    int rotate_deg = 0;      // set from a "rotate=90|180|270" cmdline (portrait-mounted panels)
    init_screen();
    clear_screen();

    printf("[INIT] Global Descriptor Table...\n"); init_gdt();
    printf("[INIT] Interrupt Descriptor Table...\n"); init_idt();
    printf("[INIT] Interrupt Service Routines...\n"); init_isr();
    printf("[INIT] Interrupt Requests...\n"); init_irq();
    printf("[INIT] Serial Port...\n"); init_serial();
    printf("[INIT] SSE/FPU for userspace...\n"); enable_sse_fpu();

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
                if (type == 1) {
                    // Boot command line (multiboot2 type 1): a NUL-terminated string
                    // after the 8-byte tag header. A "selftest" token runs the offline
                    // self-test battery before login (CI); normal boots pass no cmdline.
                    if (strstr((const char*)(tag + 8), "selftest")) selftest_mode = 1;
                    const char* rp = strstr((const char*)(tag + 8), "rotate=");
                    if (rp) rotate_deg = atoi(rp + 7);
                } else if (type == 4) {
                    uint32_t mem_lower = *(uint32_t*)(tag + 8);
                    uint32_t mem_upper = *(uint32_t*)(tag + 12);
                    mem_total = (uint64_t)(mem_lower + mem_upper) * 1024;
                } else if (type == 6) {
                    // Memory map tag: u32 entry_size, u32 entry_version, then entries of
                    // { u64 base, u64 length, u32 type, u32 reserved }. Copy them out for
                    // init_memory (base+len at e+0/e+8, type at e+16; stride = entry_size).
                    uint32_t entry_size = *(uint32_t*)(tag + 8);
                    uint8_t* e = tag + 16;
                    uint8_t* tag_end = tag + size;
                    while (entry_size >= 20 && e + entry_size <= tag_end && g_mmap_count < MAX_MMAP) {
                        g_mmap[g_mmap_count].base = *(uint64_t*)(e + 0);
                        g_mmap[g_mmap_count].len  = *(uint64_t*)(e + 8);
                        g_mmap[g_mmap_count].type = *(uint32_t*)(e + 16);
                        g_mmap_count++;
                        e += entry_size;
                    }
                } else if (type == 8) {
                    // Framebuffer info tag: the LINEAR framebuffer the bootloader (GRUB)
                    // set up in response to our type-5 request — this is how graphics works
                    // on real hardware / UEFI (GOP), where the QEMU Bochs-VBE ports don't
                    // exist. u64 addr, u32 pitch, u32 width, u32 height, u8 bpp, u8 fb_type.
                    grub_fb_addr  = *(uint64_t*)(tag + 8);
                    grub_fb_pitch = *(uint32_t*)(tag + 16);
                    grub_fb_w     = *(uint32_t*)(tag + 20);
                    grub_fb_h     = *(uint32_t*)(tag + 24);
                    grub_fb_bpp   = *(uint8_t*)(tag + 28);
                    grub_fb_type  = *(uint8_t*)(tag + 29);
                    // For a direct-RGB framebuffer (fb_type==1) the color_info follows the common
                    // header. NOTE: the field after framebuffer_type is a u16 `reserved` (offset
                    // 30-31), so color_info starts at +32, not +31:
                    // { u8 red_pos, u8 red_size, u8 green_pos, u8 green_size, u8 blue_pos,
                    //   u8 blue_size }. Guarded on the tag being big enough to hold it.
                    if (grub_fb_type == 1 && size >= 38) {
                        grub_fb_red_pos   = *(uint8_t*)(tag + 32);
                        grub_fb_red_sz    = *(uint8_t*)(tag + 33);
                        grub_fb_green_pos = *(uint8_t*)(tag + 34);
                        grub_fb_green_sz  = *(uint8_t*)(tag + 35);
                        grub_fb_blue_pos  = *(uint8_t*)(tag + 36);
                        grub_fb_blue_sz   = *(uint8_t*)(tag + 37);
                    }
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
    printf("[INIT] Physical Memory Manager...\n"); init_memory(mem_total, g_mmap, g_mmap_count);

    printf("[INIT] Paging...\n"); init_paging();
    printf("[INIT] CPU protections (SMEP/SMAP)...\n"); enable_smep_smap();

    nyxfetch();
    for (volatile long i = 0; i < 200000000; i++);
    clear_screen();
    printf("[INIT] APIC...\n"); init_apic();
    printf("[INIT] Kernel Heap...\n"); init_heap();
    printf("[INIT] Slab Allocator...\n"); slab_init_all();
    printf("[INIT] SMP...\n"); smp_init();
    fb_set_rotation(rotate_deg);   // portrait-panel display rotation (0 unless "rotate=" on the cmdline)
    if (grub_fb_addr && grub_fb_type == 1 && grub_fb_bpp == 32) {
        // REAL-HARDWARE / UEFI path: GRUB (via GOP, or VBE under BIOS) already set a linear
        // framebuffer and handed it to us in the multiboot2 type-8 tag. Map it and use it
        // directly — no QEMU-only Bochs-VBE poking. (QEMU also fills this tag now, so this same
        // path is what the headless tests exercise.)
        printf("[INIT] GRUB framebuffer %ux%ux%u pitch=%u at 0x%llx\n",
               grub_fb_w, grub_fb_h, grub_fb_bpp, grub_fb_pitch, grub_fb_addr);
        printf("[INIT] GRUB fb pixel format: R@%u/%u G@%u/%u B@%u/%u\n",
               grub_fb_red_pos, grub_fb_red_sz, grub_fb_green_pos, grub_fb_green_sz,
               grub_fb_blue_pos, grub_fb_blue_sz);
        // NyxOS renders as BGRX (blue@0, green@8, red@16). If the display reports anything else
        // (some non-Intel GOPs are RGBX), on-screen reds and blues will be swapped — flag it so a
        // blind physical bringup knows why, without changing the render path (untested on real HW).
        if (!(grub_fb_red_pos == 16 && grub_fb_green_pos == 8 && grub_fb_blue_pos == 0))
            printf("[WARN] fb is not BGRX(R16/G8/B0); red/blue may be swapped on this panel\n");
        void* lfb = vbe_map_lfb(grub_fb_addr, grub_fb_pitch * grub_fb_h);
        fb_init_ex(grub_fb_w, grub_fb_h, grub_fb_bpp, lfb, grub_fb_pitch / 4);
        fb_debug_banner();                 // paint a Nyx bar NOW — real-HW proof of life
        // + an audible "graphics up" chirp for blind bringup. Interrupts are still OFF here, so
        // this must NOT use sleep()/the timer (which would hang forever) — a raw busy-loop blip.
        speaker_on(880);
        for (volatile long i = 0; i < 20000000; i++) { }
        speaker_off();
        bootsplash_init();
        printf("[INIT] Framebuffer (GRUB): %ux%ux%u at 0x%llx\n",
               grub_fb_w, grub_fb_h, grub_fb_bpp, (uint64_t)(uintptr_t)lfb);
    } else {
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
    }
    bootsplash_update(1, 23, "Setting up exception stacks...");
    printf("[INIT] IST stacks (per-CPU)...\n");
    // One double-fault + one NMI IST stack PER online core, so a fault on one core
    // can't scribble on another core's exception stack (smp_init ran above, so
    // cpu_count is known). IST stacks grow down, hence the stack TOP is the high
    // end of each 8 KB region.
    uint32_t nist = cpu_count ? cpu_count : 1;
    void* ist_pages = kmalloc(IST_STACK_SIZE * 2 * nist);
    if (ist_pages) {
        uint64_t base = (uint64_t)ist_pages;
        for (uint32_t c = 0; c < nist; c++) {
            uint64_t df  = base + (uint64_t)IST_STACK_SIZE * (2 * c + 1);   // top of CPU c's DF stack
            uint64_t nmi = base + (uint64_t)IST_STACK_SIZE * (2 * c + 2);   // top of CPU c's NMI stack
            tss_set_ist_cpu(c, IST_DOUBLE_FAULT, df  + KERNEL_BASE);
            tss_set_ist_cpu(c, 2,                nmi + KERNEL_BASE);
        }
        // Log CPU0 and the last CPU's DF stack so the boot record shows they differ.
        printf("[INIT] IST stacks: %u CPU(s) x 2 (DF+NMI); CPU0 DF=0x%lx CPU%u DF=0x%lx\n",
               nist, base + IST_STACK_SIZE + KERNEL_BASE,
               nist - 1, base + (uint64_t)IST_STACK_SIZE * (2 * (nist - 1) + 1) + KERNEL_BASE);
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
    smp_start_ap_threads();   // one kernel thread pinned to each application processor
    bootsplash_update(5, 23, "Creating idle process...");
    printf("[INIT] System Calls...\n"); init_syscalls();
    bootsplash_update(6, 23, "Initializing system calls...");
    // The BSP's own GS base, before anything can issue a `syscall`: syscall_entry
    // reaches its per-CPU slots through GS and has no register to spare.
    cpu_install_gs_base(0);
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
    printf("[INIT] TCP...\n"); tcp_init(); tcp_echo_init();
    bootsplash_update(12, 23, "Initializing TCP...");
    // Auto-DHCP: if a NIC came up, lease an IP now so the network is ready for
    // everything (Selene, httpget, ping) without a manual `dhcp`. dhcp_request drives
    // the net itself and returns fast when a server answers; skipped entirely when no
    // NIC is present (diskless/netless boot), so it never stalls the boot.
    for (int ai = 0; ai < 8; ai++) {
        if (net_interfaces[ai].name[0] && strcmp(net_interfaces[ai].name, "lo") != 0) {
            bootsplash_update(12, 23, "Requesting an IP via DHCP...");
            printf("[INIT] Auto-DHCP on %s...\n", net_interfaces[ai].name);
            dhcp_request(ai);
            break;
        }
    }
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

        // Load the shared libc (/libc.so) before any user process is created, so
        // elf_load_image can map it into each of them.
        shared_libc_load();

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
            // Mount cleanly — do NOT scribble a test file onto the user's persistent
            // disk on every boot (this used to write /mnt/os-test.txt each time). The
            // write path (create/write/mkdir/unlink, flushed to disk by the VFS) is
            // exercised by the coreutils; files under /mnt survive reboots. `df`
            // reports free space.
            printf("[EXT2] Mounted /mnt (writable, persistent) - %u KB free.\n",
                   ext2_fs.sb.free_blocks * (ext2_fs.block_size / 1024));
        }
    } else {
        printf("[EXT2] No EXT2 filesystem found.\n");
    }

    bootsplash_update(23, 23, "Launching desktop...");
    if (vbe_get_lfb()) {
        bootsplash_clear();
        printf("[AUTH] Setting up user accounts...\n");
        auth_setup();
        // Register the compositor as a scheduler process once; the boot thread runs
        // its loop directly (compositor_run below).
        process_t* comp_proc = create_process("compositor", compositor_run, 0);
        if (comp_proc) {
            comp_proc->sched_weight = SCHED_WEIGHT_GUI;   // GUI gets priority over jobs
            for (int i = 0; i < process_count; i++) {
                if (process_table[i] == comp_proc) { current_idx = i; break; }
            }
        }
        // Login -> desktop, looping on logout: the user-badge menu's "Log out" makes
        // compositor_run() return with compositor_logout_requested set, so we tear the
        // desktop down (compositor_init frees its windows) and re-show the login — no
        // reboot. Any other exit (Shutdown) leaves the loop and reboots.
        // CI hook: with the "selftest" multiboot cmdline, run the self-test battery
        // (crypto + image decoders) and halt, instead of showing the login screen.
        if (selftest_mode) run_selftests();

        for (;;) {
            fb_use_lfb_direct();          // login draws straight to the LFB (no back buffer)
            printf("[LOGIN] Starting login screen...\n");
            login_screen();               // blocks until a successful login
            printf("[LOGIN] Login successful, starting desktop...\n");
            compositor_init();
            serial_puts("[COMP] calling compositor_run (Hemera)\n");
            compositor_run();
            if (!compositor_logout_requested) break;
            compositor_logout_requested = 0;
            printf("[LOGIN] Logout — returning to the login screen.\n");
        }
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
// Render one row of the 16-colour terminal palette as fastfetch-style blocks: 8 blocks
// of 3 cells each, aligned under the info column. The GUI terminal parses ANSI, so emit
// background SGR (40-47 normal / 100-107 bright); the boot VGA-text console does NOT parse
// escapes, so set the cell colour via set_terminal_color() and print spaces. A coloured
// SPACE is the only way to fill a cell — the write path drops glyphs >= 0x80, so a real
// block character would vanish (same reason the logo is an ASCII shade ramp).
static void nyxfetch_block_row(int gui, int bright) {
    static const uint8_t a2v[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };   // ANSI colour index -> VGA nibble
    for (int s = 0; s < NYX_LOGO_COLS + 2; s++) putchar(' ');   // indent under the info column
    for (int i = 0; i < 8; i++) {
        if (gui) {
            printf("\x1b[%dm   ", (bright ? 100 : 40) + i);
        } else {
            uint8_t bg = bright ? (uint8_t)(a2v[i] | 0x08) : a2v[i];
            set_terminal_color(vga_entry_color(VGA_BLACK, (enum vga_color)bg));
            printf("   ");
        }
    }
    if (gui) printf("\x1b[0m");
    else     set_terminal_color(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
    putchar('\n');
}

// Write one info line: the colour-tinted label left-padded to 12 columns, then the value.
// (vsnprintf has no "%-12s" left-justify, so pad the label by hand.)
static void nyxfetch_field(char* dst, const char* key_c, const char* rst_c,
                           const char* label, const char* value) {
    char lab[16]; int ll = 0;
    while (label[ll] && ll < 12) { lab[ll] = label[ll]; ll++; }
    while (ll < 12) lab[ll++] = ' ';
    lab[ll] = '\0';
    snprintf(dst, 96, "%s%s%s%s", key_c, lab, rst_c, value);
}

void nyxfetch(void) {
    clear_screen();

    /* The GUI terminal colorizes via ANSI SGR; the boot/VGA console does not
     * parse escapes (they would render as garbage). So emit ANSI only once the
     * compositor is up, and tint the boot splash with set_terminal_color(). */
    int gui = compositor_is_running();
    const char* LOGO_C = gui ? "\x1b[95m" : "";   /* fox: NyxOS purple      */
    const char* KEY_C  = gui ? "\x1b[95m" : "";   /* field labels: purple   */
    const char* ACC_C  = gui ? "\x1b[95m" : "";   /* accents: user@host     */
    const char* RST_C  = gui ? "\x1b[0m"  : "";
    set_terminal_color(vga_entry_color(VGA_LIGHT_MAGENTA, VGA_BLACK));

    /* The NyxOS crescent-moon logo (shared with the login screen — see
     * kernel/nyx_logo.h), an ASCII shade ramp (" .:o#"), 27 cols x 14 rows. */
    const char** fox = NYX_LOGO;
    const int FOX_H = NYX_LOGO_ROWS;

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
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", IP4_OCTETS(ip));
            break;
        }
    }

    char disk_str[48] = "Not mounted";
    if (ext2_fs.block_size > 0) {
        uint64_t total_kb = (uint64_t)ext2_fs.sb.total_blocks * ext2_fs.block_size / 1024;
        uint64_t free_kb = (uint64_t)ext2_fs.sb.free_blocks * ext2_fs.block_size / 1024;
        snprintf(disk_str, sizeof(disk_str), "%llu KiB / %llu KiB", free_kb, total_kb);
    }

    const char* keymap = keyboard_layout == 0 ? "US (QWERTY)" : "Spanish (ES)";

    /* ---- Build the info column (index-aligned with the fox rows). fastfetch layout:
       user@host, a separator, then label:value fields. `info` is static (not on the
       4 KB kernel stack — this array alone is ~1.9 KB). ---- */
    static char info[20][96];
    char val[80];
    int n = 0;
    snprintf(info[n++], 96, "%snyx%s@%snyxos%s", ACC_C, RST_C, ACC_C, RST_C);
    snprintf(info[n++], 96, "%s---------%s", ACC_C, RST_C);   /* separator ~ width of "nyx@nyxos" */
    snprintf(val, sizeof(val), "%s x86_64", KERNEL_NAME);
    nyxfetch_field(info[n++], KEY_C, RST_C, "OS:", val);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Host:", "QEMU Standard PC");
    snprintf(val, sizeof(val), "%s %s (%s)", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Kernel:", val);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Uptime:", uptime);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Shell:", "nyxsh");
    nyxfetch_field(info[n++], KEY_C, RST_C, "Resolution:", res_str);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Desktop:", COMPOSITOR_NAME);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Terminal:", TERMINAL_NAME);
    snprintf(val, sizeof(val), "%s (%d)", cpu_brand, cpu_count);
    nyxfetch_field(info[n++], KEY_C, RST_C, "CPU:", val);
    snprintf(val, sizeof(val), "%llu / %llu MiB (%u%%)",
             free_mem / (1024*1024), memory_total / (1024*1024), mem_pct);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Memory:", val);
    snprintf(val, sizeof(val), "%d", process_count);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Processes:", val);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Disk:", disk_str);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Network:", ip_str);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Keymap:", keymap);
    snprintf(val, sizeof(val), "%u-%02u-%02u %02u:%02u:%02u",
             (unsigned int)rtc.year, (unsigned int)rtc.month, (unsigned int)rtc.day,
             (unsigned int)rtc.hour, (unsigned int)rtc.minute, (unsigned int)rtc.second);
    nyxfetch_field(info[n++], KEY_C, RST_C, "Time:", val);

    /* ---- Render: fox on the left, facts on the right, line by line ---- */
    static const char* blank_row = "                           "; /* 27 spaces */
    int rows = n > FOX_H ? n : FOX_H;
    for (int i = 0; i < rows; i++) {
        const char* l = (i < FOX_H) ? fox[i] : blank_row;
        const char* r = (i < n)     ? info[i] : "";
        printf("%s%s%s  %s\n", LOGO_C, l, RST_C, r);
    }
    /* ---- the fastfetch palette blocks: two rows (normal + bright) under the info ---- */
    putchar('\n');
    nyxfetch_block_row(gui, 0);   /* ANSI colours 0-7  */
    nyxfetch_block_row(gui, 1);   /* ANSI colours 8-15 */
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

        // bash-style history expansion (!!, !N, !-N). A no-op for normal commands, so
        // ordinary input is untouched; on a hit, echo the expanded line (like bash) and
        // run + store that instead of the reference.
        {
            char hx[256];
            if (history_expand(cmd_line, history, hist_count, hx, sizeof(hx))) {
                printf("%s\n", hx);
                strncpy(cmd_line, hx, sizeof(cmd_line) - 1);
                cmd_line[sizeof(cmd_line) - 1] = '\0';
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
                char* argv[MAX_CMD_ARGS];
                static char expanded[MAX_CMD_ARGS][256];   /* static: the shell parse is serialised, keeps 8 KB off the stack */
                int argc = 0;
                char* token = strtok(cmd_line, " ");
                while (token != NULL && argc < MAX_CMD_ARGS) {
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
                if (token != NULL) {          /* more tokens than the cap: report, don't silently drop */
                    set_terminal_color(vga_entry_color(VGA_LIGHT_RED, VGA_BLACK));
                    printf("Too many arguments (max %d)\n", MAX_CMD_ARGS);
                } else if (argc > 0) {
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
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
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
                long long val = long_flag ? va_arg(args, long long ) : (long long)va_arg(args, int);
                char tmp[24];
                int neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                if (long_flag) { lltoa(val, tmp, 10); }
                else { itoa((int)val, tmp, 10); }
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
                // 64-bit-safe unsigned decimal + width zero-padding. itoa((int)val)
                // truncated to 32 bits (and printed a spurious '-' above INT_MAX);
                // build the digits from the full unsigned value, like %x does (issue #78).
                char tmp[24]; int ti = 0;
                if (val == 0) tmp[ti++] = '0';
                else { char rev[24]; int ri = 0; unsigned long v = val;
                       while (v) { rev[ri++] = (char)('0' + (int)(v % 10)); v /= 10; }
                       while (ri) tmp[ti++] = rev[--ri]; }
                tmp[ti] = '\0';
                if (width > ti) { int pad = width - ti; while (pad-- > 0 && written < (int)size - 1) { *p++ = '0'; written++; } }
                char *t = tmp;
                while (*t && written < (int)size - 1) { *p++ = *t++; written++; }
                fmt++;
            } else if (*fmt == 'x' || *fmt == 'X') {
                unsigned long val = long_flag ? va_arg(args, unsigned long) : (unsigned long)va_arg(args, unsigned int);
                // 64-bit-safe hex (itoa truncates to int) + width zero-padding.
                char tmp[24]; int ti = 0;
                if (val == 0) tmp[ti++] = '0';
                else { char rev[24]; int ri = 0; unsigned long v = val;
                       while (v) { int d = (int)(v & 0xF); rev[ri++] = d < 10 ? (char)('0'+d) : (char)('a'+d-10); v >>= 4; }
                       while (ri) tmp[ti++] = rev[--ri]; }
                tmp[ti] = '\0';
                if (width > ti) { int pad = width - ti; while (pad-- > 0 && written < (int)size - 1) { *p++ = '0'; written++; } }
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
    return written;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf, size, fmt, args);
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

// The boot.asm identity map covers the low 128 MB of physical RAM (64 * 2 MB pages); a
// bootloader buffer above that can't be reached via its physical address without a fault.
// Real UEFI firmware places GRUB modules high in RAM (GBs), unlike QEMU which packs
// everything low — so every bootloader-provided module pointer must be bounds-checked.
#define MB_IDMAP_LIMIT 0x08000000ULL   // 128 MB

static void module_register(uint32_t mod_start, uint32_t mod_end, const char* name, uint32_t idx) {
    if (mod_end < mod_start) return;
    if ((uint64_t)mod_start >= MB_IDMAP_LIMIT || (uint64_t)mod_end > MB_IDMAP_LIMIT) {
        printf("[MODULES] skip module%u at 0x%x (above the 128 MB identity map)\n", idx, mod_start);
        return;
    }
    char path[64];
    if (name && (uintptr_t)name < MB_IDMAP_LIMIT && *name)
        snprintf(path, sizeof(path), "/%s", name);
    else
        snprintf(path, sizeof(path), "/boot/module%u", idx);
    // COPY the module into the heap. GRUB's module memory is NOT reserved from the
    // PMM, so it gets handed back out and overwritten as the kernel runs — a VFS
    // file pointing straight at it (vfs_create_from_mem just stores the pointer)
    // would read corrupt data later: a multi-MB `module` rotted byte-by-byte, so
    // copying it to an installed disk produced a garbage kernel. A heap copy taken
    // here (early boot, module still intact) is stable. Fall back to referencing
    // only if the copy can't be allocated.
    uint32_t sz = mod_end - mod_start;
    uint8_t* copy = (uint8_t*)kmalloc(sz ? sz : 1);
    if (copy) {
        memcpy(copy, (uint8_t*)(uintptr_t)mod_start, sz);
        vfs_create_from_mem(path, copy, sz);
    } else {
        vfs_create_from_mem(path, (uint8_t*)(uintptr_t)mod_start, sz);
    }
}

void init_load_modules(void) {
    if (!saved_mboot_ptr) return;
    if (saved_mboot_magic == 0x36d76289) {
        // Multiboot2: walk the tag list for MODULE tags (type 3):
        //   u32 type=3, u32 size, u32 mod_start, u32 mod_end, char string[] (inline).
        // The old code read this mb2 struct as if it were multiboot1 (mods at mb[5]/mb[6],
        // which are garbage here) and on real UEFI hardware dereferenced a bogus ~1.7 GB
        // module pointer — a #PF panic. QEMU only escaped it because mb2 total_size & 8 == 0.
        uint8_t* tag = (uint8_t*)saved_mboot_ptr + 8;
        uint32_t idx = 0;
        for (;;) {
            uint32_t type = *(uint32_t*)tag;
            uint32_t size = *(uint32_t*)(tag + 4);
            if (type == 0 || size < 8) break;
            if (type == 3 && size >= 16)
                module_register(*(uint32_t*)(tag + 8), *(uint32_t*)(tag + 12),
                                (const char*)(tag + 16), idx++);
            tag += (size + 7) & ~7u;
        }
    } else if (saved_mboot_magic == 0x2BADB002) {
        // Multiboot1: flags bit 3 -> mods_count @ mb[5], mods_addr @ mb[6]; each entry is
        // { u32 mod_start, u32 mod_end, u32 string_ptr, u32 reserved }.
        uint32_t* mb = (uint32_t*)saved_mboot_ptr;
        if (!(mb[0] & 8)) return;
        uint32_t mods_count = mb[5];
        uint32_t mods_addr  = mb[6];
        if ((uint64_t)mods_addr >= MB_IDMAP_LIMIT) return;
        uint32_t* mod_entry = (uint32_t*)(uintptr_t)mods_addr;
        for (uint32_t i = 0; i < mods_count && i < 256; i++) {
            module_register(mod_entry[0], mod_entry[1], (const char*)(uintptr_t)mod_entry[2], i);
            mod_entry += 4;
        }
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