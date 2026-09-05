#include "libc.h"
#include "syscall.h"

/* base32 — RFC 4648 encoder/decoder, a small daily-use data tool (companion to base64).
 *
 *   base32 <text...>        encode the argument text to base32 (standard A-Z2-7 alphabet, '=' padding)
 *   base32 -d <base32...>   decode base32 back to raw bytes (written verbatim to stdout)
 *
 * Argv-based like the other NyxOS ports: encode joins its arguments with single spaces;
 * decode concatenates them and skips any whitespace/padding. The decoder also accepts
 * lowercase (a-z == A-Z) for convenience. Standard algorithm (no upstream code); installs
 * via `xbm install base32`. 5 input bytes (40 bits) map to 8 output symbols of 5 bits each.
 */

static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int b32_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';       /* lenient: accept lowercase */
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;                                      /* '=' , whitespace and junk are skipped */
}

static int b32_encode(const unsigned char* in, int n, char* out) {
    int o = 0;
    for (int i = 0; i < n; i += 5) {
        int rem = n - i; if (rem > 5) rem = 5;
        unsigned char b[5] = {0,0,0,0,0};
        for (int k = 0; k < rem; k++) b[k] = in[i + k];
        char c[8];
        c[0] = B32[b[0] >> 3];
        c[1] = B32[((b[0] & 0x07) << 2) | (b[1] >> 6)];
        c[2] = B32[(b[1] >> 1) & 0x1F];
        c[3] = B32[((b[1] & 0x01) << 4) | (b[2] >> 4)];
        c[4] = B32[((b[2] & 0x0F) << 1) | (b[3] >> 7)];
        c[5] = B32[(b[3] >> 2) & 0x1F];
        c[6] = B32[((b[3] & 0x03) << 3) | (b[4] >> 5)];
        c[7] = B32[b[4] & 0x1F];
        int nc = (rem == 1) ? 2 : (rem == 2) ? 4 : (rem == 3) ? 5 : (rem == 4) ? 7 : 8;
        for (int k = 0; k < 8; k++) out[o++] = (k < nc) ? c[k] : '=';
    }
    out[o] = '\0';
    return o;
}

static int b32_decode(const char* s, unsigned char* out, int max) {
    int o = 0, q[8], qi = 0;
    for (; *s; s++) {
        int v = b32_val((unsigned char)*s);
        if (v < 0) continue;
        q[qi++] = v;
        if (qi == 8) {
            if (o < max) out[o++] = (unsigned char)((q[0] << 3) | (q[1] >> 2));
            if (o < max) out[o++] = (unsigned char)(((q[1] & 0x03) << 6) | (q[2] << 1) | (q[3] >> 4));
            if (o < max) out[o++] = (unsigned char)(((q[3] & 0x0F) << 4) | (q[4] >> 1));
            if (o < max) out[o++] = (unsigned char)(((q[4] & 0x01) << 7) | (q[5] << 2) | (q[6] >> 3));
            if (o < max) out[o++] = (unsigned char)(((q[6] & 0x07) << 5) | q[7]);
            qi = 0;
        }
    }
    if (qi >= 2) {                                  /* trailing group: 2->1 byte, 4->2, 5->3, 7->4 */
        if (o < max) out[o++] = (unsigned char)((q[0] << 3) | (q[1] >> 2));
        if (qi >= 4 && o < max) out[o++] = (unsigned char)(((q[1] & 0x03) << 6) | (q[2] << 1) | (q[3] >> 4));
        if (qi >= 5 && o < max) out[o++] = (unsigned char)(((q[3] & 0x0F) << 4) | (q[4] >> 1));
        if (qi >= 7 && o < max) out[o++] = (unsigned char)(((q[4] & 0x01) << 7) | (q[5] << 2) | (q[6] >> 3));
    }
    return o;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: base32 <text>  |  base32 -d <base32>\n"); return 1; }

    if (strcmp(argv[1], "-d") == 0) {               /* decode -> raw bytes */
        if (argc < 3) { printf("Usage: base32 -d <base32>\n"); return 1; }
        static char joined[6144];
        int j = 0;
        for (int a = 2; a < argc; a++)
            for (const char* p = argv[a]; *p && j < (int)sizeof(joined) - 1; p++) joined[j++] = *p;
        joined[j] = '\0';
        static unsigned char out[3840];
        int n = b32_decode(joined, out, sizeof(out));
        write(1, out, n);
        return 0;
    }

    static unsigned char buf[4096];                 /* encode: join args with spaces */
    int n = 0;
    for (int a = 1; a < argc; a++) {
        if (a > 1 && n < (int)sizeof(buf)) buf[n++] = ' ';
        for (const char* p = argv[a]; *p && n < (int)sizeof(buf); p++) buf[n++] = (unsigned char)*p;
    }
    static char out[6600];
    b32_encode(buf, n, out);
    printf("%s\n", out);
    return 0;
}
