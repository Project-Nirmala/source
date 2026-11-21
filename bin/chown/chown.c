#include <stdio.h>
#include <unistd.h>

static int number(const char *s, unsigned *out)
{
    unsigned v = 0;

    if (!*s)
        return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (unsigned)(*s - '0');
    }
    *out = v;
    return 0;
}

/* chown UID[:GID] file... (numeric ids only; there is no user database library yet) */
int main(int argc, char **argv)
{
    char owner[24];
    unsigned uid, gid;
    int i, j, rc = 0, have_gid = 0;

    if (argc < 3) {
        dprintf(2, "usage: chown UID[:GID] file...\n");
        return 2;
    }
    for (i = 0; argv[1][i] && argv[1][i] != ':' && i < (int)sizeof(owner) - 1; i++)
        owner[i] = argv[1][i];
    owner[i] = 0;
    if (number(owner, &uid) < 0 || (argv[1][i] == ':' && (have_gid = 1, number(argv[1] + i + 1, &gid) < 0))) {
        dprintf(2, "chown: invalid owner '%s'\n", argv[1]);
        return 2;
    }
    for (j = 2; j < argc; j++)
        if (chown(argv[j], uid, have_gid ? gid : (unsigned)-1) < 0) {
            dprintf(2, "chown: cannot change '%s': Operation not permitted or no such file\n", argv[j]);
            rc = 1;
        }
    return rc;
}
