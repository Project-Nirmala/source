#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv)
{
    int pid = (int)getpid();
    long i;

    if (argc < 2) {
        printf("[pid %d utest] usage: utest sleep|spin|fault\n", pid);
        return 2;
    }
    printf("[pid %d utest] mode %s\n", pid, argv[1]);

    if (!strcmp(argv[1], "sleep")) {
        for (i = 1; i <= 3; i++) {
            printf("[pid %d sleeper] woke up, round %d\n", pid, (int)i);
            usleep(300000);
        }
    } else if (!strcmp(argv[1], "fault")) {
        printf("[pid %d faulter] writing to unmapped address 0x1000\n", pid);
        *(volatile int *)0x1000 = 1;
        printf("[pid %d faulter] unreachable\n", pid);
    } else {
        volatile unsigned long x = 0;
        unsigned long k;

        for (i = 1; i <= 4; i++) {
            for (k = 0; k < 20000000UL; k++)
                x++;
	printf("[pid %d spinner] round %d done (x=%lu)\n", pid, (int)i, x);
        }
    }
    return 0;
}
