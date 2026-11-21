#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/reboot.h>

/* One program, three names: reboot, poweroff and halt (see ALIASES in mk/main.mk). */
int main(int argc, char **argv)
{
    const char *name = argv[0] ? argv[0] : "reboot", *slash = strrchr(name, '/');
    int cmd = RB_AUTOBOOT;

    (void)argc;
    if (slash)
        name = slash + 1;
    if (!strcmp(name, "poweroff"))
        cmd = RB_POWER_OFF;
    else if (!strcmp(name, "halt"))
        cmd = RB_HALT_SYSTEM;

    if (reboot(cmd) < 0) {
        if (errno == EPERM)
            dprintf(2, "%s: must be root\n", name);
        else
            dprintf(2, "%s: failed (errno %d)\n", name, errno);
        return 1;
    }
    return 0;
}
