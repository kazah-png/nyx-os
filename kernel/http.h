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
void http_free(http_response_t* resp);

#endif
