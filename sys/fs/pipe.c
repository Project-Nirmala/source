#include <vfs.h>
#include <file.h>
#include <proc.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>
#include <sys/fcntl.h>

#define PIPE_SIZE 4096

struct pipe {
    uint8_t      buf[PIPE_SIZE];
    size_t       head;
    size_t       count;
    int          readers;
    int          writers;
    struct vnode rd;
    struct vnode wr;
};

static long pipe_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    struct pipe *p = vn->priv;
    uint8_t *out = buf;
    size_t got = 0;

    (void)off;
    for (;;) {
        if (p->count > 0)
            break;
        if (p->writers == 0)
            return 0;
        if (curproc->pending_sig)
            return -EINTR;
        proc_wait_on(p);
    }
    while (got < n && p->count > 0) {
        out[got++] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_SIZE;
        p->count--;
    }
    proc_wakeup(p);
    return (long)got;
}

static long pipe_write(struct vnode *vn, uint64_t off, const void *buf, size_t n)
{
    struct pipe *p = vn->priv;
    const uint8_t *in = buf;
    size_t done = 0;

    (void)off;
    while (done < n) {
        if (p->readers == 0)
            return done ? (long)done : -EPIPE;
        if (p->count == PIPE_SIZE) {
            if (curproc->pending_sig)
                return done ? (long)done : -EINTR;
            proc_wait_on(p);
            continue;
        }
        while (done < n && p->count < PIPE_SIZE) {
            p->buf[(p->head + p->count) % PIPE_SIZE] = in[done++];
            p->count++;
        }
        proc_wakeup(p);
    }
    return (long)done;
}

static void pipe_release(struct vnode *vn)
{
    struct pipe *p = vn->priv;

    if (vn == &p->rd)
        p->readers--;
    else
        p->writers--;
    proc_wakeup(p);
    if (p->readers == 0 && p->writers == 0)
        kfree(p);
}

static const struct vnops pipe_ops = {
    .read = pipe_read,
    .write = pipe_write,
    .release = pipe_release,
};

int pipe_create(struct file **rd, struct file **wr)
{
    struct pipe *p = kzalloc(sizeof(*p));

    if (!p)
        return -ENOMEM;
    p->readers = 1;
    p->writers = 1;
    p->rd.type = VPIPE;
    p->rd.ops = &pipe_ops;
    p->rd.priv = p;
    p->rd.ino = vfs_next_ino();
    p->wr = p->rd;
    p->wr.ino = vfs_next_ino();

    *rd = file_new(&p->rd, O_RDONLY);
    *wr = file_new(&p->wr, O_WRONLY);
    if (!*rd || !*wr) {
        if (*rd)
            kfree(*rd);
        if (*wr)
            kfree(*wr);
        kfree(p);
        return -ENOMEM;
    }
    return 0;
}
