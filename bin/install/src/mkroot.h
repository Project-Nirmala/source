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

/*
 * Creates a lemonfs on the partition [first, first + nsect) of fd and fills it
 * with the directories and /etc/passwd of the installed system. Passwords are
 * stored as salted SHA-256, the same scheme login(1) checks. Returns 0 or -1.
 */
int mkroot(int fd, uint64_t first, uint64_t nsect, const struct account *acc, unsigned nacc);

#endif
