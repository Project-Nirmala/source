#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

static unsigned short cks(const unsigned char *p, int n)
{
    unsigned s = 0;
    int i;

    for (i = 0; i + 1 < n; i += 2)
        s += (unsigned)(p[i] << 8 | p[i + 1]);
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return (unsigned short)~s;
}

static long now_ms(void)
{
    struct timespec t;

    clock_gettime(1, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
    unsigned char pkt[40], rep[128];
    struct sockaddr_in sa;
    unsigned ip;
    int fd, i, got = 0, count = argc > 2 ? atoi(argv[2]) : 4;

    if (argc < 2) {
        fputs("usage: ping host [count]\n", stderr);
        return 1;
    }
    if (resolve(argv[1], &ip) < 0) {
        fprintf(stderr, "ping: cannot resolve %s\n", argv[1]);
        return 1;
    }
    fd = socket(AF_INET, SOCK_RAW, 1);
    if (fd < 0) {
        fputs("ping: no network\n", stderr);
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr = ip;
    printf("PING %s (%u.%u.%u.%u)\n", argv[1], ip & 255, ip >> 8 & 255, ip >> 16 & 255, ip >> 24);
    for (i = 0; i < count; i++) {
        long t0, r;
        unsigned short c;

        memset(pkt, 'a', sizeof(pkt));
        pkt[0] = 8;
        pkt[1] = 0;
        pkt[4] = 0x4E;
        pkt[5] = 0x50;
        pkt[6] = (unsigned char)(i >> 8);
        pkt[7] = (unsigned char)i;
        pkt[2] = pkt[3] = 0;
        c = cks(pkt, sizeof(pkt));
        pkt[2] = (unsigned char)(c >> 8);
        pkt[3] = (unsigned char)c;
        t0 = now_ms();
        sendto(fd, pkt, sizeof(pkt), 0, &sa, sizeof(sa));
        for (;;) {
            r = recvfrom(fd, rep, sizeof(rep), 0, NULL, NULL);
            if (r < 8) {
                printf("seq=%d timeout\n", i);
                break;
            }
            if (rep[0] == 0 && rep[4] == 0x4E && rep[5] == 0x50 && rep[7] == (unsigned char)i) {
                printf("%ld bytes from %s: seq=%d time=%ld ms\n", r, argv[1], i, now_ms() - t0);
                got++;
                break;
            }
        }
        if (i + 1 < count)
            usleep(1000000);
    }
    printf("%d sent, %d received\n", count, got);
    return got ? 0 : 1;
}
