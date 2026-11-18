#include <file.h>
#include <mm.h>
#include <kern.h>

struct file *file_new(struct vnode *vn, int flags)
{
    struct file *f = kzalloc(sizeof(*f));

    if (!f)
        return NULL;
    f->vn = vn;
    f->flags = flags;
    f->refs = 1;
    vn->refs++;
    return f;
}

struct file *file_ref(struct file *f)
{
    f->refs++;
    return f;
}

void file_unref(struct file *f)
{
    if (--f->refs == 0) {
        f->vn->refs--;
        if (f->vn->ops && f->vn->ops->release)
            f->vn->ops->release(f->vn);
        kfree(f);
    }
}
