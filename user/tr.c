#include "libc.h"

/* tr — translate, squeeze, or delete characters from stdin to stdout, like GNU tr.
 *   tr SET1 SET2        translate each byte in SET1 to the matching byte in SET2
 *   tr -d SET1          delete every byte in SET1
 *   tr -s SET1          squeeze runs of bytes in SET1 down to one
 *   tr -d -s SET1 SET2  delete SET1, then squeeze SET2
 * Flags combine: -ds, -cs, -cd. -c operates on the COMPLEMENT of SET1. When SET2
 * is shorter than SET1 its last byte repeats. A SET may contain ranges (a-z), C
 * escapes (\n \t \r \\ \0 \a \b \f \v \ooo), and POSIX classes: [:alpha:] [:digit:]
 * [:alnum:] [:upper:] [:lower:] [:space:] [:blank:] [:punct:] [:cntrl:] [:xdigit:]
 * [:print:] [:graph:]. tr is a pure stdin->stdout filter (no file operand). */

/* ---- buffered stdout, with an optional squeeze applied on the way out -------- */
static unsigned char obuf[4096];
static int olen = 0;
static int g_squeeze = 0;
static unsigned char g_sq[256];        /* membership of the squeeze set */
static int g_last = -1;                /* last byte emitted (for run collapsing) */

static void oflush(void) { if (olen) { write(1, obuf, olen); olen = 0; } }
static void oput(unsigned char c) { obuf[olen++] = c; if (olen == (int)sizeof(obuf)) oflush(); }

static void emit(unsigned char c) {
    if (g_squeeze && g_sq[c] && (int)c == g_last) return;   /* collapse a run */
    oput(c);
    g_last = (int)c;
}

/* ---- SET parsing: expand a SET string into an ordered list of bytes ---------- */

/* Read one logical character at s[*pi], resolving a C/octal escape, advancing *pi. */
static unsigned char read_ch(const char* s, int* pi) {
    int i = *pi;
    unsigned char c = (unsigned char)s[i++];
    if (c == '\\' && s[i]) {
        unsigned char e = (unsigned char)s[i++];
        switch (e) {
            case 'n': c = 10; break;  case 't': c = 9;  break;  case 'r': c = 13; break;
            case 'a': c = 7;  break;  case 'b': c = 8;  break;  case 'f': c = 12; break;
            case 'v': c = 11; break;  case '\\': c = 92; break;
            default:
                if (e >= '0' && e <= '7') {                    /* \ooo, up to 3 octal digits */
                    int v = e - '0', k = 0;
                    while (k < 2 && s[i] >= '0' && s[i] <= '7') { v = v * 8 + (s[i] - '0'); i++; k++; }
                    c = (unsigned char)v;
                } else {
                    c = e;                                     /* unknown escape -> literal char */
                }
        }
    }
    *pi = i;
    return c;
}

static int cls_eq(const char* p, int len, const char* name) {
    int k = 0;
    while (name[k]) { if (k >= len || p[k] != name[k]) return 0; k++; }
    return k == len;
}

/* Expand a [:class:] whose name is p[0..len) into out[]; returns 0 if unknown. */
static int expand_class(const char* p, int len, unsigned char* out, int cap, int* pn) {
    static const char* names[] = { "alpha","digit","alnum","upper","lower","space",
                                   "blank","punct","cntrl","xdigit","print","graph" };
    int id = -1;
    for (int k = 0; k < 12; k++) if (cls_eq(p, len, names[k])) { id = k; break; }
    if (id < 0) return 0;
    int n = *pn;
    for (int b = 0; b < 256; b++) {
        int lower = (b >= 'a' && b <= 'z'), upper = (b >= 'A' && b <= 'Z');
        int digit = (b >= '0' && b <= '9'), alpha = lower || upper, alnum = alpha || digit;
        int m = 0;
        switch (id) {
            case 0:  m = alpha; break;
            case 1:  m = digit; break;
            case 2:  m = alnum; break;
            case 3:  m = upper; break;
            case 4:  m = lower; break;
            case 5:  m = (b==' '||b=='\t'||b=='\n'||b=='\r'||b=='\v'||b=='\f'); break;
            case 6:  m = (b==' '||b=='\t'); break;
            case 7:  m = (b >= 33 && b < 127 && !alnum); break;              /* punct */
            case 8:  m = (b < 32 || b == 127); break;                       /* cntrl */
            case 9:  m = (digit || (b>='A'&&b<='F') || (b>='a'&&b<='f')); break;  /* xdigit */
            case 10: m = (b >= 32 && b < 127); break;                       /* print */
            case 11: m = (b > 32  && b < 127); break;                       /* graph */
        }
        if (m && n < cap) out[n++] = (unsigned char)b;
    }
    *pn = n;
    return 1;
}

/* Expand a whole SET into out[] (ordered, with repeats). Returns 0 on a bad set. */
static int expand(const char* s, unsigned char* out, int cap, int* outn) {
    int n = 0, i = 0;
    while (s[i]) {
        if (s[i] == '[' && s[i + 1] == ':') {          /* a POSIX character class */
            int start = i + 2, j = start;
            while (s[j] && !(s[j] == ':' && s[j + 1] == ']')) j++;
            if (s[j] == ':') {
                if (!expand_class(s + start, j - start, out, cap, &n)) { *outn = n; return 0; }
                i = j + 2;
                continue;
            }
            /* not a real class — fall through and treat '[' literally */
        }
        unsigned char c = read_ch(s, &i);
        if (s[i] == '-' && s[i + 1] != '\0') {         /* a range c-d */
            i++;                                       /* consume '-' */
            unsigned char d = read_ch(s, &i);
            if (c > d) { *outn = n; return 0; }        /* reverse range is an error */
            for (int v = c; v <= d; v++) if (n < cap) out[n++] = (unsigned char)v;
        } else {
            if (n < cap) out[n++] = c;
        }
    }
    *outn = n;
    return 1;
}

int main(int argc, char** argv) {
    int dflag = 0, sflag = 0, cflag = 0;
    const char* s1 = 0; const char* s2 = 0;
    int ai = 1;

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        if (argv[ai][1] == '-' && argv[ai][2] == '\0') { ai++; break; }   /* "--" ends options */
        for (const char* a = argv[ai] + 1; *a; a++) {
            if (*a == 'd') dflag = 1;
            else if (*a == 's') sflag = 1;
            else if (*a == 'c' || *a == 'C') cflag = 1;
            else { printf("tr: invalid option -- '%c'\n", *a); return 1; }
        }
    }
    if (ai < argc) s1 = argv[ai++];
    if (ai < argc) s2 = argv[ai++];

    if (!s1) { printf("usage: tr [-cds] SET1 [SET2]\n"); return 1; }

    /* Decide the mode. */
    enum { M_TRANSLATE, M_DELETE, M_SQUEEZE } mode;
    if (dflag)          mode = M_DELETE;
    else if (s2)        mode = M_TRANSLATE;
    else if (sflag)     mode = M_SQUEEZE;
    else { printf("tr: two SETs required to translate (or use -d / -s)\n"); return 1; }
    if (dflag && sflag && !s2) { printf("tr: -s with -d needs SET2 to squeeze\n"); return 1; }

    unsigned char e1[512], e2[512];
    int n1 = 0, n2 = 0;
    if (!expand(s1, e1, (int)sizeof(e1), &n1)) { printf("tr: invalid SET1\n"); return 1; }
    if (s2 && !expand(s2, e2, (int)sizeof(e2), &n2)) { printf("tr: invalid SET2\n"); return 1; }

    int raw1[256], raw2[256];
    for (int b = 0; b < 256; b++) { raw1[b] = 0; raw2[b] = 0; }
    for (int i = 0; i < n1; i++) raw1[e1[i]] = 1;
    for (int i = 0; i < n2; i++) raw2[e2[i]] = 1;

    /* Translation table (identity unless translating). */
    unsigned char trans[256];
    for (int b = 0; b < 256; b++) trans[b] = (unsigned char)b;
    if (mode == M_TRANSLATE) {
        if (n2 == 0) { printf("tr: SET2 must not be empty\n"); return 1; }
        if (cflag) {
            unsigned char last = e2[n2 - 1];
            for (int b = 0; b < 256; b++) if (!raw1[b]) trans[b] = last;
        } else {
            for (int i = 0; i < n1; i++) trans[e1[i]] = e2[i < n2 ? i : n2 - 1];
        }
    }

    /* Delete membership (respects -c). */
    int del[256];
    for (int b = 0; b < 256; b++) del[b] = cflag ? !raw1[b] : raw1[b];

    /* Squeeze set: SET2 when translating/deleting, else SET1 (respecting -c). */
    g_squeeze = sflag;
    if (sflag) {
        if (mode == M_SQUEEZE) for (int b = 0; b < 256; b++) g_sq[b] = (unsigned char)(cflag ? !raw1[b] : raw1[b]);
        else                   for (int b = 0; b < 256; b++) g_sq[b] = (unsigned char)raw2[b];
    }

    unsigned char ibuf[4096];
    for (;;) {
        int n = (int)read(0, ibuf, sizeof(ibuf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            unsigned char b = ibuf[i];
            if (mode == M_DELETE) { if (del[b]) continue; emit(b); }
            else if (mode == M_TRANSLATE) emit(trans[b]);
            else emit(b);                       /* M_SQUEEZE: emit() does the work */
        }
    }
    oflush();
    return 0;
}
