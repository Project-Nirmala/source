#include <fbcon.h>
#include <vfs.h>
#include <uaccess.h>
#include <sys/errno.h>
#include <sys/fbtext.h>

#define READ_CHUNK 256

static long fbtext_read_text(uint64_t arg)
{
    struct nir_fbread rd;
    uint8_t tmp[READ_CHUNK];
    uint64_t dst;
    long done = 0;

    if (copy_from_user(&rd, arg, sizeof(rd)) < 0)
        return -EFAULT;
    if (rd.start < 0 || rd.len < 0)
        return -EINVAL;
    dst = (uint64_t)(uintptr_t)rd.buf;
    while (done < rd.len) {
        size_t want = (size_t)(rd.len - done) < sizeof(tmp) ? (size_t)(rd.len - done) : sizeof(tmp);
        size_t got = fbcon_text_copy((unsigned)(rd.start + done), tmp, want);

        if (!got)
            break;
        if (copy_to_user(dst + (uint64_t)done, tmp, got) < 0)
            return -EFAULT;
        done += (long)got;
    }
    return done;
}

static long fbtext_ioctl(struct vnode *vn, uint64_t req, uint64_t arg)
{
    struct nir_fbinfo info;
    struct nir_fbptr ptr;
    struct nir_fbsel sel;

    (void)vn;
    switch (req) {
    case NIR_FBINFO:
        info.cols = (unsigned)fbcon_cols();
        info.rows = (unsigned)fbcon_rows();
        info.selection = fbcon_selection_active();
        fbcon_pixel_info(&info.width, &info.height, &info.cellw, &info.cellh);
        return copy_to_user(arg, &info, sizeof(info));
    case NIR_FBPOINTER:
        if (copy_from_user(&ptr, arg, sizeof(ptr)) < 0)
            return -EFAULT;
        fbcon_set_pointer(ptr.col, ptr.row, ptr.visible != 0);
        return 0;
    case NIR_FBSELECT:
        if (copy_from_user(&sel, arg, sizeof(sel)) < 0)
            return -EFAULT;
        fbcon_set_selection(sel.start, sel.end);
        return 0;
    case NIR_FBREAD:
        return fbtext_read_text(arg);
    default:
        return -ENOTTY;
    }
}

static const struct vnops fbtext_ops = {
    .ioctl = fbtext_ioctl,
};

struct vnode fbtext_vnode = {
    .type = VCHR,
    .ops = &fbtext_ops,
    .mode = 020600,
    .name = "fbcon",
};
