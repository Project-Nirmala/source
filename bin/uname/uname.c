#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

int main(int argc, char **argv)
{
    struct utsname u;
    const char *opt = argc > 1 ? argv[1] : "-s";

    if (uname(&u) < 0)
        return 1;
    if (!strcmp(opt, "-a"))
        printf("%s %s %s %s %s\n", u.sysname, u.nodename, u.release, u.version, u.machine);
    else if (!strcmp(opt, "-n"))
        printf("%s\n", u.nodename);
    else if (!strcmp(opt, "-r"))
        printf("%s\n", u.release);
    else if (!strcmp(opt, "-v"))
        printf("%s\n", u.version);
    else if (!strcmp(opt, "-m"))
        printf("%s\n", u.machine);
    else
        printf("%s\n", u.sysname);
    return 0;
}
