#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int s;

    if (argc != 2) {
        dprintf(2, "Usage: sleep [N]...\n");
        return 1;
    }
    if (argv[1][0] < '0' || argv[1][0] > '9') {
        dprintf(2, "sleep: invalid number '%s'\n", argv[1]);
        return 1;
    }
    for (s = atoi(argv[1]); s > 0; s--)
        usleep(1000000);
    return 0;
}
