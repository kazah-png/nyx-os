#ifndef DNS_H
#define DNS_H

uint32_t dns_resolve(const char* hostname, int iface_idx);
void dns_set_server(uint32_t ip);
uint32_t dns_get_server(void);

#endif