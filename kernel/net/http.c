#include "../core/kernel.h"
#include "http.h"
#include "tcp.h"
#include "dns.h"

// True once we've received the full header block plus a Content-Length worth of
// body (when the server declares one), so a completed response can finish
// immediately instead of waiting out the silence timeout. `buf` must be
// NUL-terminated at `total` by the caller.
static int http_body_complete(const uint8_t* buf, uint32_t total) {
    const char* b = (const char*)buf;
    char* hdr_end = strstr(b, "\r\n\r\n");
    if (!hdr_end) return 0;                       // headers not fully in yet
    uint32_t header_len = (uint32_t)(hdr_end - b) + 4;
    char* cl = strstr(b, "Content-Length:");
    if (!cl) cl = strstr(b, "Content-length:");
    if (!cl) cl = strstr(b, "content-length:");
    if (!cl || cl >= hdr_end) return 0;           // no length -> wait for FIN
    cl += 15;
    while (*cl == ' ') cl++;
    uint32_t content_length = 0;
    while (*cl >= '0' && *cl <= '9') { content_length = content_length * 10 + (uint32_t)(*cl - '0'); cl++; }
    return (total - header_len) >= content_length;
}

// Parse a raw HTTP/1.x response held in buf[0..total) (NUL-terminated at buf[total]) into
// Case-insensitive: extract the value of header `name` (e.g. "location:") that starts its own line
// within [buf, hdr_end), whitespace-trimmed, into out (NUL-terminated, bounded by cap). Empty if absent.
static void http_get_header(const char* buf, const char* hdr_end, const char* name, char* out, uint32_t cap) {
    out[0] = '\0';
    uint32_t nl = 0; while (name[nl]) nl++;
    for (const char* p = buf; p + nl <= hdr_end; p++) {
        if (p != buf && p[-1] != '\n') continue;              // a header name starts a line
        uint32_t i = 0;
        for (; i < nl; i++) {
            char a = p[i], b = name[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (i != nl) continue;                                // name didn't match
        const char* v = p + nl;
        while (v < hdr_end && (*v == ' ' || *v == '\t')) v++;  // skip leading whitespace
        uint32_t o = 0;
        while (v < hdr_end && *v != '\r' && *v != '\n' && o + 1 < cap) out[o++] = *v++;
        out[o] = '\0';
        return;
    }
}

// resp: status code/text plus a heap-allocated, de-chunked body. Does NOT free buf; the
// caller owns it. Returns 0 on success (body possibly NULL/0), -1 on a malformed response.
int http_parse_response(uint8_t* buf, uint32_t total, http_response_t* resp)
{
    // Initialise the body up front so EVERY return path (including the error
    // returns below) leaves resp->body well-defined — a caller that frees on a
    // -1 return then never touches an uninitialised pointer.
    resp->body = NULL;
    resp->body_len = 0;

    // Parse status line: "HTTP/1.1 200 OK\r\n"
    char* line_end = strstr((char*)buf, "\r\n");
    if (!line_end) return -1;

    const char* status = (const char*)buf;
    while (*status && *status != ' ') status++;
    if (*status) status++;
    resp->status_code = atoi(status);

    // Copy status text (e.g. "OK")
    resp->status_text[0] = '\0';
    resp->location[0] = '\0';
    const char* st = status;
    while (*st && *st != '\r' && *st != '\n') st++;
    while (*status && *status != ' ' && status < st) status++;
    if (*status == ' ') status++;
    int st_len = (int)(st - status);
    if (st_len > 63) st_len = 63;
    if (st_len > 0) {
        __builtin_memcpy(resp->status_text, status, (size_t)st_len);   // st_len is bounded [1,63] here
        resp->status_text[st_len] = '\0';
    }

    // Find double CRLF separating headers from body
    char* body_start = strstr((char*)buf, "\r\n\r\n");
    if (!body_start) { resp->body = NULL; resp->body_len = 0; return 0; }
    body_start += 4;

    // Parse Content-Length
    uint32_t content_length = 0;
    char* headers_end = body_start - 4;
    http_get_header((char*)buf, headers_end, "location:", resp->location, sizeof(resp->location));   // for 3xx redirects
    char* cl = strstr((char*)buf, "Content-Length:");
    if (!cl || cl >= headers_end) cl = strstr((char*)buf, "Content-length:");
    if (!cl || cl >= headers_end) cl = strstr((char*)buf, "content-length:");
    if (cl && cl < headers_end) {
        cl += 15; // skip "Content-Length:"
        while (*cl == ' ') cl++;
        content_length = 0;
        while (*cl >= '0' && *cl <= '9') { content_length = content_length * 10 + (uint32_t)(*cl - '0'); cl++; }
    }

    uint32_t body_avail = total - (uint32_t)(body_start - (char*)buf);
    if (content_length > 0 && content_length < body_avail)
        body_avail = content_length;

    // Transfer-Encoding: chunked — decode the chunk framing so callers see the real body.
    int chunked = 0;
    char* te = strstr((char*)buf, "Transfer-Encoding:");
    if (!te || te >= headers_end) te = strstr((char*)buf, "transfer-encoding:");
    if (te && te < headers_end) {
        char* teol = strstr(te, "\r\n");
        if ((strstr(te, "chunked") && (!teol || strstr(te, "chunked") < teol)))
            chunked = 1;
    }

    if (chunked) {
        uint8_t* out = (uint8_t*)kmalloc(body_avail + 1);
        if (!out) return -1;
        uint32_t olen = 0;
        char* p = body_start;
        char* end = (char*)buf + total;
        while (p < end) {
            uint32_t sz = 0; int any = 0;
            while (p < end) {
                char c = *p; int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = (c - 'a') + 10;
                else if (c >= 'A' && c <= 'F') d = (c - 'A') + 10;
                else break;
                sz = sz * 16 + (uint32_t)d; p++; any = 1;
            }
            while (p < end && *p != '\n') p++;      // skip chunk-ext + CR up to LF
            if (p < end) p++;                        // consume LF
            if (!any || sz == 0) break;              // 0-size chunk terminates
            if (sz > body_avail - olen) sz = body_avail - olen;   // overflow-safe: olen <= body_avail is invariant, so this never does the wrapping `olen + sz` (a server-controlled huge chunk size could wrap it and skip the clamp -> heap overflow of `out`)
            for (uint32_t k = 0; k < sz && p < end; k++) out[olen++] = (uint8_t)*p++;
            if (p < end && *p == '\r') p++;          // trailing CRLF after data
            if (p < end && *p == '\n') p++;
        }
        out[olen] = '\0';
        resp->body = out;
        resp->body_len = olen;
        return 0;
    }

    resp->body = (uint8_t*)kmalloc(body_avail + 1);
    if (!resp->body) return -1;
    __builtin_memcpy(resp->body, body_start, body_avail);
    resp->body[body_avail] = '\0';
    resp->body_len = body_avail;
    return 0;
}

int http_request(const char* host, uint16_t port, const char* path, const char* method,
                 const uint8_t* body, uint32_t body_len, http_response_t* resp, int iface_idx)
{
    if (!host || !path || !resp) return -1;
    if (!method) method = "GET";

    uint32_t dst_ip = dns_resolve(host, iface_idx);
    if (!dst_ip) return -1;

    int conn = tcp_connect(dst_ip, port, 12346);
    if (conn < 0) return -1;

    // Drive the 3-way handshake to completion before sending (tcp_connect only
    // fires the SYN; the SYN-ACK is processed in tcp_handle_packet on poll).
    // Time-based (the timer is live), so a lost SYN gets retransmitted (tcp_tick
    // runs inside kernel_poll_net) and still connects within the window.
    uint32_t hs_deadline = get_ticks() + 4000;
    int established = 0;
    while ((int32_t)(get_ticks() - hs_deadline) < 0) {
        kernel_poll_net();
        if (tcp_state(conn) == TCP_STATE_ESTABLISHED) { established = 1; break; }
    }
    if (!established) { tcp_close(conn); return -1; }

    char req[512];
    int req_len;
    if (body && body_len > 0) {                       // POST/PUT with a form body
        req_len = snprintf(req, sizeof(req),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %u\r\n"
            "\r\n", method, path, host, body_len);
    } else {
        req_len = snprintf(req, sizeof(req),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "\r\n", method, path, host);
    }
    // snprintf returns the would-be length: if the request didn't fit in req[] it was truncated,
    // but req_len can still exceed sizeof(req). Clamp so tcp_send never reads past the stack buffer
    // (a long host/path — e.g. from an attacker's redirect Location — must not leak stack memory).
    if (req_len < 0) { tcp_close(conn); return -1; }
    if (req_len >= (int)sizeof(req)) req_len = (int)sizeof(req) - 1;

    if (tcp_send(conn, (const uint8_t*)req, (uint32_t)req_len) < 0) {
        tcp_close(conn);
        return -1;
    }
    if (body && body_len > 0 && tcp_send(conn, body, body_len) < 0) {
        tcp_close(conn);
        return -1;
    }

    uint8_t* buf = (uint8_t*)kmalloc(HTTP_MAX_RESPONSE);
    if (!buf) { tcp_close(conn); return -1; }
    uint32_t total = 0;

    // Receive until the peer closes (Connection: close -> FIN), the full
    // Content-Length body is in, or a timeout. Everything is time-based off
    // get_ticks() so a slow multi-segment response isn't truncated: the old code
    // used a fixed iteration count, and a normal gap between TCP segments tripped
    // it after a single partial segment (that's why large replies came back
    // empty). ACKs we send on each inbound segment keep the server streaming.
    uint32_t start   = get_ticks();
    uint32_t last_rx = start;
    for (;;) {
        kernel_poll_net();
        int n = tcp_recv(conn, buf + total, HTTP_MAX_RESPONSE - total - 1);
        if (n > 0) {
            total += (uint32_t)n;
            last_rx = get_ticks();
            if (total >= HTTP_MAX_RESPONSE - 1) break;
        } else if (n < 0) {
            break;                       // peer closed the connection (complete)
        }
        buf[total] = '\0';
        if (total > 0 && http_body_complete(buf, total)) break;
        uint32_t now = get_ticks();
        if (total == 0) {
            if ((int32_t)(now - (start + 5000)) >= 0) break;    // nothing arrived
        } else if ((int32_t)(now - (last_rx + 1500)) >= 0) {
            break;                                              // stream went quiet
        }
        if ((int32_t)(now - (start + 15000)) >= 0) break;       // hard 15 s cap
    }
    buf[total] = '\0';
    tcp_close(conn);

    if (total == 0) { kfree(buf); return -1; }

    int pr = http_parse_response(buf, total, resp);
    kfree(buf);
    return pr;
}

int http_get(const char* host, uint16_t port, const char* path,
             http_response_t* resp, int iface_idx)
{
    return http_request(host, port, path, "GET", NULL, 0, resp, iface_idx);
}

void http_free(http_response_t* resp)
{
    if (resp->body) { kfree(resp->body); resp->body = NULL; }
    resp->body_len = 0;
}

// Adversarial/robustness self-test for http_parse_response(). It now sits on the
// xbm package-fetch path (v6.4.73), parsing UNTRUSTED server data, so this feeds
// it crafted responses — well-formed, chunked, header-less, status-less, and
// HOSTILE (oversized / hex-overflow chunk sizes, Content-Length far larger than
// the actual body) — and asserts the parser extracts the right fields AND never
// over-reads the input or overruns the heap body it allocates. Returns 0 if all
// cases pass, else the number of failures. Mirrors image_reject_selftest().
static int hp_case(const char* s, http_response_t* r) {
    static uint8_t buf[512];
    uint32_t n = (uint32_t)strlen(s);
    if (n >= sizeof(buf)) return -999;
    __builtin_memcpy(buf, s, n);
    buf[n] = '\0';
    return http_parse_response(buf, n, r);
}

int http_parse_selftest(void) {
    int fails = 0;
    http_response_t r;

    // 1) Plain 200 + Content-Length body.
    if (hp_case("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", &r) != 0) fails++;
    else {
        if (r.status_code != 200 || strcmp(r.status_text, "OK") != 0) fails++;
        if (r.body_len != 5 || !r.body || strncmp((char*)r.body, "hello", 5) != 0) fails++;
        http_free(&r);
    }

    // 2) Transfer-Encoding: chunked — must de-chunk to "hello world".
    if (hp_case("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n", &r) != 0) fails++;
    else {
        if (r.body_len != 11 || !r.body || strncmp((char*)r.body, "hello world", 11) != 0) fails++;
        http_free(&r);
    }

    // 3) HOSTILE: a chunk size of 0xFFFFFFFF must be clamped, not overrun.
    if (hp_case("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "FFFFFFFF\r\nAB\r\n0\r\n\r\n", &r) != 0) fails++;
    else {
        if (r.body_len > 32) fails++;           // bounded by the tiny body region
        http_free(&r);
    }

    // 4) HOSTILE: a hex chunk size that overflows uint32 must stay safe.
    if (hp_case("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                "100000000000\r\nX\r\n0\r\n\r\n", &r) != 0) fails++;
    else {
        if (r.body_len > 32) fails++;
        http_free(&r);
    }

    // 5) Header block with no terminating CRLFCRLF -> success, empty body.
    if (hp_case("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n", &r) != 0) fails++;
    else {
        if (r.body != NULL || r.body_len != 0) fails++;
        http_free(&r);
    }

    // 6) No status line at all (no CRLF) -> rejected.
    if (hp_case("garbage with no CRLF", &r) != -1) fails++;

    // 7) 3xx: the Location header is extracted for the redirect follower.
    if (hp_case("HTTP/1.1 302 Found\r\nLocation: http://a.b/c\r\nContent-Length: 0\r\n\r\n", &r) != 0) fails++;
    else {
        if (r.status_code != 302 || strcmp(r.location, "http://a.b/c") != 0) fails++;
        http_free(&r);
    }

    // 8) Content-Length far larger than the real body -> body bounded to actual (no over-read).
    if (hp_case("HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\nhi", &r) != 0) fails++;
    else {
        if (r.body_len != 2 || !r.body || strncmp((char*)r.body, "hi", 2) != 0) fails++;
        http_free(&r);
    }

    printf("[HTTPPARSE] %s (%d failure%s across 8 cases)\n",
           fails == 0 ? "all cases passed" : "FAILURES", fails, fails == 1 ? "" : "s");
    return fails;
}
