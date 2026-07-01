#include "kernel.h"
#include "http.h"
#include "tcp.h"
#include "dns.h"

int http_get(const char* host, uint16_t port, const char* path,
             http_response_t* resp, int iface_idx)
{
    if (!host || !path || !resp) return -1;

    uint32_t dst_ip = dns_resolve(host, iface_idx);
    if (!dst_ip) return -1;

    int conn = tcp_connect(dst_ip, port, 12346);
    if (conn < 0) return -1;

    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n", path, host);

    if (tcp_send(conn, (const uint8_t*)req, req_len) < 0) {
        tcp_close(conn);
        return -1;
    }

    uint8_t* buf = (uint8_t*)kmalloc(HTTP_MAX_RESPONSE);
    if (!buf) { tcp_close(conn); return -1; }
    uint32_t total = 0;

    for (int tries = 0; tries < 100; tries++) {
        kernel_poll_net();
        int n = tcp_recv(conn, buf + total, HTTP_MAX_RESPONSE - total - 1);
        if (n > 0) {
            total += n;
            if (total >= HTTP_MAX_RESPONSE - 1) break;
        }
        if (n < 0) break;
        for (volatile int d = 0; d < 50000; d++) __asm__ volatile("pause");
    }
    buf[total] = '\0';
    tcp_close(conn);

    if (total == 0) { kfree(buf); return -1; }

    // Parse status line: "HTTP/1.1 200 OK\r\n"
    char* line_end = strstr((char*)buf, "\r\n");
    if (!line_end) { kfree(buf); return -1; }

    const char* status = (const char*)buf;
    while (*status && *status != ' ') status++;
    if (*status) status++;
    resp->status_code = atoi(status);

    // Copy status text (e.g. "OK")
    resp->status_text[0] = '\0';
    const char* st = status;
    while (*st && *st != '\r' && *st != '\n') st++;
    while (*status && *status != ' ' && status < st) status++;
    if (*status == ' ') status++;
    int st_len = (int)(st - status);
    if (st_len > 63) st_len = 63;
    if (st_len > 0) {
        __builtin_memcpy(resp->status_text, status, st_len);
        resp->status_text[st_len] = '\0';
    }

    // Find double CRLF separating headers from body
    char* body_start = strstr((char*)buf, "\r\n\r\n");
    if (!body_start) { kfree(buf); resp->body = NULL; resp->body_len = 0; return 0; }
    body_start += 4;

    // Parse Content-Length
    uint32_t content_length = 0;
    char* headers_end = body_start - 4;
    char* cl = strstr((char*)buf, "Content-Length:");
    if (!cl || cl >= headers_end) cl = strstr((char*)buf, "Content-length:");
    if (!cl || cl >= headers_end) cl = strstr((char*)buf, "content-length:");
    if (cl && cl < headers_end) {
        cl += 15; // skip "Content-Length:"
        while (*cl == ' ') cl++;
        content_length = 0;
        while (*cl >= '0' && *cl <= '9') {
            content_length = content_length * 10 + (*cl - '0');
            cl++;
        }
    }

    uint32_t body_avail = total - (uint32_t)(body_start - (char*)buf);
    if (content_length > 0 && content_length < body_avail)
        body_avail = content_length;

    resp->body = (uint8_t*)kmalloc(body_avail + 1);
    if (!resp->body) { kfree(buf); return -1; }
    __builtin_memcpy(resp->body, body_start, body_avail);
    resp->body[body_avail] = '\0';
    resp->body_len = body_avail;

    kfree(buf);
    return 0;
}

void http_free(http_response_t* resp)
{
    if (resp->body) { kfree(resp->body); resp->body = NULL; }
    resp->body_len = 0;
}
