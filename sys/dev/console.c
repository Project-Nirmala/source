#include <vfs.h>
#include <tty.h>

static long console_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    (void)vn;
    (void)off;
    return tty_read(buf, n);
}

static long console_write(struct vnode *vn, uint64_t off, const void *buf, size_t n)
{
    (void)vn;
    (void)off;
    return tty_write(buf, n);
}

static long console_ioctl(struct vnode *vn, uint64_t req, uint64_t arg)
{
    (void)vn;
    return tty_ioctl(req, arg);
}

static const struct vnops console_ops = {
    .read = console_read,
    .write = console_write,
    .ioctl = console_ioctl,
};

struct vnode console_vnode = {
    .type = VCHR,
    .ino = 2,
    .ops = &console_ops,
    .name = "console",
};
