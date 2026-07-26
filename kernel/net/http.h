#ifndef HTTP_H
#define HTTP_H

#define HTTP_MAX_URL      512
#define HTTP_MAX_RESPONSE (64 * 1024)

typedef struct {
    int      status_code;
    char     status_text[64];
    uint8_t* body;
    uint32_t body_len;
} http_response_t;

int http_get(const char* host, uint16_t port, const char* path,
             http_response_t* resp, int iface_idx);
// General HTTP request. `method` (GET/POST/...); a non-NULL body is sent as
// application/x-www-form-urlencoded with a Content-Length. Same response handling as http_get.
int http_request(const char* host, uint16_t port, const char* path, const char* method,
                 const uint8_t* body, uint32_t body_len, http_response_t* resp, int iface_idx);
// Parse a raw HTTP/1.x response (buf[0..total), NUL-terminated at buf[total]) into resp,
// allocating a de-chunked resp->body (free via http_free). Does not take ownership of buf.
int http_parse_response(uint8_t* buf, uint32_t total, http_response_t* resp);
void http_free(http_response_t* resp);

#endif
