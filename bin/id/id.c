#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("uid=%u gid=%u\n", (unsigned)getuid(), (unsigned)getgid());
    return 0;
}
