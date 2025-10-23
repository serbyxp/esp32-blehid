#ifndef LWIP_INET_H
#define LWIP_INET_H

#include <stdint.h>
#include <stdio.h>

#define IPSTR "%u.%u.%u.%u"

typedef struct
{
    uint32_t addr;
} ip4_addr_t;

#define IP2STR(ipaddr)                                                                 \
    (int)(((const ip4_addr_t *)(ipaddr))->addr >> 24 & 0xFF),                          \
    (int)(((const ip4_addr_t *)(ipaddr))->addr >> 16 & 0xFF),                          \
    (int)(((const ip4_addr_t *)(ipaddr))->addr >> 8 & 0xFF),                           \
    (int)(((const ip4_addr_t *)(ipaddr))->addr & 0xFF)

static inline const char *ip4addr_ntoa(const ip4_addr_t *addr)
{
    static char buf[16];
    uint32_t ip = addr ? addr->addr : 0;
    ip4_addr_t tmp = {ip};
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&tmp));
    return buf;
}

#endif // LWIP_INET_H
