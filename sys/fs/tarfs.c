#include <vfs.h>
#include <kern.h>
#include <sys/errno.h>

struct tar_hdr {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __packed;

static uint64_t parse_octal(const char *s, size_t n)
{
    uint64_t v = 0;
    size_t i = 0;

    while (i < n && s[i] == ' ')
        i++;
    for (; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (uint64_t)(s[i] - '0');
    return v;
}

static size_t bounded_len(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && s[n])
        n++;
    return n;
}

static void mkdir_p(const char *path, size_t len, bool all)
{
    char tmp[320];
    size_t i;

    if (len >= sizeof(tmp))
        return;
    memcpy(tmp, path, len);
    tmp[len] = 0;
    for (i = 1; i <= len; i++) {
        if (tmp[i] == '/' || (tmp[i] == 0 && all)) {
            char c = tmp[i];

            tmp[i] = 0;
            if (tmp[i - 1] != '/')
                vfs_mkdir(tmp, vfs_root(), 0755);
            tmp[i] = c;
        }
    }
}

static bool is_system_path(const char *path)
{
    return !memcmp(path, "/bin/", 5) || !memcmp(path, "/sbin/", 6) || !memcmp(path, "/usr/", 5);
}

static bool same_content(struct vnode *v, const uint8_t *data, uint64_t size)
{
    static uint8_t buf[4096];
    uint64_t off = 0;

    if (v->size != size)
        return false;
    while (off < size) {
        size_t n = size - off > sizeof(buf) ? sizeof(buf) : (size_t)(size - off);

        if (vnode_read(v, off, buf, n) != (long)n || memcmp(buf, data + off, n))
            return false;
        off += n;
    }
    return true;
}

static void extract_file(const char *path, uint32_t mode, const uint8_t *data, uint64_t size)
{
    struct vnode *v;
    uint64_t off = 0;
    int r = vfs_lookup(path, vfs_root(), &v);

    if (r == 0) {
        if (!is_system_path(path) || same_content(v, data, size))
            return;
        r = vfs_truncate(v, 0);
    } else {
        r = vfs_create(path, vfs_root(), mode, 0, &v);
    }

    if (r < 0) {
        kprintf("initrd: %s: error %d\n", path, -r);
        return;
    }
    while (off < size) {
        size_t n = size - off > 4096 ? 4096 : (size_t)(size - off);
        long w = vnode_write(v, off, data + off, n);

        if (w <= 0) {
            kprintf("initrd: %s: write error %ld\n", path, -w);
            return;
        }
        off += (uint64_t)w;
    }
}

void tarfs_extract(const void *image, size_t size)
{
    const uint8_t *base = image;
    size_t off = 0;

    while (off + 512 <= size) {
        const struct tar_hdr *h = (const struct tar_hdr *)(base + off);
        char path[320];
        size_t plen = 1, nlen, data_off;
        uint64_t sz;

        if (h->name[0] == 0)
            break;
        if (memcmp(h->magic, "ustar", 5)) {
            kprintf("initrd: bad tar header at offset %lu\n", (uint64_t)off);
            break;
        }
        sz = parse_octal(h->size, sizeof(h->size));
        data_off = off + 512;
        if (data_off + sz > size) {
            kprintf("initrd: truncated archive\n");
            break;
        }
        path[0] = '/';
        if (h->prefix[0]) {
            size_t pl = bounded_len(h->prefix, sizeof(h->prefix));

            memcpy(path + plen, h->prefix, pl);
            plen += pl;
            path[plen++] = '/';
        }
        nlen = bounded_len(h->name, sizeof(h->name));
        if (plen + nlen < sizeof(path)) {
            memcpy(path + plen, h->name, nlen);
            plen += nlen;
            path[plen] = 0;
            while (plen > 1 && path[plen - 1] == '/')
                path[--plen] = 0;
            if (h->typeflag == '5') {
                mkdir_p(path, plen, true);
            } else if (h->typeflag == '0' || h->typeflag == 0) {
                mkdir_p(path, plen, false);
                extract_file(path, (uint32_t)parse_octal(h->mode, sizeof(h->mode)) & 0777,
                             base + data_off, sz);
            }
        }
        off = data_off + ((sz + 511) & ~511UL);
    }
}
