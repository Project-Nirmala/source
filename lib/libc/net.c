#include <sys/socket.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static long raw6(long n, long a, long b, long c, long d, long e, long f)
{
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;

    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

int socket(int d, int t, int p) { return (int)raw6(SYS_socket, d, t, p, 0, 0, 0); }
int connect(int fd, const void *a, unsigned l) { return (int)raw6(SYS_connect, fd, (long)a, l, 0, 0, 0); }
int bind(int fd, const void *a, unsigned l) { return (int)raw6(SYS_bind, fd, (long)a, l, 0, 0, 0); }
int netinfo(struct netinfo *o) { return (int)raw6(SYS_netinfo, (long)o, 0, 0, 0, 0, 0); }

ssize_t sendto(int fd, const void *b, size_t n, int fl, const void *a, unsigned l)
{
    return raw6(SYS_sendto, fd, (long)b, (long)n, fl, (long)a, l);
}

ssize_t recvfrom(int fd, void *b, size_t n, int fl, void *a, unsigned *l)
{
    return raw6(SYS_recvfrom, fd, (long)b, (long)n, fl, (long)a, (long)l);
}

static int dotted(const char *s, unsigned *ip)
{
    unsigned v = 0, i;

    for (i = 0; i < 4; i++) {
        unsigned o = 0, d = 0;

        while (*s >= '0' && *s <= '9') {
            o = o * 10 + (unsigned)(*s++ - '0');
            d++;
        }
        if (!d || o > 255 || (i < 3 && *s++ != '.'))
            return 0;
        v = v << 8 | o;
    }
    if (*s)
        return 0;
    *ip = htonl(v);
    return 1;
}

static int skip_name(const unsigned char *p, int i, int n)
{
    while (i < n) {
        if (p[i] == 0)
            return i + 1;
        if ((p[i] & 0xC0) == 0xC0)
            return i + 2;
        i += p[i] + 1;
    }
    return -1;
}

int resolve(const char *host, unsigned *ip)
{
    unsigned char q[300], r[512];
    struct netinfo ni;
    struct sockaddr_in sa;
    int fd, i, n = 12, try, an, off;
    const char *s = host;

    if (dotted(host, ip))
        return 0;
    if (netinfo(&ni) < 0)
        return -1;
    memset(q, 0, 12);
    q[0] = 0x4E; q[1] = 0x52; q[2] = 1; q[5] = 1;
    while (*s) {
        const char *e = strchr(s, '.');
        int l = e ? (int)(e - s) : (int)strlen(s);

        if (l < 1 || l > 63 || n + l + 6 > (int)sizeof(q))
            return -1;
        q[n++] = (unsigned char)l;
        memcpy(q + n, s, (size_t)l);
        n += l;
        s += l + (e ? 1 : 0);
    }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 1; q[n++] = 0; q[n++] = 1;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    sa.sin_addr = htonl(ni.dns);
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    for (try = 0; try < 3; try++) {
        long len;

        if (sendto(fd, q, (size_t)n, 0, &sa, sizeof(sa)) < 0)
            break;
        len = recvfrom(fd, r, sizeof(r), 0, NULL, NULL);
        if (len < 12 || r[0] != q[0] || r[1] != q[1] || (r[3] & 15))
            continue;
        an = r[6] << 8 | r[7];
        off = skip_name(r, 12, (int)len);
        if (off < 0)
            break;
        off += 4;
        for (i = 0; i < an && off + 10 <= len; i++) {
            int rl;

            off = skip_name(r, off, (int)len);
            if (off < 0 || off + 10 > len)
                break;
            rl = r[off + 8] << 8 | r[off + 9];
            if (r[off] == 0 && r[off + 1] == 1 && rl == 4 && off + 14 <= len) {
                memcpy(ip, r + off + 10, 4);
                close(fd);
                return 0;
            }
            off += 10 + rl;
        }
        break;
    }
    close(fd);
    return -1;
}
