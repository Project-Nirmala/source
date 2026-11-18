#ifndef _NIRMALA_FILE_H
#define _NIRMALA_FILE_H

#include <types.h>
#include <vfs.h>

#define NOFILE 16

struct file {
    struct vnode *vn;
    uint64_t      off;
    int           flags;
    int           refs;
};

struct file *file_new(struct vnode *vn, int flags);
struct file *file_ref(struct file *f);
void         file_unref(struct file *f);

#endif
