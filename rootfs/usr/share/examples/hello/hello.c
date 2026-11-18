#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *p = malloc(100);
    double d = strtod("3.25e2", 0);

    strcpy(p, "linked by nirld");
    printf("hello from %s (argc=%d, %s, strtod=%d)\n", p, argc, argc > 1 ? argv[1] : "-", (int)d);
    free(p);
    return 0;
}
