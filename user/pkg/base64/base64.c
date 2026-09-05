#include "libc.h"
#include "syscall.h"

/* base64 — RFC 4648 encoder/decoder, a small daily-use data tool.
 *
 *   base64 <text...>        encode the argument text to base64 (standard alphabet, '=' padding)
 *   base64 -d <base64...>   decode base64 back to raw bytes (written verbatim to stdout)
 *
 * Argv-based like the other NyxOS ports: encode joins its arguments with single spaces;
 * decode concatenates them (base64 carries no spaces) and skips any whitespace/padding.
 * Standard algorithm (no upstream code); installs via `xbm install base64`.
 */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;                                  /* '=' , whitespace and junk are skipped */
}

static int b64_encode(const unsigned char* in, int n, char* out) {
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        int b0 = in[i], b1 = (i + 1 < n) ? in[i + 1] : 0, b2 = (i + 2 < n) ? in[i + 2] : 0;
        out[o++] = B64[b0 >> 2];
        out[o++] = B64[((b0 & 3) << 4) | (b1 >> 4)];
        out[o++] = (i + 1 < n) ? B64[((b1 & 15) << 2) | (b2 >> 6)] : '=';
        out[o++] = (i + 2 < n) ? B64[b2 & 63] : '=';
    }
    out[o] = '\0';
    return o;
}

static int b64_decode(const char* s, unsigned char* out, int max) {
    int o = 0, q[4], qi = 0;
    for (; *s; s++) {
        int v = b64_val((unsigned char)*s);
        if (v < 0) continue;
        q[qi++] = v;
        if (qi == 4) {
            if (o < max) out[o++] = (unsigned char)((q[0] << 2) | (q[1] >> 4));
            if (o < max) out[o++] = (unsigned char)(((q[1] & 15) << 4) | (q[2] >> 2));
            if (o < max) out[o++] = (unsigned char)(((q[2] & 3) << 6) | q[3]);
            qi = 0;
        }
    }
    if (qi >= 2) {                               /* trailing group: 2 chars -> 1 byte, 3 -> 2 */
        if (o < max) out[o++] = (unsigned char)((q[0] << 2) | (q[1] >> 4));
        if (qi == 3 && o < max) out[o++] = (unsigned char)(((q[1] & 15) << 4) | (q[2] >> 2));
    }
    return o;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: base64 <text>  |  base64 -d <base64>\n"); return 1; }

    if (strcmp(argv[1], "-d") == 0) {            /* decode -> raw bytes */
        if (argc < 3) { printf("Usage: base64 -d <base64>\n"); return 1; }
        static char joined[6144];
        int j = 0;
        for (int a = 2; a < argc; a++)
            for (const char* p = argv[a]; *p && j < (int)sizeof(joined) - 1; p++) joined[j++] = *p;
        joined[j] = '\0';
        static unsigned char out[4608];
        int n = b64_decode(joined, out, sizeof(out));
        write(1, out, n);
        return 0;
    }

    static unsigned char buf[4096];              /* encode: join args with spaces */
    int n = 0;
    for (int a = 1; a < argc; a++) {
        if (a > 1 && n < (int)sizeof(buf)) buf[n++] = ' ';
        for (const char* p = argv[a]; *p && n < (int)sizeof(buf); p++) buf[n++] = (unsigned char)*p;
    }
    static char out[5504];
    b64_encode(buf, n, out);
    printf("%s\n", out);
    return 0;
}
