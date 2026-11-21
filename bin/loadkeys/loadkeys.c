#include <stdio.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        dprintf(2, "usage: loadkeys us|dvorak|colemak\n");
        return 2;
    }
    if (setkeymap(argv[1]) < 0) {
        dprintf(2, errno == EPERM ? "loadkeys: must be root\n" : "loadkeys: unknown keymap '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}
