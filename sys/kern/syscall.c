#include <sys/version.h>
#include <proc.h>
#include <blk.h>
#include <mm.h>
#include <kern.h>
#include <exec.h>
#include <file.h>
#include <vfs.h>
#include <uaccess.h>
#include <wallclock.h>
#include <sys/syscall.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <net.h>

#define IO_CHUNK 512

static void free_strv(char **v)
{
    int i;

    for (i = 0; v[i]; i++)
        kfree(v[i]);
    kfree(v);
}

static long copy_strv(uint64_t uva, char ***out)
{
    char **v = kzalloc((EXEC_MAX_ARGS + 1) * sizeof(char *));
    char tmp[EXEC_MAX_ARGLEN];
    long r = 0;
    int n = 0;

    if (!v)
        return -ENOMEM;
    while (uva) {
        uint64_t p;
        long len;

        if (n >= EXEC_MAX_ARGS) {
            r = -E2BIG;
            break;
        }
        r = copy_from_user(&p, uva + (uint64_t)n * 8, 8);
        if (r < 0 || !p)
            break;
        len = copy_str_from_user(tmp, p, sizeof(tmp));
        if (len < 0) {
            r = len == -ENAMETOOLONG ? -E2BIG : len;
            break;
        }
        v[n] = kmalloc((size_t)len + 1);
        if (!v[n]) {
            r = -ENOMEM;
            break;
        }
        memcpy(v[n], tmp, (size_t)len + 1);
        n++;
    }
    if (r < 0) {
        free_strv(v);
        return r;
    }
    *out = v;
    return n;
}

static long sys_write(uint64_t fd, uint64_t buf, uint64_t count)
{
    struct file *f = fd_get((int)fd);
    char tmp[IO_CHUNK];
    uint64_t done = 0;

    if (!f)
        return -EBADF;
    if ((f->flags & O_ACCMODE) == O_RDONLY)
        return -EBADF;
    if ((f->flags & O_APPEND) && f->vn->type == VREG)
        f->off = f->vn->size;
    while (done < count) {
        size_t n = count - done < sizeof(tmp) ? count - done : sizeof(tmp);
        long r = copy_from_user(tmp, buf + done, n);

        if (r == 0)
            r = vnode_write(f->vn, f->off, tmp, n);
        if (r < 0)
            return done ? (long)done : r;
        f->off += (uint64_t)r;
        done += (uint64_t)r;
        if ((size_t)r < n)
            break;
    }
    return (long)done;
}

static long sys_read(uint64_t fd, uint64_t buf, uint64_t count)
{
    struct file *f = fd_get((int)fd);
    char tmp[IO_CHUNK];
    uint64_t done = 0;

    if (!f)
        return -EBADF;
    if ((f->flags & O_ACCMODE) == O_WRONLY)
        return -EBADF;
    while (done < count) {
        size_t n = count - done < sizeof(tmp) ? count - done : sizeof(tmp);
        long r = vnode_read(f->vn, f->off, tmp, n);

        if (r < 0)
            return done ? (long)done : r;
        if (r == 0)
            break;
        if (copy_to_user(buf + done, tmp, (size_t)r) < 0)
            return done ? (long)done : -EFAULT;
        f->off += (uint64_t)r;
        done += (uint64_t)r;
        if ((size_t)r < n || f->vn->type == VCHR || f->vn->type == VPIPE)
            break;
    }
    return (long)done;
}

static long sys_open(uint64_t path_uva, uint64_t flags, uint64_t mode)
{
    char path[PATH_MAX];
    struct vnode *vn;
    struct file *f;
    long r;
    int fd, acc = (int)(flags & O_ACCMODE);

    r = copy_str_from_user(path, path_uva, sizeof(path));
    if (r < 0)
        return r;
    if (flags & O_CREAT)
        r = vfs_create(path, curproc->cwd, (uint32_t)mode & 0777, (flags & O_EXCL) != 0, &vn);
    else
        r = vfs_lookup(path, curproc->cwd, &vn);
    if (r < 0)
        return r;
    if (vn->type == VDIR && acc != O_RDONLY)
        return -EISDIR;
    if ((flags & O_DIRECTORY) && vn->type != VDIR)
        return -ENOTDIR;
    if (acc != O_RDONLY && (!vn->ops || !vn->ops->write))
        return -EROFS;
    if ((flags & O_TRUNC) && acc != O_RDONLY && vn->type == VREG) {
        r = vfs_truncate(vn, 0);
        if (r < 0)
            return r;
    }
    f = file_new(vn, (int)flags);
    if (!f)
        return -ENOMEM;
    fd = fd_install(curproc, f);
    if (fd < 0) {
        file_unref(f);
        return -EMFILE;
    }
    return fd;
}

static long sys_close(uint64_t fd)
{
    return fd_close((int)fd) < 0 ? -EBADF : 0;
}

static long sys_lseek(uint64_t fd, int64_t off, uint64_t whence)
{
    struct file *f = fd_get((int)fd);
    int64_t base;

    if (!f)
        return -EBADF;
    if (f->vn->type == VCHR || f->vn->type == VPIPE)
        return -ESPIPE;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = (int64_t)f->off; break;
    case SEEK_END: base = (int64_t)f->vn->size; break;
    default:       return -EINVAL;
    }
    if (base + off < 0)
        return -EINVAL;
    f->off = (uint64_t)(base + off);
    return (long)f->off;
}

static long sys_ioctl(uint64_t fd, uint64_t req, uint64_t arg)
{
    struct file *f = fd_get((int)fd);

    if (!f)
        return -EBADF;
    return vnode_ioctl(f->vn, req, arg);
}

static long sys_pipe(uint64_t fds_uva)
{
    struct file *rd, *wr;
    int fds[2], r;

    r = pipe_create(&rd, &wr);
    if (r < 0)
        return r;
    fds[0] = fd_install(curproc, rd);
    if (fds[0] < 0) {
        file_unref(rd);
        file_unref(wr);
        return -EMFILE;
    }
    fds[1] = fd_install(curproc, wr);
    if (fds[1] < 0) {
        fd_close(fds[0]);
        file_unref(wr);
        return -EMFILE;
    }
    if (copy_to_user(fds_uva, fds, sizeof(fds)) < 0) {
        fd_close(fds[0]);
        fd_close(fds[1]);
        return -EFAULT;
    }
    return 0;
}

static long sys_dup(uint64_t fd)
{
    struct file *f = fd_get((int)fd);
    int n;

    if (!f)
        return -EBADF;
    n = fd_install(curproc, file_ref(f));
    if (n < 0) {
        file_unref(f);
        return -EMFILE;
    }
    return n;
}

static long sys_dup2(uint64_t oldfd, uint64_t newfd)
{
    struct file *f = fd_get((int)oldfd);

    if (!f || newfd >= NOFILE)
        return -EBADF;
    if (oldfd == newfd)
        return (long)newfd;
    if (curproc->fds[newfd])
        fd_close((int)newfd);
    curproc->fds[newfd] = file_ref(f);
    return (long)newfd;
}

static long sys_wait4(int64_t pid, uint64_t status_uva, int options)
{
    int ws = 0;
    long r;

    if (pid < -1)
        return -EINVAL;
    r = proc_wait(pid == 0 ? -1 : (int)pid, options, &ws);
    if (r > 0 && status_uva && copy_to_user(status_uva, &ws, sizeof(ws)) < 0)
        return -EFAULT;
    return r;
}

static long sys_chdir(uint64_t path_uva)
{
    char path[PATH_MAX];
    struct vnode *vn;
    long r = copy_str_from_user(path, path_uva, sizeof(path));

    if (r < 0)
        return r;
    r = vfs_lookup(path, curproc->cwd, &vn);
    if (r < 0)
        return r;
    if (vn->type != VDIR)
        return -ENOTDIR;
    curproc->cwd = vn;
    return 0;
}

static long sys_getcwd(uint64_t buf_uva, uint64_t size)
{
    char path[PATH_MAX];
    long n = vfs_getpath(curproc->cwd, path, sizeof(path));

    if (n < 0)
        return n;
    if ((uint64_t)n + 1 > size)
        return -ERANGE;
    if (copy_to_user(buf_uva, path, (size_t)n + 1) < 0)
        return -EFAULT;
    return n + 1;
}

struct dirent_hdr {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
} __packed;

#define DIRENT_NAME_OFF 19

static long sys_getdents64(uint64_t fd, uint64_t buf_uva, uint64_t count)
{
    struct file *f = fd_get((int)fd);
    char tmp[1024];
    size_t used = 0, cap = count < sizeof(tmp) ? count : sizeof(tmp);

    if (!f)
        return -EBADF;
    if (f->vn->type != VDIR)
        return -ENOTDIR;
    for (;;) {
        struct vnode *e;
        char name[NAME_MAX + 1];
        struct dirent_hdr h;
        size_t nlen, reclen;

        if (!vfs_readdir(f->vn, f->off, &e, name))
            break;
        nlen = strlen(name);
        reclen = (DIRENT_NAME_OFF + nlen + 1 + 7) & ~(size_t)7;
        if (used + reclen > cap) {
            if (!used)
                return -EINVAL;
            break;
        }
        h.d_ino = e->ino;
        h.d_off = (int64_t)f->off + 1;
        h.d_reclen = (uint16_t)reclen;
        h.d_type = e->type == VDIR ? 4 : e->type == VCHR ? 2 : e->type == VBLK ? 6 : e->type == VPIPE ? 1 : 8;
        memset(tmp + used, 0, reclen);
        memcpy(tmp + used, &h, sizeof(h));
        memcpy(tmp + used + DIRENT_NAME_OFF, name, nlen + 1);
        used += reclen;
        f->off++;
    }
    if (used && copy_to_user(buf_uva, tmp, used) < 0)
        return -EFAULT;
    return (long)used;
}

struct sysinfo_out {
    int64_t  uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t pad2;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
    char     reserved[4];
};

static long sys_sysinfo(uint64_t uva)
{
    struct sysinfo_out si;

    memset(&si, 0, sizeof(si));
    si.uptime = (int64_t)(pit_ticks() / pit_hz());
    si.totalram = pmm_total_pages() * PAGE_SIZE;
    si.freeram = pmm_free_pages() * PAGE_SIZE;
    si.procs = (uint16_t)proc_count();
    si.mem_unit = 1;
    return copy_to_user(uva, &si, sizeof(si));
}

static long sys_nanosleep(uint64_t req)
{
    struct { int64_t sec; int64_t nsec; } ts;
    uint64_t ms, ticks;
    int r = copy_from_user(&ts, req, sizeof(ts));

    if (r < 0)
        return r;
    if (ts.sec < 0 || ts.nsec < 0 || ts.nsec >= 1000000000L)
        return -EINVAL;
    if (ts.sec > 3600)
        ts.sec = 3600;
    ms = (uint64_t)ts.sec * 1000 + (uint64_t)ts.nsec / 1000000;
    ticks = (ms * pit_hz() + 999) / 1000;
    proc_sleep_ticks(ticks);
    return curproc->pending_sig ? -EINTR : 0;
}

static long sys_execve(struct trapframe *tf, uint64_t path_uva, uint64_t argv_uva, uint64_t envp_uva)
{
    char path[PATH_MAX];
    char **argv = NULL, **envp = NULL;
    struct exec_image img;
    const char *base;
    long r;
    int i;

    r = copy_str_from_user(path, path_uva, sizeof(path));
    if (r < 0)
        return r;
    r = copy_strv(argv_uva, &argv);
    if (r < 0)
        return r;
    r = copy_strv(envp_uva, &envp);
    if (r < 0) {
        free_strv(argv);
        return r;
    }

    r = exec_prepare(path, curproc->cwd, argv, envp, &img);
    free_strv(argv);
    free_strv(envp);
    if (r < 0)
        return r;

    exec_commit(curproc, &img, tf);
    base = path;
    for (i = 0; path[i]; i++)
        if (path[i] == '/')
            base = path + i + 1;
    memset(curproc->name, 0, sizeof(curproc->name));
    for (i = 0; i < 15 && base[i]; i++)
        curproc->name[i] = base[i];
    return 0;
}

static long user_path(uint64_t uva, char *buf)
{
    long r = copy_str_from_user(buf, uva, PATH_MAX);

    return r < 0 ? r : 0;
}

static long sys_mkdir(uint64_t path_uva, uint64_t mode)
{
    char path[PATH_MAX];
    long r = user_path(path_uva, path);

    return r < 0 ? r : vfs_mkdir(path, curproc->cwd, (uint32_t)mode & 0777);
}

static long sys_rmdir(uint64_t path_uva)
{
    char path[PATH_MAX];
    long r = user_path(path_uva, path);

    return r < 0 ? r : vfs_rmdir(path, curproc->cwd);
}

static long sys_unlink(uint64_t path_uva)
{
    char path[PATH_MAX];
    long r = user_path(path_uva, path);

    return r < 0 ? r : vfs_unlink(path, curproc->cwd);
}

static long sys_rename(uint64_t from_uva, uint64_t to_uva)
{
    char from[PATH_MAX], to[PATH_MAX];
    long r = user_path(from_uva, from);

    if (r < 0)
        return r;
    r = user_path(to_uva, to);
    return r < 0 ? r : vfs_rename(from, to, curproc->cwd);
}

static long sys_truncate(uint64_t path_uva, int64_t len)
{
    char path[PATH_MAX];
    struct vnode *vn;
    long r = user_path(path_uva, path);

    if (r < 0)
        return r;
    if (len < 0)
        return -EINVAL;
    r = vfs_lookup(path, curproc->cwd, &vn);
    return r < 0 ? r : vfs_truncate(vn, (uint64_t)len);
}

static long sys_ftruncate(uint64_t fd, int64_t len)
{
    struct file *f = fd_get((int)fd);

    if (!f)
        return -EBADF;
    if ((f->flags & O_ACCMODE) == O_RDONLY || len < 0)
        return -EINVAL;
    return vfs_truncate(f->vn, (uint64_t)len);
}

static long sys_access(uint64_t path_uva)
{
    char path[PATH_MAX];
    struct vnode *vn;
    long r = user_path(path_uva, path);

    return r < 0 ? r : vfs_lookup(path, curproc->cwd, &vn);
}

struct kstat {
    uint64_t st_dev, st_ino, st_nlink;
    uint32_t st_mode, st_uid, st_gid, pad0;
    uint64_t st_rdev;
    int64_t  st_size, st_blksize, st_blocks;
    uint64_t atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    int64_t  unused[3];
};

static long put_stat(struct vnode *vn, uint64_t st_uva)
{
    struct kstat st;
    uint32_t mode = vn->mode;

    memset(&st, 0, sizeof(st));
    if (!mode)
        mode = vn->type == VDIR ? 040755 : vn->type == VCHR ? 020620 : vn->type == VBLK ? 060660 :
               vn->type == VPIPE ? 010600 : 0100644;
    st.st_ino = vn->ino;
    st.st_nlink = vn->nlink ? vn->nlink : 1;
    st.st_mode = mode;
    st.st_size = (int64_t)vn->size;
    st.st_blksize = 4096;
    st.st_blocks = (int64_t)((vn->size + 511) / 512);
    st.atime = st.mtime = st.ctime = vn->mtime;
    return copy_to_user(st_uva, &st, sizeof(st)) < 0 ? -EFAULT : 0;
}

static long sys_stat(uint64_t path_uva, uint64_t st_uva)
{
    char path[PATH_MAX];
    struct vnode *vn;
    long r = user_path(path_uva, path);

    if (r < 0)
        return r;
    r = vfs_lookup(path, curproc->cwd, &vn);
    return r < 0 ? r : put_stat(vn, st_uva);
}

static long sys_fstat(uint64_t fd, uint64_t st_uva)
{
    struct file *f = fd_get((int)fd);

    return f ? put_stat(f->vn, st_uva) : -EBADF;
}

struct kstatfs {
    int64_t  f_type;
    uint64_t f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t  f_fsid[2];
    int64_t  f_namelen, f_frsize, f_flags, f_spare[4];
};

static long sys_statfs(uint64_t path_uva, uint64_t buf_uva)
{
    char path[PATH_MAX];
    struct vnode *vn;
    struct vstatfs v;
    struct kstatfs k;
    long r = user_path(path_uva, path);

    if (r < 0)
        return r;
    r = vfs_lookup(path, curproc->cwd, &vn);
    if (r < 0)
        return r;
    r = vfs_statfs(vn, &v);
    if (r < 0)
        return r;
    memset(&k, 0, sizeof(k));
    k.f_type = 0x4C454D4F;
    k.f_bsize = k.f_frsize = v.bsize;
    k.f_blocks = v.blocks;
    k.f_bfree = k.f_bavail = v.bfree;
    k.f_files = v.files;
    k.f_ffree = v.ffree;
    k.f_namelen = v.namelen;
    return copy_to_user(buf_uva, &k, sizeof(k)) < 0 ? -EFAULT : 0;
}

static long sys_uname(uint64_t uva)
{
    char u[6][65];
    static const char *const f[6] = { "Nirmala", "nirmala", NIRMALA_VERSION, "NirmalaSYS " NIRMALA_CODENAME, "x86_64", "" };
    int i, j;

    memset(u, 0, sizeof(u));
    for (i = 0; i < 6; i++)
        for (j = 0; f[i][j]; j++)
            u[i][j] = f[i][j];
    return copy_to_user(uva, u, sizeof(u)) < 0 ? -EFAULT : 0;
}

static long sys_kill(int64_t pid, int64_t sig)
{
    if (pid <= 1 || sig < 0 || sig > 31)
        return -EPERM;
    if (sig == 0)
        return proc_signal((int)pid, 0) < 0 ? -ESRCH : 0;
    return proc_signal((int)pid, (int)sig) < 0 ? -ESRCH : 0;
}

struct ktimespec {
    int64_t tv_sec, tv_nsec;
};

static long sys_clock_gettime(uint64_t clk, uint64_t ts_uva)
{
    struct ktimespec ts;
    uint64_t sec;
    uint32_t nsec;

    if (clk == 0 || clk == 5 || clk == 8) {
        wall_time(&sec, &nsec);
    } else if (clk == 1 || clk == 4 || clk == 6 || clk == 7) {
        sec = pit_ticks() / pit_hz();
        nsec = (uint32_t)((pit_ticks() % pit_hz()) * (1000000000UL / pit_hz()));
    } else {
        return -EINVAL;
    }
    ts.tv_sec = (int64_t)sec;
    ts.tv_nsec = nsec;
    return copy_to_user(ts_uva, &ts, sizeof(ts)) < 0 ? -EFAULT : 0;
}

static long sys_time(uint64_t t_uva)
{
    int64_t t = (int64_t)wall_seconds();

    if (t_uva && copy_to_user(t_uva, &t, sizeof(t)) < 0)
        return -EFAULT;
    return t;
}

static long sys_gettimeofday(uint64_t tv_uva)
{
    int64_t tv[2];
    uint64_t sec;
    uint32_t nsec;

    wall_time(&sec, &nsec);
    tv[0] = (int64_t)sec;
    tv[1] = nsec / 1000;
    return tv_uva && copy_to_user(tv_uva, tv, sizeof(tv)) < 0 ? -EFAULT : 0;
}

static long sys_utime(uint64_t path_uva, uint64_t times_uva)
{
    char path[PATH_MAX];
    struct vnode *vn;
    int64_t t[2];
    long r = user_path(path_uva, path);

    if (r < 0)
        return r;
    r = vfs_lookup(path, curproc->cwd, &vn);
    if (r < 0)
        return r;
    if (times_uva) {
        if (copy_from_user(t, times_uva, sizeof(t)) < 0)
            return -EFAULT;
    } else {
        t[1] = (int64_t)wall_seconds();
    }
    return vfs_utime(vn, (uint32_t)t[1]);
}

long sys_socket(uint64_t, uint64_t, uint64_t);
long sys_connect(uint64_t, uint64_t, uint64_t);
long sys_bind(uint64_t, uint64_t, uint64_t);
long sys_sendto(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
long sys_recvfrom(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
long sys_netinfo(uint64_t);

void syscall_dispatch(struct trapframe *tf)
{
    long r;

    switch (tf->rax) {
    case SYS_read:        r = sys_read(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_write:       r = sys_write(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_open:        r = sys_open(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_creat:       r = sys_open(tf->rdi, O_WRONLY | O_CREAT | O_TRUNC, tf->rsi); break;
    case SYS_brk:         r = proc_brk(curproc, tf->rdi); break;
    case SYS_clock_gettime: r = sys_clock_gettime(tf->rdi, tf->rsi); break;
    case SYS_time:        r = sys_time(tf->rdi); break;
    case SYS_gettimeofday: r = sys_gettimeofday(tf->rdi); break;
    case SYS_utime:       r = sys_utime(tf->rdi, tf->rsi); break;
    case SYS_stat:
    case SYS_lstat:       r = sys_stat(tf->rdi, tf->rsi); break;
    case SYS_fstat:       r = sys_fstat(tf->rdi, tf->rsi); break;
    case SYS_access:      r = sys_access(tf->rdi); break;
    case SYS_kill:        r = sys_kill((int64_t)tf->rdi, (int64_t)tf->rsi); break;
    case SYS_uname:       r = sys_uname(tf->rdi); break;
    case SYS_truncate:    r = sys_truncate(tf->rdi, (int64_t)tf->rsi); break;
    case SYS_ftruncate:   r = sys_ftruncate(tf->rdi, (int64_t)tf->rsi); break;
    case SYS_rename:      r = sys_rename(tf->rdi, tf->rsi); break;
    case SYS_mkdir:       r = sys_mkdir(tf->rdi, tf->rsi); break;
    case SYS_rmdir:       r = sys_rmdir(tf->rdi); break;
    case SYS_unlink:      r = sys_unlink(tf->rdi); break;
    case SYS_statfs:      r = sys_statfs(tf->rdi, tf->rsi); break;
    case SYS_chmod:
    case SYS_fchmod:
    case SYS_fsync:
    case SYS_sync:        r = blk_sync_all(); break;
    case SYS_umask:       r = 022; break;
    case SYS_close:       r = sys_close(tf->rdi); break;
    case SYS_ioctl:       r = sys_ioctl(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_lseek:       r = sys_lseek(tf->rdi, (int64_t)tf->rsi, tf->rdx); break;
    case SYS_getpid:      r = curproc->pid; break;
    case SYS_sysinfo:     r = sys_sysinfo(tf->rdi); break;
    case SYS_getppid:     r = curproc->ppid; break;
    case SYS_fork:        r = proc_fork(tf); break;
    case SYS_pipe:        r = sys_pipe(tf->rdi); break;
    case SYS_dup:         r = sys_dup(tf->rdi); break;
    case SYS_dup2:        r = sys_dup2(tf->rdi, tf->rsi); break;
    case SYS_wait4:       r = sys_wait4((int64_t)tf->rdi, tf->rsi, (int)tf->rdx); break;
    case SYS_chdir:       r = sys_chdir(tf->rdi); break;
    case SYS_getcwd:      r = sys_getcwd(tf->rdi, tf->rsi); break;
    case SYS_getdents64:  r = sys_getdents64(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_sched_yield: proc_yield(); r = 0; break;
    case SYS_nanosleep:   r = sys_nanosleep(tf->rdi); break;
    case SYS_socket:      r = sys_socket(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_connect:     r = sys_connect(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_bind:        r = sys_bind(tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_sendto:      r = sys_sendto(tf->rdi, tf->rsi, tf->rdx, tf->r10, tf->r8, tf->r9); break;
    case SYS_recvfrom:    r = sys_recvfrom(tf->rdi, tf->rsi, tf->rdx, tf->r10, tf->r8, tf->r9); break;
    case SYS_netinfo:     r = sys_netinfo(tf->rdi); break;
    case SYS_execve:      r = sys_execve(tf, tf->rdi, tf->rsi, tf->rdx); break;
    case SYS_exit:        proc_exit((int)tf->rdi);
    default:              r = -ENOSYS; break;
    }
    tf->rax = (uint64_t)r;
    if (curproc->pending_sig)
        proc_kill_self(curproc->pending_sig);
}
