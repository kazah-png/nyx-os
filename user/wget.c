#include "libc.h"

/* wget — a tiny userspace HTTP/1.0 client over NyxOS TCP sockets (v5.8.73).
 *
 *   wget [options] http://host[:port][/path]
 *   wget [options] <host> [port] [path]        (port default 80, path default /)
 *
 * <host> may be a dotted IPv4 or a hostname; hostnames are resolved with a
 * self-contained DNS-over-UDP query (no new syscall — it uses the same
 * sendto()/recvfrom() the UDP demo does). The body is written to stdout, or to a
 * file with -O. Options:
 *   -O <file>   save the response body to <file> instead of stdout
 *   -r <ip>     DNS resolver IP     (default 10.0.2.3, QEMU SLIRP's resolver)
 *   -p <port>   DNS resolver port   (default 53)
 *   -q          quiet: no status line on stderr
 *   -S          print the response headers on stderr
 *
 * Single process by design: NyxOS's concurrent multi-process net path can still
 * garble TCP state, so wget does all its I/O in one process.
 */

#define RESP_MAX 65536
#define MAX_REDIRECTS 5

static void es(const char* s) { write(2, s, strlen(s)); }   /* stderr string */

/* Scan an HTTP header block [h, h+len) for a "Location:" line (case-insensitive)
 * and copy its value into out. Returns 1 if found. */
static int find_location(const char* h, int len, char* out, int outsz) {
    int i = 0;
    while (i < len) {
        const char* key = "location:";
        int j = i, k = 0;
        while (key[k] && j < len) {
            char c = h[j];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (c != key[k]) break;
            k++; j++;
        }
        if (!key[k]) {
            while (j < len && (h[j] == ' ' || h[j] == '\t')) j++;
            int o = 0;
            while (j < len && h[j] != '\r' && h[j] != '\n' && o < outsz - 1) out[o++] = h[j++];
            out[o] = '\0';
            return 1;
        }
        while (i < len && h[i] != '\n') i++;   // advance to next line
        i++;
    }
    return 0;
}

/* Parse a dotted-quad into the kernel's network order (first octet = low byte,
 * as connect()/sendto() expect). Returns 1 on success, 0 if s isn't a.b.c.d. */
static int parse_ipv4(const char* s, unsigned int* out) {
    unsigned int ip = 0; int oct = 0, val = 0, digits = 0;
    for (const char* p = s; ; p++) {
        if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); digits++; if (val > 255) return 0; }
        else if (*p == '.' || *p == '\0') {
            if (!digits) return 0;
            ip |= (unsigned int)(val & 0xFF) << (8 * oct);
            oct++; val = 0; digits = 0;
            if (*p == '\0') break;
            if (oct == 4) return 0;                 /* too many octets */
        } else return 0;
    }
    if (oct != 4) return 0;
    *out = ip;
    return 1;
}

/* Skip a DNS name at offset `off` in `p` (length-prefixed labels, terminated by a
 * 0 byte or a 0xC0 compression pointer). Returns the offset just past the name. */
static int dns_skip_name(const unsigned char* p, int off, int max) {
    while (off < max) {
        unsigned char len = p[off];
        if (len == 0) return off + 1;
        if ((len & 0xC0) == 0xC0) return off + 2;   /* pointer ends the name */
        off += 1 + len;
    }
    return off;
}

/* Resolve `host` to an IPv4 (network order) via one A-record query to
 * resolver:port over UDP. Returns 0 on success, -1 on failure. */
static int dns_resolve(const char* host, unsigned int resolver, int rport, unsigned int* out) {
    unsigned char q[300];
    int n = 0;
    q[n++] = 0x12; q[n++] = 0x34;      /* id */
    q[n++] = 0x01; q[n++] = 0x00;      /* flags: recursion desired */
    q[n++] = 0x00; q[n++] = 0x01;      /* QDCOUNT = 1 */
    q[n++] = 0x00; q[n++] = 0x00;      /* ANCOUNT */
    q[n++] = 0x00; q[n++] = 0x00;      /* NSCOUNT */
    q[n++] = 0x00; q[n++] = 0x00;      /* ARCOUNT */
    /* QNAME: split host into length-prefixed labels. */
    const char* s = host;
    while (*s) {
        const char* dot = s;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - s);
        if (len <= 0 || len > 63 || n + len + 1 > (int)sizeof(q) - 6) return -1;
        q[n++] = (unsigned char)len;
        for (int i = 0; i < len; i++) q[n++] = (unsigned char)s[i];
        s = (*dot == '.') ? dot + 1 : dot;
    }
    q[n++] = 0x00;                     /* end of QNAME */
    q[n++] = 0x00; q[n++] = 0x01;      /* QTYPE = A */
    q[n++] = 0x00; q[n++] = 0x01;      /* QCLASS = IN */

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    unsigned char r[512];
    int ok = -1;
    for (int attempt = 0; attempt < 3 && ok < 0; attempt++) {
        if (sendto(fd, q, n, 0, resolver, rport) < 0) break;
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 2000) <= 0) continue;    /* retry on timeout */
        unsigned int sip = 0; int sport = 0;
        int m = recvfrom(fd, r, sizeof(r), 0, &sip, &sport);
        if (m < 12) continue;
        int anc = (r[6] << 8) | r[7];
        if (anc < 1) { ok = -2; break; }           /* answered, but no records */
        int off = 12;
        off = dns_skip_name(r, off, m) + 4;        /* skip question name + QTYPE/QCLASS */
        for (int a = 0; a < anc && off + 10 <= m; a++) {
            off = dns_skip_name(r, off, m);
            int type = (r[off] << 8) | r[off + 1];
            int rdlen = (r[off + 8] << 8) | r[off + 9];
            int rdata = off + 10;
            if (type == 1 && rdlen == 4 && rdata + 4 <= m) {
                *out = (unsigned int)r[rdata] | ((unsigned int)r[rdata + 1] << 8)
                     | ((unsigned int)r[rdata + 2] << 16) | ((unsigned int)r[rdata + 3] << 24);
                ok = 0;
                break;
            }
            off = rdata + rdlen;
        }
    }
    close(fd);
    return ok == 0 ? 0 : -1;
}

int main(int argc, char** argv) {
    const char* outfile = 0;
    unsigned int resolver = 10u | (0u << 8) | (2u << 16) | (3u << 24);   /* 10.0.2.3 */
    int rport = 53, quiet = 0, show_hdr = 0;

    /* Options, then up to three positionals (url | host [port] [path]). */
    const char* pos[3]; int np = 0;
    for (int i = 1; i < argc; i++) {
        char* a = argv[i];
        if (a[0] == '-' && a[1] && !( (a[1] >= '0' && a[1] <= '9') )) {
            if (strcmp(a, "-O") == 0 && i + 1 < argc) outfile = argv[++i];
            else if (strcmp(a, "-r") == 0 && i + 1 < argc) parse_ipv4(argv[++i], &resolver);
            else if (strcmp(a, "-p") == 0 && i + 1 < argc) rport = atoi(argv[++i]);
            else if (strcmp(a, "-q") == 0) quiet = 1;
            else if (strcmp(a, "-S") == 0) show_hdr = 1;
            else { es("wget: unknown option "); es(a); es("\n"); return 2; }
        } else if (np < 3) pos[np++] = a;
    }
    if (np == 0) {
        es("usage: wget [-O file] [-r resolver] [-p port] [-q] [-S] "
           "http://host[:port][/path] | host [port] [path]\n");
        return 2;
    }

    char host[128]; int port = 80; char path[256];
    path[0] = '/'; path[1] = '\0';

    const char* url = pos[0];
    if (strstr(url, "://")) {
        /* URL form. Only http:// is supported. */
        const char* p = url;
        if (strncmp(p, "http://", 7) == 0) p += 7;
        else { es("wget: only http:// URLs are supported\n"); return 2; }
        int h = 0;
        while (*p && *p != ':' && *p != '/' && h < (int)sizeof(host) - 1) host[h++] = *p++;
        host[h] = '\0';
        if (*p == ':') { p++; port = atoi(p); while (*p >= '0' && *p <= '9') p++; }
        if (*p == '/') { strncpy(path, p, sizeof(path) - 1); path[sizeof(path) - 1] = '\0'; }
    } else {
        /* host [port] [path] form. */
        strncpy(host, pos[0], sizeof(host) - 1); host[sizeof(host) - 1] = '\0';
        int used = 1;
        if (np >= 2 && argv && pos[1][0] >= '0' && pos[1][0] <= '9') { port = atoi(pos[1]); used = 2; }
        if (np > used && pos[used][0] == '/') { strncpy(path, pos[used], sizeof(path) - 1); path[sizeof(path) - 1] = '\0'; }
    }

    /* Fetch host:port/path, following up to MAX_REDIRECTS 3xx Location redirects.
     * host/port/path are rewritten in place on each hop. */
    static char resp[RESP_MAX];
    int total = 0, status = 0, body_len = 0;
    char* body = resp;
    for (int hop = 0; ; hop++) {
        unsigned int ip;
        if (!parse_ipv4(host, &ip)) {
            if (!quiet) { es("wget: resolving "); es(host); es(" ...\n"); }
            if (dns_resolve(host, resolver, rport, &ip) != 0) {
                es("wget: cannot resolve host: "); es(host); es("\n");
                return 1;
            }
        }
        if (!quiet) {
            char m[200];
            snprintf(m, sizeof(m), "wget: connecting to %d.%d.%d.%d:%d%s\n",
                     ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, port, path);
            es(m);
        }

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { es("wget: socket() failed\n"); return 1; }
        if (connect(fd, ip, port) != 0) { es("wget: connection failed\n"); close(fd); return 1; }

        char hosthdr[160];
        if (port == 80) snprintf(hosthdr, sizeof(hosthdr), "%s", host);
        else            snprintf(hosthdr, sizeof(hosthdr), "%s:%d", host, port);
        char req[512];
        int rl = snprintf(req, sizeof(req),
            "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: NyxOS-wget/1.0\r\nConnection: close\r\n\r\n",
            path, hosthdr);
        if (write(fd, req, rl) != rl) { es("wget: send failed\n"); close(fd); return 1; }

        total = 0;
        for (;;) {
            int n = read(fd, resp + total, RESP_MAX - 1 - total);
            if (n <= 0) break;
            total += n;
            if (total >= RESP_MAX - 1) break;
        }
        resp[total] = '\0';
        close(fd);
        if (total == 0) { es("wget: empty response\n"); return 1; }

        char* sep = strstr(resp, "\r\n\r\n");
        body = sep ? sep + 4 : resp;
        body_len = sep ? total - (int)(body - resp) : total;
        status = 0;
        if (strncmp(resp, "HTTP/", 5) == 0) {
            char* sp = strchr(resp, ' ');
            if (sp) status = atoi(sp + 1);
        }
        if (show_hdr && sep) { write(2, resp, (int)(sep - resp)); es("\n"); }

        /* 3xx with a Location and hops left → parse the new target and re-fetch. */
        char loc[256];
        if (status >= 300 && status < 400 && hop < MAX_REDIRECTS && sep &&
            find_location(resp, (int)(sep - resp), loc, sizeof(loc))) {
            if (!quiet) { es("wget: redirect -> "); es(loc); es("\n"); }
            if (strncmp(loc, "http://", 7) == 0) {          // absolute URL → new host
                const char* p = loc + 7; int h = 0; port = 80;
                while (*p && *p != ':' && *p != '/' && h < (int)sizeof(host) - 1) host[h++] = *p++;
                host[h] = '\0';
                if (*p == ':') { p++; port = atoi(p); while (*p >= '0' && *p <= '9') p++; }
                if (*p == '/') { strncpy(path, p, sizeof(path) - 1); path[sizeof(path) - 1] = '\0'; }
                else           { path[0] = '/'; path[1] = '\0'; }
            } else if (loc[0] == '/') {                     // root-relative → same host/port
                strncpy(path, loc, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
            } else {                                        // bare relative → treat as /loc
                char tmp[256]; snprintf(tmp, sizeof(tmp), "/%s", loc);
                strncpy(path, tmp, sizeof(path) - 1); path[sizeof(path) - 1] = '\0';
            }
            continue;
        }
        break;
    }

    if (outfile) {
        long of = open(outfile, O_CREAT | O_TRUNC, 0644);
        if (of < 0) { es("wget: cannot open output file\n"); return 1; }
        write((int)of, body, body_len);
        close((int)of);
    } else {
        write(1, body, body_len);
        if (body_len && body[body_len - 1] != '\n') write(1, "\n", 1);
    }

    if (!quiet) {
        char m[96];
        snprintf(m, sizeof(m), "wget: HTTP %d, %d bytes%s%s\n", status, body_len,
                 outfile ? " -> " : "", outfile ? outfile : "");
        es(m);
    }
    return status >= 200 && status < 400 ? 0 : 1;
}
