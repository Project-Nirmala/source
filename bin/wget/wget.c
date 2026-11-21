#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

int main(int argc, char **argv)
{
    char host[128], req[512], buf[1024], *p, *path = "/", *hdr_end;
    struct sockaddr_in sa;
    unsigned ip;
    int fd, port = 80, hl = 0, body = 0;
    long n;

    if (argc != 2) {
        fputs("usage: wget http://host[:port]/path\n", stderr);
        return 1;
    }
    p = argv[1];
    if (!strncmp(p, "http://", 7))
        p += 7;
    n = (long)strcspn(p, "/");
    if (n >= (long)sizeof(host))
        return 1;
    memcpy(host, p, (size_t)n);
    host[n] = 0;
    if (p[n])
        path = p + n;
    if ((p = strchr(host, ':')) != NULL) {
        *p = 0;
        port = atoi(p + 1);
    }
    if (resolve(host, &ip) < 0) {
        fprintf(stderr, "wget: cannot resolve %s\n", host);
        return 1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr = ip;
    if (fd < 0 || connect(fd, &sa, sizeof(sa)) < 0) {
        fprintf(stderr, "wget: cannot connect to %s\n", host);
        return 1;
    }
    n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    write(fd, req, (size_t)n);
    while ((n = read(fd, buf + hl, sizeof(buf) - 1 - (size_t)hl)) > 0) {
        if (body) {
            write(1, buf, (size_t)n);
            continue;
        }
        hl += (int)n;
        buf[hl] = 0;
        hdr_end = strstr(buf, "\r\n\r\n");
        if (hdr_end) {
            hdr_end += 4;
            write(1, hdr_end, (size_t)(buf + hl - hdr_end));
            body = 1;
            hl = 0;
        } else if (hl > (int)sizeof(buf) - 2) {
            hl = 0;
        }
    }
    close(fd);
    return 0;
}
