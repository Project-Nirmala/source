#ifndef _LIBC_PWD_H
#define _LIBC_PWD_H

#include <sys/types.h>

struct passwd {
    char *pw_name;
    char *pw_dir;
    char *pw_shell;
    uid_t pw_uid;
    gid_t pw_gid;
};

struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uid_t uid);

#endif
