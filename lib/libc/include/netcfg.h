#ifndef _LIBC_NETCFG_H
#define _LIBC_NETCFG_H

/* Same layout as the kernel's struct netinfo (see netinfo()). Addresses are host order. */
struct netcfg {
    unsigned int ip, mask, gw, dns;
    unsigned char  mac[6];
    unsigned short up;
};

/* Root only. Sets a static address and marks the interface configured. */
int netset(const struct netcfg *cfg);

/* Parse "a.b.c.d" into host order. Returns 0, or -1 if the text is not an IPv4 address. */
int ip_parse(const char *text, unsigned int *out);

#endif
