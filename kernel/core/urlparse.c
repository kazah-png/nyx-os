// ============================================================
// urlparse.c - hardened URL parser (see urlparse.h)
// ============================================================
#include "urlparse.h"

int url_parse(const char* url, char* host, int hostsz, uint16_t* port,
              char* path, int pathsz, int* is_https) {
    if (!url || !host || !port || !path || hostsz < 1 || pathsz < 2) return -1;
    while (*url == ' ') url++;                              // trim leading spaces

    int https = 0;
    if      (strncmp(url, "https://", 8) == 0) { https = 1; url += 8; }
    else if (strncmp(url, "http://",  7) == 0) { https = 0; url += 7; }
    // no scheme -> treat as http (a bare "example.com" is fine), url unchanged
    if (is_https) *is_https = https;

    const char* hs = url;                                  // host: up to ':' / '/' / end
    while (*url && *url != ':' && *url != '/') url++;
    int hl = (int)(url - hs);
    if (hl <= 0 || hl >= hostsz) return -1;                // empty or won't fit -> reject (no truncation)
    for (int i = 0; i < hl; i++) host[i] = hs[i];
    host[hl] = '\0';

    *port = https ? 443 : 80;
    if (*url == ':') {
        url++;
        if (*url < '0' || *url > '9') return -1;            // a ':' must be followed by a digit
        uint32_t v = 0;
        while (*url >= '0' && *url <= '9') {
            v = v * 10 + (uint32_t)(*url - '0');
            if (v > 65535) return -1;                       // bounded: out-of-range port rejected (no wrap)
            url++;
        }
        if (v == 0) return -1;                              // port 0 is invalid
        *port = (uint16_t)v;
    }

    if (*url == '\0') { path[0] = '/'; path[1] = '\0'; }    // no path -> "/"
    else if (*url == '/') {
        int i = 0;
        while (url[i] && i < pathsz - 1) { path[i] = url[i]; i++; }
        path[i] = '\0';
    } else return -1;                                       // junk after host:port (not '/') -> malformed
    return 0;
}

// ---- known-answer self-test (`urlparse`) ----
static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int url_parse_selftest(void) {
    char host[64], path[128]; uint16_t port; int https;
    int pass = 0, total = 0;

    // helper macro-free style: each case is explicit so a failure names itself.
    total++; if (url_parse("http://example.com/", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && streq(host, "example.com") && port == 80 && streq(path, "/") && https == 0)
             { pass++; printf("urlparse: http basic PASS\n"); } else printf("urlparse: http basic FAIL\n");

    total++; if (url_parse("https://example.com/", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && port == 443 && https == 1)
             { pass++; printf("urlparse: https default port PASS\n"); } else printf("urlparse: https FAIL\n");

    total++; if (url_parse("example.com", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && streq(host, "example.com") && port == 80 && streq(path, "/") && https == 0)
             { pass++; printf("urlparse: scheme-less -> http PASS\n"); } else printf("urlparse: scheme-less FAIL\n");

    total++; if (url_parse("http://host:8080/p?q=1", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && streq(host, "host") && port == 8080 && streq(path, "/p?q=1"))
             { pass++; printf("urlparse: explicit port + query PASS\n"); } else printf("urlparse: port+query FAIL\n");

    total++; if (url_parse("http://host", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && streq(path, "/"))
             { pass++; printf("urlparse: no path -> / PASS\n"); } else printf("urlparse: no path FAIL\n");

    total++; if (url_parse("  http://host/x", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && streq(host, "host") && streq(path, "/x"))
             { pass++; printf("urlparse: leading spaces PASS\n"); } else printf("urlparse: leading spaces FAIL\n");

    total++; if (url_parse("http://host:65535/", host, sizeof(host), &port, path, sizeof(path), &https) == 0
                 && port == 65535)
             { pass++; printf("urlparse: max port 65535 PASS\n"); } else printf("urlparse: max port FAIL\n");

    // ---- rejections ----
    total++; if (url_parse("http://host:70000/", host, sizeof(host), &port, path, sizeof(path), &https) != 0)
             { pass++; printf("urlparse: port overflow rejected PASS\n"); } else printf("urlparse: overflow NOT rejected FAIL\n");

    total++; if (url_parse("http://host:99999999999/", host, sizeof(host), &port, path, sizeof(path), &https) != 0)
             { pass++; printf("urlparse: huge port rejected PASS\n"); } else printf("urlparse: huge port FAIL\n");

    total++; if (url_parse("http://host:0/", host, sizeof(host), &port, path, sizeof(path), &https) != 0)
             { pass++; printf("urlparse: port 0 rejected PASS\n"); } else printf("urlparse: port 0 FAIL\n");

    total++; if (url_parse("http://host:/", host, sizeof(host), &port, path, sizeof(path), &https) != 0)
             { pass++; printf("urlparse: colon no digit rejected PASS\n"); } else printf("urlparse: colon-no-digit FAIL\n");

    total++; if (url_parse("http://:80/", host, sizeof(host), &port, path, sizeof(path), &https) != 0)
             { pass++; printf("urlparse: empty host rejected PASS\n"); } else printf("urlparse: empty host FAIL\n");

    // over-long host: 80 'a's into a 64-byte host buffer must be rejected, not truncated
    total++; { char longurl[128]; int o = 0; const char* pre = "http://";
        for (const char* q = pre; *q; q++) longurl[o++] = *q;
        for (int i = 0; i < 80; i++) longurl[o++] = 'a';
        longurl[o++] = '/'; longurl[o] = '\0';
        if (url_parse(longurl, host, sizeof(host), &port, path, sizeof(path), &https) != 0)
             { pass++; printf("urlparse: over-long host rejected PASS\n"); } else printf("urlparse: over-long host FAIL\n"); }

    printf("urlparse: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
