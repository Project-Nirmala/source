#ifndef NIRMALA_MKROOT_H
#define NIRMALA_MKROOT_H

#include <stdint.h>

#define MKROOT_MAX_ACCOUNTS 4

struct account {
    char     name[32];
    char     password[128];
    uint32_t uid, gid;
    char     home[64];
    char     shell[32];
};

/* System-wide settings the installer stores under /etc of the new system. */
struct sysconfig {
    char keymap[16];     /* /etc/keymap   : applied by init (loadkeys) */
    char locale[32];     /* /etc/locale   : exported as LANG by login */
    char timezone[40];   /* /etc/timezone : exported as TZ by login */
    char hostname[32];   /* /etc/hostname : applied by init */
    int  net_static;     /* /etc/network  : "dhcp" or "static IP MASK GATEWAY DNS" */
    char ip[16], mask[16], gw[16], dns[16];
    char swap_partuuid[37]; /* /etc/swap    : "PARTUUID=<guid>" of the swap partition, if any */
};

/*
 * Creates a lemonfs on the partition [first, first + nsect) of fd and fills it
 * with the directories and /etc/passwd of the installed system. Passwords are
 * stored as salted SHA-256, the same scheme login(1) checks, in a file only root
 * can read. When cfg is not NULL its settings are written as one-line files in
 * /etc. Returns 0 or -1.
 */
int mkroot(int fd, uint64_t first, uint64_t nsect, const struct account *acc, unsigned nacc,
           const struct sysconfig *cfg);

#endif
