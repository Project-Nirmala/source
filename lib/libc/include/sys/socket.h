#ifndef _LIBC_SYS_SOCKET_H
#define _LIBC_SYS_SOCKET_H

#include <sys/types.h>
#include <stddef.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define htons(x) ((unsigned short)__builtin_bswap16(x))
#define ntohs(x) ((unsigned short)__builtin_bswap16(x))
#define htonl(x) ((unsigned)__builtin_bswap32(x))
#define ntohl(x) ((unsigned)__builtin_bswap32(x))

struct sockaddr_in {
    unsigned short sin_family, sin_port;
    unsigned       sin_addr;
    unsigned char  sin_zero[8];
};

struct netinfo {
    unsigned       ip, mask, gw, dns;
    unsigned char  mac[6];
    unsigned short up;
};

int     socket(int domain, int type, int proto);
int     connect(int fd, const void *addr, unsigned len);
int     bind(int fd, const void *addr, unsigned len);
ssize_t sendto(int fd, const void *buf, size_t n, int flags, const void *addr, unsigned len);
ssize_t recvfrom(int fd, void *buf, size_t n, int flags, void *addr, unsigned *len);
int     netinfo(struct netinfo *out);
int     resolve(const char *host, unsigned *ip);

#endif
