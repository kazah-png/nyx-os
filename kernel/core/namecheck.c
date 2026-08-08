// ============================================================
// namecheck.c - filename / path-component naming policy
// ============================================================
// User-supplied names (touch/mkdir arguments, and any future create path) should not be
// able to smuggle a control character, a malformed byte sequence, a path separator, or a
// "."/".." into the filesystem as a single component. name_is_valid() enforces one clean
// policy for that, and in doing so becomes the first real caller of the strict UTF-8
// decoder (utf8.c): every code point of the name is decoded and range-checked, so an
// overlong form, a lone surrogate, a truncated sequence, or a stray 0xFF is rejected the
// same way a control byte is. Correctness is pinned by namecheck_selftest() (the CI
// `namecheck` KAT). This is validation only — it never allocates and never touches the FS.
#include "kernel.h"
#include "../crypto/utf8.h"
#include "namecheck.h"

int name_is_valid(const char* name) {
    if (!name) return 0;
    size_t n = 0;
    while (name[n]) n++;
    if (n == 0 || n > NAME_MAX_BYTES) return 0;
    if (name[0] == '.' && (n == 1 || (n == 2 && name[1] == '.'))) return 0;   // "." / ".."

    const uint8_t* s = (const uint8_t*)name;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        int len = utf8_decode(s + i, n - i, &cp);
        if (len == 0) return 0;                        // malformed / overlong / truncated UTF-8
        if (cp == '/') return 0;                        // path separator is not part of a name
        if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) return 0;   // C0/C1 controls + DEL
        i += (size_t)len;
    }
    return 1;
}

int path_last_component_ok(const char* path) {
    if (!path) return 0;
    size_t n = 0;
    while (path[n]) n++;
    while (n > 0 && path[n - 1] == '/') n--;            // ignore trailing slashes ("foo/" -> "foo")
    size_t start = 0;
    for (size_t i = 0; i < n; i++)
        if (path[i] == '/') start = i + 1;              // component begins after the last '/'
    size_t clen = n - start;
    if (clen == 0 || clen > NAME_MAX_BYTES) return 0;
    char comp[NAME_MAX_BYTES + 1];
    for (size_t i = 0; i < clen; i++) comp[i] = path[start + i];
    comp[clen] = '\0';
    return name_is_valid(comp);
}

// ---- Known-answer test --------------------------------------------------------------
int namecheck_selftest(void) {
    // Valid names, including multi-byte UTF-8 (byte escapes keep this source ASCII-safe).
    if (!name_is_valid("file.txt"))            return 1;
    if (!name_is_valid("a"))                   return 2;
    if (!name_is_valid("caf\xC3\xA9"))         return 3;   // "cafe" + U+00E9 (2-byte)
    if (!name_is_valid("\xE6\x96\x87\xE4\xBB\xB6")) return 4;   // U+6587 U+4EF6 (3-byte each)
    if (!name_is_valid(".hidden"))             return 5;   // a leading dot is fine (not "."/"..")
    if (!name_is_valid("my-file_2.tar.gz"))    return 6;

    // Empty and the special directory entries.
    if (name_is_valid(""))                     return 7;
    if (name_is_valid("."))                    return 8;
    if (name_is_valid(".."))                   return 9;

    // Path separator and control characters.
    if (name_is_valid("a/b"))                  return 10;
    if (name_is_valid("x\ty"))                 return 11;   // TAB (C0)
    if (name_is_valid("x\ny"))                 return 12;   // LF  (C0)
    { char d[4] = { 'a', (char)0x7F, 'b', 0 }; if (name_is_valid(d)) return 13; }   // DEL
    if (name_is_valid("nul\xC2\x85here"))      return 14;   // U+0085 NEL (C1 control)

    // Malformed UTF-8.
    { char b[3] = { (char)0xFF, 'x', 0 };            if (name_is_valid(b)) return 15; }  // 0xFF never valid
    { char b[2] = { (char)0xC3, 0 };                 if (name_is_valid(b)) return 16; }  // truncated lead
    { char b[3] = { (char)0xE0, (char)0x80, 0 };     if (name_is_valid(b)) return 17; }  // overlong/truncated
    { char b[4] = { (char)0xED, (char)0xA0, (char)0x80, 0 }; if (name_is_valid(b)) return 18; } // UTF-16 surrogate

    // Length boundary: exactly 255 bytes is ok, 256 is not.
    { char ok[NAME_MAX_BYTES + 1];  int k; for (k = 0; k < NAME_MAX_BYTES; k++) ok[k] = 'a'; ok[NAME_MAX_BYTES] = 0;
      if (!name_is_valid(ok)) return 19; }
    { char big[NAME_MAX_BYTES + 2]; int k; for (k = 0; k < NAME_MAX_BYTES + 1; k++) big[k] = 'a'; big[NAME_MAX_BYTES + 1] = 0;
      if (name_is_valid(big)) return 20; }

    // Basename extraction: the parent path is not judged, only the final component.
    if (!path_last_component_ok("/home/nyx/file.txt")) return 21;
    if (!path_last_component_ok("dir/"))               return 22;   // trailing slash ignored
    if (path_last_component_ok("/home/nyx/.."))        return 23;   // final ".." rejected
    if (path_last_component_ok("/a/b/x\x01y"))         return 24;   // control in the final component
    return 0;
}
