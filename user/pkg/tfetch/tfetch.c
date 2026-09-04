#include "libc.h"

/* tfetch — "tubular fetch": a little system-info fetch with a bunny and pride-flag
 * colours. A C reimplementation for NyxOS of Parker0312's Rust `tfetch`
 * (github.com/Parker0312/tfetch), ported with the author's permission — see
 * THIRD-PARTY-NOTICES. It prints a bunny beside Host / OS / CPU / Memory / Uptime;
 * with a `--<flag>` argument it recolours the bunny and labels in that pride flag's
 * palette (a gradient down the art). No argument prints it plain.
 *
 *   tfetch              plain
 *   tfetch --trans      trans-flag colours
 *   tfetch --gay        rainbow, etc.
 *
 * NyxOS adaptations from the original: the bunny is the same art re-rendered in CP437
 * half-blocks (the console font has no Unicode braille); colours are the terminal's
 * 16-colour ANSI set (no 24-bit); info
 * comes from NyxOS's /proc (version/cpuinfo/meminfo/uptime), and Disk (which needs a
 * statfs the ring-3 libc doesn't have yet) is shown as Uptime instead. Built INSIDE
 * the OS by `cc`; installs via `xbm install tfetch`.
 */

/* ---- the bunny ---- The upstream art is Unicode braille, which the CP437 console
 * font can't draw; this is the SAME bunny decoded to a dot bitmap and re-rendered with
 * CP437 half-block glyphs (0xDB full, 0xDF upper, 0xDC lower), which the font does have.
 * Generated from the original braille, so the shape is faithful. */
static const char* BUNNY[] = {
    "   \xDC\xDB\xDB\xDB   \xDC\xDC\xDC",
    "  \xDC\xDB  \xDB \xDC\xDB\xDF \xDB",
    "  \xDB   \xDB\xDB\xDF  \xDC\xDB\xDB\xDB\xDB\xDC\xDC",
    "  \xDB \xDB\xDB\xDB\xDF\xDC \xDC\xDB     \xDF\xDF\xDB",
    " \xDC\xDB\xDB\xDB\xDB\xDC\xDB\xDB\xDB\xDF        \xDF\xDB\xDC\xDC",
    "\xDC\xDB\xDF   \xDF \xDF           \xDB\xDC\xDB\xDC",
    "\xDB           \xDF       \xDC\xDB\xDB",
    "\xDB          \xDC\xDB      \xDC\xDB\xDF",
    "\xDB\xDB   \xDC\xDC\xDC   \xDF  \xDC\xDC \xDC\xDB\xDB\xDF",
    "\xDB\xDB\xDC\xDC \xDF\xDB\xDF\xDC\xDC\xDB\xDB\xDC\xDB\xDB\xDB\xDB\xDF\xDF",
    "  \xDF\xDF\xDF\xDF\xDF\xDF\xDF",
};
#define NBUNNY ((int)(sizeof(BUNNY) / sizeof(BUNNY[0])))

static const char* SEP = "~*~*~*~*~ * ~*~*~*~*~";

/* A flag = an ordered list of 16-colour ANSI SGR codes (30-37 normal, 90-97 bright);
 * the original's 24-bit colours are mapped to the nearest of the 16. */
typedef struct { const char* names; unsigned char codes[8]; int n; } flag_t;
static const flag_t FLAGS[] = {
    {"nonbinary",              {33, 37, 95, 90},             4},
    {"boyflux",                {96, 94, 34, 92, 34, 96},     6},
    {"girlflux",               {93, 95, 31, 33, 31, 95, 93}, 7},
    {"trans",                  {36, 95, 37, 95, 36},         5},
    {"lesbian",                {91, 93, 37, 95, 35},         5},
    {"gay",                    {91, 33, 33, 32, 94, 35},     6},
    {"bi bisexual",            {95, 35, 94},                 3},
    {"pan pansexual",          {91, 33, 96},                 3},
    {"ace asexual",            {90, 97, 95, 37},             4},
    {"aro aromantic",          {32, 92, 37, 90, 90},         5},
    {"genderfluid genderflux", {95, 37, 35, 90, 94},         5},
    {"agender",                {90, 37, 37, 93, 37, 37, 90}, 7},
    {"demiboy",                {90, 94, 94, 37},             4},
    {"demigirl",               {90, 95, 95, 37},             4},
    {"bigender",               {95, 37, 94},                 3},
    {"genderqueer",            {95, 37, 92},                 3},
    {"femboy",                 {95, 95, 37, 96, 95, 95},     6},
    {"tomboy",                 {94, 96, 37, 33, 91},         5},
    {"intersex",               {33, 37, 95},                 3},
    {"demisexual",             {90, 95, 37, 95, 90},         5},
    {"demiromantic",           {90, 91, 37, 91, 90},         5},
    {"polygender",             {95, 33, 33, 92, 94},         5},
    {"polyamorous",            {91, 94, 90},                 3},
    {"omnisexual",             {91, 33, 33, 35, 95},         5},
    {"queer",                  {35, 37, 92},                 3},
    {"questioning",            {95, 33, 33, 37},             4},
    {"two-spirit",             {91, 93},                     2},
    {"demigender",             {90, 37, 37, 37, 90},         5},
};
#define NFLAGS ((int)(sizeof(FLAGS) / sizeof(FLAGS[0])))

/* whitespace-delimited word `name` present in the space-separated list `names`? */
static int name_in(const char* names, const char* name) {
    int nl = (int)strlen(name);
    const char* p = names;
    while (*p) {
        const char* start = p;
        while (*p && *p != ' ') p++;
        if ((int)(p - start) == nl && strncmp(start, name, nl) == 0) return 1;
        while (*p == ' ') p++;
    }
    return 0;
}

/* Read a whole (small) file into buf, NUL-terminated. Returns length, -1 on error. */
static int read_all(const char* path, char* buf, int cap) {
    int fd = (int)open(path, 0, 0);
    if (fd < 0) return -1;
    int n = (int)read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) n = 0;
    buf[n] = '\0';
    return n;
}

/* Value after the first ':' on the line beginning with `prefix` (trimmed), into out. */
static void proc_field(const char* text, const char* prefix, char* out, int outsz) {
    out[0] = '\0';
    int pl = (int)strlen(prefix);
    for (const char* p = text; *p; ) {
        const char* eol = p; while (*eol && *eol != '\n') eol++;
        if (strncmp(p, prefix, pl) == 0) {
            const char* c = p; while (c < eol && *c != ':') c++;
            if (c < eol) { c++; while (c < eol && (*c == ' ' || *c == '\t')) c++;
                int i = 0; while (c < eol && i < outsz - 1) out[i++] = *c++; out[i] = '\0'; }
            return;
        }
        p = (*eol == '\n') ? eol + 1 : eol;
    }
}

int main(int argc, char** argv) {
    char host[64], os[128], cpu[64], mem[64], up[64];
    char pv[256], ci[256], mi[256], ut[64];

    strcpy(host, "nyxos");

    read_all("/proc/version", pv, sizeof pv);
    { int i = 0; while (pv[i] && pv[i] != '\n' && i < (int)sizeof(os) - 1) { os[i] = pv[i]; i++; } os[i] = '\0'; }
    if (!os[0]) strcpy(os, "NyxOS x86_64");

    read_all("/proc/cpuinfo", ci, sizeof ci);
    proc_field(ci, "arch", cpu, sizeof cpu);
    if (!cpu[0]) strcpy(cpu, "x86_64");

    read_all("/proc/meminfo", mi, sizeof mi);
    { char t[32], u[32]; proc_field(mi, "MemTotal", t, sizeof t); proc_field(mi, "MemUsed", u, sizeof u);
      unsigned long tk = strtoul(t, 0, 10), uk = strtoul(u, 0, 10);
      snprintf(mem, sizeof mem, "%lu / %lu MiB", uk / 1024, tk / 1024); }

    read_all("/proc/uptime", ut, sizeof ut);
    { unsigned long s = strtoul(ut, 0, 10);
      if (s >= 3600) snprintf(up, sizeof up, "%luh %lum", s / 3600, (s % 3600) / 60);
      else           snprintf(up, sizeof up, "%lum %lus", s / 60, s % 60); }

    const char* labels[5] = { "Host:", "OS:", "CPU:", "Memory:", "Uptime:" };
    const char* values[5] = { host, os, cpu, mem, up };

    const flag_t* flag = 0;
    if (argc >= 2) {
        const char* a = argv[1];
        while (*a == '-') a++;                      /* strip leading dashes */
        char name[32]; int i = 0;
        for (; a[i] && i < (int)sizeof(name) - 1; i++)
            name[i] = (a[i] >= 'A' && a[i] <= 'Z') ? a[i] + 32 : a[i];   /* lowercase */
        name[i] = '\0';
        for (int f = 0; f < NFLAGS; f++) if (name_in(FLAGS[f].names, name)) { flag = &FLAGS[f]; break; }
        if (!flag) {
            printf("Unknown flag: %s. Available:\n", name);
            for (int f = 0; f < NFLAGS; f++) {
                char first[24]; int j = 0;
                while (FLAGS[f].names[j] && FLAGS[f].names[j] != ' ' && j < 23) { first[j] = FLAGS[f].names[j]; j++; }
                first[j] = '\0';
                printf("%s--%s", (f % 6 == 0) ? "  " : " ", first);
                if (f % 6 == 5) printf("\n");
            }
            printf("\n");
            return 1;
        }
    }

    if (!flag) {                                    /* plain */
        for (int i = 0; i < NBUNNY; i++) printf("%s\n", BUNNY[i]);
        printf("%s\n\n", SEP);
        for (int i = 0; i < 5; i++) printf("%s %s\n", labels[i], values[i]);
        return 0;
    }

    for (int i = 0; i < NBUNNY; i++) {              /* coloured by flag gradient */
        int c = (i * flag->n) / NBUNNY;
        printf("\x1b[%dm%s\x1b[0m\n", flag->codes[c], BUNNY[i]);
    }
    printf("%s\n\n", SEP);
    for (int i = 0; i < 5; i++) {
        int c = (i * flag->n) / 5;
        printf("\x1b[%dm%s\x1b[0m %s\n", flag->codes[c], labels[i], values[i]);
    }
    return 0;
}
