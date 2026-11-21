/* Host test: owner/group/other permission checks in the VFS (see vfs_perm). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <vfs.h>
#include <blk.h>
#include <proc.h>
#include <sys/errno.h>

void *kmalloc(size_t n) { return malloc(n); }
void *kzalloc(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
void kprintf(const char *f, ...) { (void)f; }
void panic(const char *f, ...) { va_list ap; va_start(ap, f); printf("PANIC: "); vprintf(f, ap); va_end(ap); abort(); }
uint64_t pit_ticks(void) { return 1000; }
uint64_t wall_seconds(void) { return 1700000000; }
uint32_t pit_hz(void) { return 100; }
struct vnode console_vnode;
struct blkdrv *virtio_blk_probe(void) { return NULL; }
struct cpu cpu0;

static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

static struct proc me;

static void become(uint32_t uid, uint32_t gid)
{
    me.uid = uid;
    me.gid = gid;
    cpu0.cur = &me;
}

static struct vnode *R;

static int lookup(const char *path, struct vnode **v) { return vfs_lookup(path, R, v); }

int main(void)
{
    struct vnode *v, *home, *tmp;
    void *tar;
    FILE *f;
    size_t n;

    if (system("rm -rf /tmp/perm_root && mkdir -p /tmp/perm_root/etc /tmp/perm_root/tmp && echo x > /tmp/perm_root/etc/passwd && "
               "chmod 600 /tmp/perm_root/etc/passwd && tar --format=ustar --owner=0 --group=0 -cf /tmp/perm.tar -C /tmp/perm_root .") != 0)
        return 2;
    f = fopen("/tmp/perm.tar", "rb");
    fseek(f, 0, SEEK_END); n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    tar = malloc(n);
    if (fread(tar, 1, n, f) != n) return 2;
    fclose(f);

    cpu0.cur = NULL;               /* boot context behaves as root */
    vfs_init(tar, n);
    R = vfs_root();

    /* root builds a small tree: /home/alice (1000, 0700), /home/bob (1001, 0755), /tmp sticky */
    become(0, 0);
    T(vfs_mkdir("/home", R, 0755) == 0, "root mkdir /home");
    T(vfs_mkdir("/home/alice", R, 0700) == 0 && lookup("/home/alice", &v) == 0 && vfs_chattr(v, -1, 1000, 1000) == 0, "alice home");
    T(vfs_mkdir("/home/bob", R, 0755) == 0 && lookup("/home/bob", &v) == 0 && vfs_chattr(v, -1, 1001, 1001) == 0, "bob home");
    T(lookup("/tmp", &tmp) == 0 && vfs_chattr(tmp, 01777, -1, -1) == 0 && (tmp->mode & 07777) == 01777, "sticky /tmp");
    T(lookup("/etc/passwd", &v) == 0 && (v->mode & 0777) == 0600 && v->uid == 0, "passwd is 0600 root");

    /* alice */
    become(1000, 1000);
    T(lookup("/etc/passwd", &v) == 0 && vfs_perm(v, VP_R) == -EACCES, "alice cannot read passwd");
    T(vfs_perm(v, VP_W) == -EACCES, "alice cannot write passwd");
    T(vfs_create("/rootfile", R, 0644, 0, &v) == -EACCES, "alice cannot create in /");
    T(vfs_mkdir("/etc2", R, 0755) == -EACCES, "alice cannot mkdir in /");
    T(vfs_unlink("/etc/passwd", R) == -EACCES, "alice cannot unlink passwd");
    T(vfs_create("/home/alice/notes", R, 0644, 0, &v) == 0 && v->uid == 1000 && v->gid == 1000, "alice creates in her home, owned by her");
    T(vfs_create("/tmp/a", R, 0644, 0, &v) == 0 && v->uid == 1000, "alice creates in /tmp");
    T(lookup("/home/bob", &home) == 0 && vfs_perm(home, VP_R | VP_X) == 0, "alice may enter bob's 0755 home");
    T(vfs_create("/home/bob/x", R, 0644, 0, &v) == -EACCES, "alice cannot write in bob's home");
    T(lookup("/home/alice/notes", &v) == 0 && vfs_chattr(v, 0600, -1, -1) == 0, "owner may chmod");
    T(vfs_chattr(v, -1, 0, -1) == -EPERM, "alice cannot give her file to root");
    T(vfs_chattr(v, -1, 1001, -1) == -EPERM, "alice cannot give her file to bob");
    T(lookup("/etc/passwd", &v) == 0 && vfs_chattr(v, 0666, -1, -1) == -EPERM, "alice cannot chmod passwd");

    /* bob */
    become(1001, 1001);
    T(lookup("/home/alice/notes", &v) == -EACCES, "bob cannot enter alice's 0700 home");
    T(vfs_create("/tmp/b", R, 0644, 0, &v) == 0, "bob creates in /tmp");
    T(vfs_unlink("/tmp/a", R) == -EPERM, "sticky: bob cannot delete alice's file");
    T(vfs_rename("/tmp/a", "/tmp/stolen", R) == -EPERM, "sticky: bob cannot rename alice's file");
    T(vfs_unlink("/tmp/b", R) == 0, "bob deletes his own file");

    /* alice again, then root */
    become(1000, 1000);
    T(vfs_unlink("/tmp/a", R) == 0, "alice deletes her own file in /tmp");
    become(0, 0);
    T(lookup("/home/alice/notes", &v) == 0 && vfs_perm(v, VP_R | VP_W) == 0, "root reads anything");
    T(vfs_chattr(v, -1, 1001, -1) == 0 && v->uid == 1001, "root may chown");
    T(vfs_unlink("/home/alice/notes", R) == 0, "root removes it");

    printf(fails ? "perm_test: %d failure(s)\n" : "perm_test: ok\n", fails);
    return fails ? 1 : 0;
}
