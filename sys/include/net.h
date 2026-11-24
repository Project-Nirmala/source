#ifndef _NIRMALA_NET_H
#define _NIRMALA_NET_H

#include <types.h>

struct netinfo {
    uint32_t ip, mask, gw, dns;
    uint8_t  mac[6];
    uint16_t up;
};

bool e1000_probe(uint8_t mac[6]);
int  e1000_send(const void *frame, size_t len);
void e1000_poll(void (*rx)(const uint8_t *, size_t));

struct sock;

bool vnet_probe(uint8_t mac[6]);
int  vnet_send(const void *frame, size_t len);
void vnet_poll(void (*rx)(const uint8_t *, size_t));

void net_init(void);
void net_poll(void);
int  net_up(void);
void net_info(struct netinfo *out);
int  net_set_static(uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns);

struct sock *sock_new(int type);
void         sock_free(struct sock *s);
int          sock_connect(struct sock *s, uint32_t addr, uint16_t port);
int          sock_bind(struct sock *s, uint16_t port);
long         sock_send(struct sock *s, const void *buf, size_t n, uint32_t addr, uint16_t port, int hasdst);
long         sock_recv(struct sock *s, void *buf, size_t n, uint32_t *addr, uint16_t *port);

#endif
