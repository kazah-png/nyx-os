// ============================================================
// filetype.c - file(1)-style type identification (see filetype.h).
// Content magic numbers win over the name; text is then split into ASCII / UTF-8 /
// binary "data" (reusing the strict utf8_validate), and a recognised source-file
// extension refines a plain-text verdict.
// ============================================================
#include "filetype.h"
#include "../crypto/utf8.h"

static int ft_starts(const uint8_t* d, uint32_t len, const char* sig, uint32_t n) {
    if (len < n) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (d[i] != (uint8_t)sig[i]) return 0;
    return 1;
}

// Case-insensitive "does name end with suf" (suf includes the dot, e.g. ".c").
static int ft_ext(const char* name, const char* suf) {
    if (!name) return 0;
    uint32_t ls = 0, lf = 0;
    while (name[ls]) ls++;
    while (suf[lf]) lf++;
    if (lf > ls) return 0;
    for (uint32_t i = 0; i < lf; i++) {
        char a = name[ls - lf + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

const char* filetype_identify(const char* name, const uint8_t* data, uint32_t len) {
    if (len == 0) return "empty";

    // ---- magic numbers: the content signature wins over any extension ----
    if (ft_starts(data, len, "\x7f" "ELF", 4))                    return "ELF executable";
    if (ft_starts(data, len, "\x89PNG\r\n\x1a\n", 8))             return "PNG image data";
    if (ft_starts(data, len, "GIF87a", 6) ||
        ft_starts(data, len, "GIF89a", 6))                        return "GIF image data";
    if (ft_starts(data, len, "\xff\xd8\xff", 3))                  return "JPEG image data";
    if (ft_starts(data, len, "%PDF-", 5))                         return "PDF document";
    if (ft_starts(data, len, "PK\x03\x04", 4) ||
        ft_starts(data, len, "PK\x05\x06", 4))                    return "Zip archive data";
    if (ft_starts(data, len, "\x1f\x8b", 2))                      return "gzip compressed data";
    if (len >= 12 && ft_starts(data, len, "RIFF", 4) &&
        data[8]=='W' && data[9]=='A' && data[10]=='V' && data[11]=='E') return "WAV audio";
    if (len >= 2 && data[0]=='B' && data[1]=='M')                 return "PC bitmap (BMP)";
    if (len >= 262 && data[257]=='u' && data[258]=='s' &&
        data[259]=='t' && data[260]=='a' && data[261]=='r')       return "tar archive";
    if (len >= 2 && data[0]=='#' && data[1]=='!')                 return "script text executable";

    // ---- text vs binary over the sampled prefix ----
    uint32_t n = len > 1024 ? 1024 : len;
    int has_high = 0, has_ctrl = 0, has_nul = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = data[i];
        if (b == 0) { has_nul = 1; break; }
        if (b >= 0x80) has_high = 1;
        else if (b < 0x20 && b != '\t' && b != '\n' && b != '\r' &&
                 b != '\f' && b != '\v' && b != 0x1b) has_ctrl = 1;   // 0x1b = ESC (ANSI)
    }
    if (has_nul || has_ctrl) return "data";
    if (has_high && !utf8_validate(data, n)) return "data";   // high bytes that aren't UTF-8

    // ---- it is text: refine a plain-text verdict by a known source extension ----
    if (ft_ext(name, ".c"))                        return "C source, text";
    if (ft_ext(name, ".h"))                        return "C header, text";
    if (ft_ext(name, ".md"))                       return "Markdown document, text";
    if (ft_ext(name, ".n"))                        return "N source, text";
    if (ft_ext(name, ".sh"))                       return "shell script, text";
    if (ft_ext(name, ".json"))                     return "JSON text data";
    if (ft_ext(name, ".html") || ft_ext(name, ".htm")) return "HTML document, text";

    return has_high ? "UTF-8 Unicode text" : "ASCII text";
}

// ---- known-answer self-test (`filetype`) ----
int filetype_selftest(void) {
    struct { const char* name; const char* data; uint32_t len; const char* want; } t[] = {
        { "empty.dat", "",                              0,  "empty" },
        { "a.out",     "\x7f" "ELF\x02\x01\x01",        7,  "ELF executable" },
        { "x.png",     "\x89PNG\r\n\x1a\n" "IHDR",      12, "PNG image data" },
        { "x.gif",     "GIF89a\x10\x00",                8,  "GIF image data" },
        { "x.jpg",     "\xff\xd8\xff\xe0",              4,  "JPEG image data" },
        { "x.pdf",     "%PDF-1.7\n",                    9,  "PDF document" },
        { "x.gz",      "\x1f\x8b\x08\x00",              4,  "gzip compressed data" },
        { "x.zip",     "PK\x03\x04\x14",                5,  "Zip archive data" },
        { "run.sh",    "#!/bin/sh\necho hi\n",          18, "script text executable" }, // #! beats .sh
        { "readme.md", "# Title\nbody\n",               13, "Markdown document, text" },
        { "main.c",    "int main(void){return 0;}\n",   26, "C source, text" },
        { "k.h",       "#ifndef K\n#endif\n",           17, "C header, text" },
        { "notes.txt", "hello, world\n",                13, "ASCII text" },
        { "u.txt",     "caf\xc3\xa9\n",                 6,  "UTF-8 Unicode text" },     // "café"
        { "b.bin",     "\x01\x02\x00\x03",              4,  "data" },                   // has NUL
        { "hi.raw",    "\xff\xfe\xfd\xfc",              4,  "data" },                   // high, not UTF-8
    };
    for (unsigned i = 0; i < sizeof(t)/sizeof(t[0]); i++) {
        const char* got = filetype_identify(t[i].name, (const uint8_t*)t[i].data, t[i].len);
        const char* w = t[i].want;
        int j = 0;
        while (got[j] && w[j] && got[j] == w[j]) j++;
        if (got[j] != w[j]) return (int)(i + 1);
    }
    return 0;
}
