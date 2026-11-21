#include <net.h>
#include <proc.h>
#include <mm.h>
#include <kern.h>
#include <vfs.h>
#include <uaccess.h>
#include <sys/errno.h>
#include <sys/fcntl.h>

struct sockaddr_in {
    uint16_t family, port;
    uint32_t addr;
    uint8_t  zero[8];
};

static long sk_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    (void)off;
    return sock_recv(vn->priv, buf, n, NULL, NULL);
}

static long sk_write(struct vnode *vn, uint64_t off, const void *buf, size_t n)
{
    (void)off;
    return sock_send(vn->priv, buf, n, 0, 0, 0);
}

static void sk_release(struct vnode *vn)
{
    sock_free(vn->priv);
    kfree(vn);
}

static const struct vnops sock_ops = { sk_read, sk_write, NULL, sk_release };

static struct sock *get_sock(uint64_t fd)
{
    struct file *f = fd_get((int)fd);

    return f && f->vn->ops == &sock_ops ? f->vn->priv : NULL;
}

static long get_addr(uint64_t uva, uint64_t len, uint32_t *addr, uint16_t *port)
{
    struct sockaddr_in sa;

    if (len < 8)
        return -EINVAL;
    if (copy_from_user(&sa, uva, 8) < 0)
        return -EFAULT;
    if (sa.family != 2)
        return -EAFNOSUPPORT;
    *port = __builtin_bswap16(sa.port);
    *addr = __builtin_bswap32(sa.addr);
    return 0;
}

long sys_socket(uint64_t domain, uint64_t type, uint64_t proto)
{
    struct sock *s;
    struct vnode *vn;
    struct file *f;
    long r;
    int fd;

    (void)proto;
    if (domain != 2)
        return -EAFNOSUPPORT;
    if ((type & 0xF) < 1 || (type & 0xF) > 3)
        return -EPROTONOSUPPORT;
    r = net_up();
    if (r < 0)
        return r;
    s = sock_new((int)(type & 0xF));
    vn = kzalloc(sizeof(*vn));
    if (!s || !vn) {
        if (s)
            sock_free(s);
        kfree(vn);
        return -ENOMEM;
    }
    vn->type = VCHR;
    vn->ops = &sock_ops;
    vn->priv = s;
    vn->nlink = 1;
    vn->mode = 0666;
    vn->ino = vfs_next_ino();
    memcpy(vn->name, "socket", 7);
    f = file_new(vn, O_RDWR);
    if (!f) {
        sock_free(s);
        kfree(vn);
        return -ENOMEM;
    }
    fd = fd_install(curproc, f);
    if (fd < 0)
        file_unref(f);
    return fd;
}

long sys_connect(uint64_t fd, uint64_t addr_uva, uint64_t len)
{
    struct sock *s = get_sock(fd);
    uint32_t a;
    uint16_t p;
    long r;

    if (!s)
        return -EBADF;
    r = get_addr(addr_uva, len, &a, &p);
    return r < 0 ? r : sock_connect(s, a, p);
}

long sys_bind(uint64_t fd, uint64_t addr_uva, uint64_t len)
{
    struct sock *s = get_sock(fd);
    uint32_t a;
    uint16_t p;
    long r;

    if (!s)
        return -EBADF;
    r = get_addr(addr_uva, len, &a, &p);
    return r < 0 ? r : sock_bind(s, p);
}

long sys_sendto(uint64_t fd, uint64_t buf, uint64_t n, uint64_t flags, uint64_t addr_uva, uint64_t alen)
{
    struct sock *s = get_sock(fd);
    uint8_t tmp[1500];
    uint32_t a = 0;
    uint16_t p = 0;

    (void)flags;
    if (!s)
        return -EBADF;
    if (n > sizeof(tmp))
        n = sizeof(tmp);
    if (addr_uva) {
        long r = get_addr(addr_uva, alen, &a, &p);

        if (r < 0)
            return r;
    }
    if (copy_from_user(tmp, buf, n) < 0)
        return -EFAULT;
    return sock_send(s, tmp, n, a, p, addr_uva != 0);
}

long sys_recvfrom(uint64_t fd, uint64_t buf, uint64_t n, uint64_t flags, uint64_t addr_uva, uint64_t alen_uva)
{
    struct sock *s = get_sock(fd);
    uint8_t tmp[1500];
    uint32_t a = 0;
    uint16_t p = 0;
    long r;

    (void)flags;
    if (!s)
        return -EBADF;
    if (n > sizeof(tmp))
        n = sizeof(tmp);
    r = sock_recv(s, tmp, n, &a, &p);
    if (r < 0)
        return r;
    if (copy_to_user(buf, tmp, (size_t)r) < 0)
        return -EFAULT;
    if (addr_uva) {
        struct sockaddr_in sa;
        uint32_t l = 16;

        memset(&sa, 0, sizeof(sa));
        sa.family = 2;
        sa.port = __builtin_bswap16(p);
        sa.addr = __builtin_bswap32(a);
        if (copy_to_user(addr_uva, &sa, sizeof(sa)) < 0 ||
            (alen_uva && copy_to_user(alen_uva, &l, 4) < 0))
            return -EFAULT;
    }
    return r;
}

long sys_netinfo(uint64_t uva)
{
    struct netinfo ni;
    long r = net_up();

    if (r < 0)
        return r;
    net_info(&ni);
    return copy_to_user(uva, &ni, sizeof(ni)) < 0 ? -EFAULT : 0;
}
