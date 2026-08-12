// ============================================================
// ini.c - a hardened, allocation-free INI / .conf reader (see ini.h).
// Pure logic, every access bounded by the caller-supplied length; a malformed
// line is skipped, never trusted. Used by the `iniget` command + the iniparse KAT.
// ============================================================
#include "ini.h"

static int is_sp(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Exact compare of the bounded token text[0..n) against the NUL-terminated `str`.
static int tok_eq(const char* s, int n, const char* str) {
    int i = 0;
    while (i < n && str[i] && s[i] == (char)str[i]) i++;
    return i == n && str[i] == 0;
}

int ini_get(const char* text, int len, const char* section, const char* key,
            char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = 0;
    if (!text || len < 0 || !key) return 0;

    const char* want = section ? section : "";
    int in_target = (want[0] == 0);   // keys before any [section] are the global section
    int i = 0;

    while (i < len) {
        int ls = i;                                   // line start
        while (i < len && text[i] != '\n') i++;
        int le = i;                                   // line end (exclusive), before '\n'
        if (i < len) i++;                             // step past '\n'

        int a = ls;                                   // first non-blank on the line
        while (a < le && is_sp(text[a])) a++;
        if (a >= le) continue;                        // blank line

        char c0 = text[a];
        if (c0 == ';' || c0 == '#') continue;         // whole-line comment

        if (c0 == '[') {                              // section header
            int se = a + 1;
            while (se < le && text[se] != ']') se++;
            if (se >= le) { in_target = 0; continue; }// no closing ']': malformed, leave any section
            int s1 = a + 1;      while (s1 < se && is_sp(text[s1]))   s1++;   // trim name
            int s2 = se;         while (s2 > s1 && is_sp(text[s2 - 1])) s2--;
            in_target = tok_eq(text + s1, s2 - s1, want);
            continue;
        }

        if (!in_target) continue;                     // a key, but not in our section

        int eq = a;                                   // find the '=' delimiter
        while (eq < le && text[eq] != '=') eq++;
        if (eq >= le) continue;                       // no '=': malformed key line, skip

        int ke = eq;                                  // trim the key's trailing blanks
        while (ke > a && is_sp(text[ke - 1])) ke--;
        if (!tok_eq(text + a, ke - a, key)) continue; // key mismatch

        int vs = eq + 1;                              // value, trimmed both ends
        while (vs < le && is_sp(text[vs]))     vs++;
        int ve = le;
        while (ve > vs && is_sp(text[ve - 1])) ve--;

        int n = ve - vs;
        if (n > cap - 1) n = cap - 1;                 // bounded copy
        for (int k = 0; k < n; k++) out[k] = text[vs + k];
        out[n] = 0;
        return 1;                                     // first assignment wins
    }
    return 0;
}

// ---- known-answer self-test (`iniparse`) ----
int ini_selftest(void) {
    static const char CFG[] =
        "; a leading comment\n"
        "name = NyxOS\n"
        "version=6.4.190\n"
        "\n"
        "[net]\n"
        "host = 10.0.2.15\n"
        "  spaced   =   trimmed  value  \n"
        "motd = hi ; not a comment\n"
        "colon : nope\n"
        "\n"
        "[empty]\n"
        "blank =\n"
        "\n"
        "[dup]\n"
        "k = first\n"
        "k = second\n";
    const int L = (int)sizeof(CFG) - 1;   // exclude the trailing NUL: exercise the length bound
    char v[32];

    struct { const char* sec; const char* key; int found; const char* val; } c[] = {
        { 0,       "name",    1, "NyxOS" },          // global section (NULL selects it)
        { "",      "version", 1, "6.4.190" },        // "" also selects global; no spaces around '='
        { "net",   "host",    1, "10.0.2.15" },      // section-scoped
        { "net",   "spaced",  1, "trimmed  value" }, // key + value trimmed; interior blanks kept
        { "net",   "motd",    1, "hi ; not a comment" }, // inline ';' stays in the value
        { "net",   "colon",   0, "" },               // ':' is not a delimiter -> skipped -> not found
        { "empty", "blank",   1, "" },               // present but empty -> found, empty value
        { "dup",   "k",       1, "first" },          // first assignment wins
        { "net",   "nope",    0, "" },               // missing key
        { "nope",  "host",    0, "" },               // missing section
        { 0,       "host",    0, "" },               // section scoping: host is in [net], not global
        { "net",   "spaced",  1, "trimmed  value" }, // (repeat, checked again under truncation below)
    };
    int nc = (int)(sizeof(c) / sizeof(c[0]));
    for (int i = 0; i < nc; i++) {
        int f = ini_get(CFG, L, c[i].sec, c[i].key, v, (int)sizeof(v));
        if (f != c[i].found) return i + 1;
        if (f) { int k = 0; while (c[i].val[k] && v[k] == c[i].val[k]) k++;
                 if (c[i].val[k] || v[k]) return i + 1; }
    }

    // truncation: a value longer than the buffer must be cut and still NUL-terminated.
    char small[6];
    if (ini_get(CFG, L, "net", "host", small, (int)sizeof(small)) != 1) return 100;
    if (small[5] != 0) return 101;                    // NUL-terminated at cap-1
    if (small[0] != '1' || small[4] != '.') return 102; // "10.0." (first 5 of "10.0.2.15")

    return 0;
}
