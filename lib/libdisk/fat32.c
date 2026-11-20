#include <diskfmt.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define SECTOR 512u
#define EOC 0x0FFFFFF8u
#define MASK 0x0FFFFFFFu
#define ATTR_DIR 0x10
#define ATTR_LFN 0x0F
#define DOS_DATE 0x5C21
#define WBUF_BYTES 65536u
#define MAX_PATH 512

struct fat32 {
    int       fd;
    uint64_t  first, nsect;
    uint32_t  spc, reserved, nfats, fatsz, root, nclusters, hint;
    uint64_t  data_start;
    uint32_t *fat;
    int       writable, dirty;
    uint8_t  *wbuf;
    uint32_t  wbuf_cl, wbuf_n;
};

struct dirbuf {
    uint8_t  *data;
    uint32_t  size;
    uint32_t  cluster;
};

static uint16_t g16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t g32(const uint8_t *p)
{
    return (uint32_t)g16(p) | (uint32_t)g16(p + 2) << 16;
}

static void p16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void p32(uint8_t *p, uint32_t v)
{
    p16(p, (uint16_t)v);
    p16(p + 2, (uint16_t)(v >> 16));
}

static int raw_io(int fd, uint64_t byte_off, void *buf, size_t len, int write_op)
{
    uint8_t *p = buf;

    if (lseek(fd, (off_t)byte_off, SEEK_SET) < 0)
        return -1;
    while (len) {
        long r = write_op ? write(fd, p, len) : read(fd, p, len);

        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static int sec_io(struct fat32 *fs, uint64_t sector, void *buf, size_t len, int write_op)
{
    return raw_io(fs->fd, (fs->first + sector) * SECTOR, buf, len, write_op);
}

static uint32_t cluster_bytes(const struct fat32 *fs)
{
    return fs->spc * SECTOR;
}

static uint64_t cluster_sector(const struct fat32 *fs, uint32_t cl)
{
    return fs->data_start + (uint64_t)(cl - 2) * fs->spc;
}

static int cl_read(struct fat32 *fs, uint32_t cl, void *buf)
{
    return sec_io(fs, cluster_sector(fs, cl), buf, cluster_bytes(fs), 0);
}

static int cl_write(struct fat32 *fs, uint32_t cl, const void *buf)
{
    return sec_io(fs, cluster_sector(fs, cl), (void *)buf, cluster_bytes(fs), 1);
}

static int wbuf_flush(struct fat32 *fs)
{
    int r = 0;

    if (fs->wbuf_n) {
        r = sec_io(fs, cluster_sector(fs, fs->wbuf_cl), fs->wbuf, (size_t)fs->wbuf_n * cluster_bytes(fs), 1);
        fs->wbuf_n = 0;
    }
    return r;
}

static int wbuf_put(struct fat32 *fs, uint32_t cl, const uint8_t *data)
{
    uint32_t cb = cluster_bytes(fs);

    if (fs->wbuf_n && (fs->wbuf_cl + fs->wbuf_n != cl || (fs->wbuf_n + 1) * cb > WBUF_BYTES))
        if (wbuf_flush(fs) < 0)
            return -1;
    if (!fs->wbuf_n)
        fs->wbuf_cl = cl;
    memcpy(fs->wbuf + (size_t)fs->wbuf_n * cb, data, cb);
    fs->wbuf_n++;
    return 0;
}

static uint32_t fat_next(const struct fat32 *fs, uint32_t cl)
{
    return fs->fat[cl] & MASK;
}

static uint32_t fat_alloc(struct fat32 *fs)
{
    uint32_t i, n = fs->nclusters + 2;

    for (i = 0; i < n; i++) {
        uint32_t cl = fs->hint + i;

        if (cl >= n)
            cl = cl - n + 2;
        if (cl < 2)
            continue;
        if ((fs->fat[cl] & MASK) == 0) {
            fs->fat[cl] = MASK;
            fs->hint = cl + 1;
            fs->dirty = 1;
            return cl;
        }
    }
    return 0;
}

static void fat_free_chain(struct fat32 *fs, uint32_t cl)
{
    while (cl >= 2 && cl < EOC) {
        uint32_t next = fat_next(fs, cl);

        fs->fat[cl] = 0;
        cl = next;
    }
    fs->dirty = 1;
}

static int load_fat(struct fat32 *fs)
{
    size_t bytes = (size_t)fs->fatsz * SECTOR;

    fs->fat = malloc(bytes);
    if (!fs->fat)
        return -1;
    return sec_io(fs, fs->reserved, fs->fat, bytes, 0);
}

static int zero_sectors(int fd, uint64_t byte_off, uint64_t bytes)
{
    static uint8_t z[WBUF_BYTES];

    while (bytes) {
        size_t n = bytes > sizeof(z) ? sizeof(z) : (size_t)bytes;

        if (raw_io(fd, byte_off, z, n, 1) < 0)
            return -1;
        byte_off += n;
        bytes -= n;
    }
    return 0;
}

static void fsinfo_fill(uint8_t *s, uint32_t free_count, uint32_t next_free)
{
    memset(s, 0, SECTOR);
    p32(s, 0x41615252);
    p32(s + 484, 0x61417272);
    p32(s + 488, free_count);
    p32(s + 492, next_free);
    p32(s + 508, 0xAA550000);
}

int fat32_format(int fd, uint64_t first, uint64_t nsect, const char *label)
{
    uint8_t boot[SECTOR], info[SECTOR];
    uint32_t spc, reserved = 32, fatsz = 0, clusters = 0, i;
    uint64_t data_start, base = first * SECTOR, vol = 0x4E495230u ^ (uint32_t)nsect;
    uint32_t *fat0;
    size_t len;
    char lab[11];

    for (spc = 128; spc >= 1; spc >>= 1) {
        fatsz = 0;
        for (i = 0; i < 4; i++) {
            if (nsect < reserved + 2ull * fatsz)
                break;
            clusters = (uint32_t)((nsect - reserved - 2ull * fatsz) / spc);
            fatsz = ((clusters + 2) * 4 + SECTOR - 1) / SECTOR;
        }
        if (nsect < reserved + 2ull * fatsz)
            continue;
        clusters = (uint32_t)((nsect - reserved - 2ull * fatsz) / spc);
        if (clusters >= 65525)
            break;
    }
    if (spc == 0 || clusters < 65525 || clusters > 0x0FFFFFF5u)
        return -1;
    data_start = reserved + 2ull * fatsz;

    memset(lab, ' ', sizeof(lab));
    len = label ? strlen(label) : 0;
    for (i = 0; i < len && i < 11; i++)
        lab[i] = label[i] >= 'a' && label[i] <= 'z' ? (char)(label[i] - 32) : label[i];

    memset(boot, 0, sizeof(boot));
    boot[0] = 0xEB;
    boot[1] = 0x58;
    boot[2] = 0x90;
    memcpy(boot + 3, "NIRMALA ", 8);
    p16(boot + 11, SECTOR);
    boot[13] = (uint8_t)spc;
    p16(boot + 14, (uint16_t)reserved);
    boot[16] = 2;
    boot[21] = 0xF8;
    p16(boot + 24, 63);
    p16(boot + 26, 255);
    p32(boot + 28, (uint32_t)first);
    p32(boot + 32, (uint32_t)nsect);
    p32(boot + 36, fatsz);
    p32(boot + 44, 2);
    p16(boot + 48, 1);
    p16(boot + 50, 6);
    boot[64] = 0x80;
    boot[66] = 0x29;
    p32(boot + 67, (uint32_t)vol);
    memcpy(boot + 71, lab, 11);
    memcpy(boot + 82, "FAT32   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;
    fsinfo_fill(info, clusters - 1, 3);

    if (raw_io(fd, base, boot, SECTOR, 1) < 0 || raw_io(fd, base + SECTOR, info, SECTOR, 1) < 0)
        return -1;
    if (raw_io(fd, base + 6 * SECTOR, boot, SECTOR, 1) < 0 || raw_io(fd, base + 7 * SECTOR, info, SECTOR, 1) < 0)
        return -1;
    if (zero_sectors(fd, base + (uint64_t)reserved * SECTOR, 2ull * fatsz * SECTOR) < 0)
        return -1;
    fat0 = calloc(1, SECTOR);
    if (!fat0)
        return -1;
    fat0[0] = 0x0FFFFFF8;
    fat0[1] = 0x0FFFFFFF;
    fat0[2] = 0x0FFFFFFF;
    i = (uint32_t)(raw_io(fd, base + (uint64_t)reserved * SECTOR, fat0, SECTOR, 1) < 0 ||
                   raw_io(fd, base + ((uint64_t)reserved + fatsz) * SECTOR, fat0, SECTOR, 1) < 0);
    free(fat0);
    if (i)
        return -1;
    {
        uint8_t *root = calloc(1, (size_t)spc * SECTOR);

        if (!root)
            return -1;
        memcpy(root, lab, 11);
        root[11] = 0x08;
        p16(root + 24, DOS_DATE);
        i = (uint32_t)(raw_io(fd, base + data_start * SECTOR, root, (size_t)spc * SECTOR, 1) < 0);
        free(root);
        return i ? -1 : 0;
    }
}

struct fat32 *fat32_open(int fd, uint64_t first, uint64_t nsect, int writable)
{
    uint8_t boot[SECTOR];
    struct fat32 *fs;
    uint32_t total;

    if (raw_io(fd, first * SECTOR, boot, SECTOR, 0) < 0)
        return NULL;
    if (boot[510] != 0x55 || boot[511] != 0xAA || g16(boot + 11) != SECTOR || boot[16] == 0 ||
        g16(boot + 22) != 0 || g32(boot + 36) == 0 || boot[13] == 0)
        return NULL;
    fs = calloc(1, sizeof(*fs));
    if (!fs)
        return NULL;
    fs->fd = fd;
    fs->first = first;
    fs->nsect = nsect;
    fs->spc = boot[13];
    fs->reserved = g16(boot + 14);
    fs->nfats = boot[16];
    fs->fatsz = g32(boot + 36);
    fs->root = g32(boot + 44);
    total = g32(boot + 32);
    fs->data_start = fs->reserved + (uint64_t)fs->nfats * fs->fatsz;
    if (total > nsect || total <= fs->data_start || fs->root < 2) {
        free(fs);
        return NULL;
    }
    fs->nclusters = (uint32_t)((total - fs->data_start) / fs->spc);
    fs->writable = writable;
    fs->hint = 3;
    fs->wbuf = malloc(WBUF_BYTES > cluster_bytes(fs) ? WBUF_BYTES : cluster_bytes(fs));
    if (!fs->wbuf || load_fat(fs) < 0) {
        free(fs->wbuf);
        free(fs->fat);
        free(fs);
        return NULL;
    }
    return fs;
}

int fat32_close(struct fat32 *fs)
{
    int r = 0;

    if (fs->writable) {
        uint8_t info[SECTOR];
        uint32_t i, freec = 0;

        r = wbuf_flush(fs);
        if (fs->dirty && r == 0) {
            size_t bytes = (size_t)fs->fatsz * SECTOR;

            for (i = 2; i < fs->nclusters + 2; i++)
                if ((fs->fat[i] & MASK) == 0)
                    freec++;
            for (i = 0; i < fs->nfats && r == 0; i++)
                r = sec_io(fs, fs->reserved + (uint64_t)i * fs->fatsz, fs->fat, bytes, 1);
            fsinfo_fill(info, freec, fs->hint);
            if (r == 0)
                r = sec_io(fs, 1, info, SECTOR, 1);
            if (r == 0)
                r = sec_io(fs, 7, info, SECTOR, 1);
        }
    }
    free(fs->wbuf);
    free(fs->fat);
    free(fs);
    return r;
}

static int dir_load(struct fat32 *fs, uint32_t cl, struct dirbuf *d)
{
    uint32_t n = 0, c, cb = cluster_bytes(fs);

    for (c = cl; c >= 2 && c < EOC; c = fat_next(fs, c))
        n++;
    d->data = malloc((size_t)n * cb);
    d->size = n * cb;
    d->cluster = cl;
    if (!d->data)
        return -1;
    for (c = cl, n = 0; c >= 2 && c < EOC; c = fat_next(fs, c), n++)
        if (cl_read(fs, c, d->data + (size_t)n * cb) < 0) {
            free(d->data);
            return -1;
        }
    return 0;
}

static int dir_store(struct fat32 *fs, struct dirbuf *d)
{
    uint32_t n = 0, c, cb = cluster_bytes(fs);

    for (c = d->cluster; c >= 2 && c < EOC; c = fat_next(fs, c), n++)
        if (cl_write(fs, c, d->data + (size_t)n * cb) < 0)
            return -1;
    return 0;
}

static int dir_extend(struct fat32 *fs, struct dirbuf *d)
{
    uint32_t cl = fat_alloc(fs), last, cb = cluster_bytes(fs);
    uint8_t *nd;

    if (!cl)
        return -1;
    for (last = d->cluster; fat_next(fs, last) < EOC; last = fat_next(fs, last))
        ;
    fs->fat[last] = cl;
    nd = realloc(d->data, d->size + cb);
    if (!nd)
        return -1;
    memset(nd + d->size, 0, cb);
    d->data = nd;
    d->size += cb;
    return 0;
}

static char upc(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (upc(*a) != upc(*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static uint8_t sfn_sum(const uint8_t *n)
{
    uint8_t s = 0;
    int i;

    for (i = 0; i < 11; i++)
        s = (uint8_t)(((s & 1) << 7) + (s >> 1) + n[i]);
    return s;
}

static void short_to_str(const uint8_t *e, char *out)
{
    int i, o = 0;

    for (i = 0; i < 8 && e[i] != ' '; i++)
        out[o++] = (e[12] & 0x08) && e[i] >= 'A' && e[i] <= 'Z' ? (char)(e[i] + 32) : (char)e[i];
    if (e[8] != ' ') {
        out[o++] = '.';
        for (i = 8; i < 11 && e[i] != ' '; i++)
            out[o++] = (e[12] & 0x10) && e[i] >= 'A' && e[i] <= 'Z' ? (char)(e[i] + 32) : (char)e[i];
    }
    out[o] = 0;
}

typedef int (*dir_cb)(const uint8_t *ent, const char *name, uint32_t off, uint32_t nlfn, void *ctx);

static int dir_walk(struct dirbuf *d, dir_cb cb, void *ctx)
{
    char lfn[256];
    uint32_t off, lfn_start = 0, nlfn = 0;
    uint8_t csum = 0;
    int have = 0;

    for (off = 0; off + 32 <= d->size; off += 32) {
        const uint8_t *e = d->data + off;

        if (e[0] == 0)
            break;
        if (e[0] == 0xE5) {
            have = 0;
            continue;
        }
        if (e[11] == ATTR_LFN) {
            uint32_t seq = e[0] & 0x1F, k;
            static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

            if (e[0] & 0x40) {
                memset(lfn, 0, sizeof(lfn));
                have = 1;
                csum = e[13];
                lfn_start = off;
                nlfn = seq;
            }
            if (have && seq >= 1 && seq <= 20 && e[13] == csum)
                for (k = 0; k < 13; k++) {
                    uint16_t ch = g16(e + pos[k]);

                    if (ch == 0 || ch == 0xFFFF)
                        break;
                    lfn[(seq - 1) * 13 + k] = ch < 128 ? (char)ch : '?';
                }
            continue;
        }
        if (e[11] & 0x08) {
            have = 0;
            continue;
        }
        {
            char name[256];
            int r;

            if (have && sfn_sum(e) == csum)
                memcpy(name, lfn, sizeof(name));
            else {
                short_to_str(e, name);
                nlfn = 0;
                lfn_start = off;
            }
            r = cb(e, name, nlfn ? lfn_start : off, nlfn, ctx);
            have = 0;
            if (r)
                return r;
            nlfn = 0;
        }
    }
    return 0;
}

struct find_ctx {
    const char *want;
    uint32_t    off, entry_off, nlfn;
    uint8_t     ent[32];
    int         found;
};

static int find_cb(const uint8_t *e, const char *name, uint32_t off, uint32_t nlfn, void *ctx)
{
    struct find_ctx *f = ctx;
    char sh[16];

    short_to_str(e, sh);
    if (!ieq(name, f->want) && !ieq(sh, f->want))
        return 0;
    f->found = 1;
    f->off = off;
    f->nlfn = nlfn;
    f->entry_off = off + nlfn * 32;
    memcpy(f->ent, e, 32);
    return 1;
}

static uint32_t ent_cluster(const uint8_t *e)
{
    return (uint32_t)g16(e + 20) << 16 | g16(e + 26);
}

static int valid_sfn_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '$' || c == '~' || c == '!';
}

static int is_plain_sfn(const char *name, uint8_t *out)
{
    size_t len = strlen(name), i, bl = 0, el = 0;
    const char *dot = strrchr(name, '.');

    memset(out, ' ', 11);
    if (len == 0 || len > 12)
        return 0;
    for (i = 0; i < len; i++) {
        if (name + i == dot)
            continue;
        if (!valid_sfn_char(name[i]))
            return 0;
        if (dot && name + i > dot) {
            if (el >= 3)
                return 0;
            out[8 + el++] = (uint8_t)name[i];
        } else {
            if (bl >= 8)
                return 0;
            out[bl++] = (uint8_t)name[i];
        }
    }
    return bl > 0;
}

struct exist_ctx {
    const uint8_t *sfn;
    int            clash;
};

static int exist_cb(const uint8_t *e, const char *name, uint32_t off, uint32_t nlfn, void *ctx)
{
    struct exist_ctx *x = ctx;

    (void)name;
    (void)off;
    (void)nlfn;
    if (!memcmp(e, x->sfn, 11))
        x->clash = 1;
    return x->clash;
}

static void make_alias(struct dirbuf *d, const char *name, uint8_t *out)
{
    const char *dot = strrchr(name, '.');
    size_t i, bl = 0, el = 0, nlen = dot ? (size_t)(dot - name) : strlen(name);
    unsigned num;
    uint8_t base[8], ext[3];
    int digits;

    memset(base, ' ', 8);
    memset(ext, ' ', 3);
    for (i = 0; i < nlen; i++) {
        char c = upc(name[i]);

        if (valid_sfn_char(c) && c != '~' && bl < 8)
            base[bl++] = (uint8_t)c;
    }
    if (dot)
        for (i = 1; dot[i] && el < 3; i++) {
            char c = upc(dot[i]);

            if (valid_sfn_char(c) && c != '~')
                ext[el++] = (uint8_t)c;
        }
    if (bl == 0)
        base[bl++] = '_';
    for (num = 1; num < 1000000; num++) {
        struct exist_ctx x;
        char tail[8];
        size_t keep;

        digits = 0;
        {
            unsigned v = num;

            do {
                tail[digits++] = (char)('0' + v % 10);
                v /= 10;
            } while (v);
        }
        keep = bl > (size_t)(7 - digits) ? (size_t)(7 - digits) : bl;
        memset(out, ' ', 11);
        memcpy(out, base, keep);
        out[keep] = '~';
        for (i = 0; i < (size_t)digits; i++)
            out[keep + 1 + i] = (uint8_t)tail[digits - 1 - i];
        memcpy(out + 8, ext, 3);
        x.sfn = out;
        x.clash = 0;
        dir_walk(d, exist_cb, &x);
        if (!x.clash)
            return;
    }
}

static int add_entry(struct fat32 *fs, struct dirbuf *d, const char *name, uint8_t attr, uint32_t cl, uint32_t size)
{
    uint8_t sfn[11];
    uint32_t nlfn = 0, need, run = 0, start = 0, off, k;
    size_t len = strlen(name);
    int plain = is_plain_sfn(name, sfn);

    if (len > 255)
        return -1;
    if (!plain) {
        make_alias(d, name, sfn);
        nlfn = (uint32_t)((len + 12) / 13);
    }
    need = nlfn + 1;
    for (;;) {
        run = 0;
        for (off = 0; off + 32 <= d->size; off += 32) {
            if (d->data[off] == 0 || d->data[off] == 0xE5) {
                if (run == 0)
                    start = off;
                if (++run == need)
                    break;
            } else {
                run = 0;
            }
        }
        if (run == need)
            break;
        if (dir_extend(fs, d) < 0)
            return -1;
    }
    for (k = 0; k < nlfn; k++) {
        uint8_t *e = d->data + start + k * 32;
        uint32_t seq = nlfn - k, c;
        static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

        memset(e, 0, 32);
        e[0] = (uint8_t)(seq | (k == 0 ? 0x40 : 0));
        e[11] = ATTR_LFN;
        e[13] = sfn_sum(sfn);
        for (c = 0; c < 13; c++) {
            size_t idx = (size_t)(seq - 1) * 13 + c;
            uint16_t ch = idx < len ? (uint8_t)name[idx] : idx == len ? 0 : 0xFFFF;

            p16(e + pos[c], ch);
        }
    }
    {
        uint8_t *e = d->data + start + nlfn * 32;

        memset(e, 0, 32);
        memcpy(e, sfn, 11);
        e[11] = attr;
        p16(e + 14, 0);
        p16(e + 16, DOS_DATE);
        p16(e + 18, DOS_DATE);
        p16(e + 20, (uint16_t)(cl >> 16));
        p16(e + 24, DOS_DATE);
        p16(e + 26, (uint16_t)cl);
        p32(e + 28, size);
        p16(e + 12, 0);
    }
    return 0;
}

static int split_path(const char *path, char comps[][256], int max)
{
    int n = 0;

    while (*path) {
        size_t l = 0;

        while (*path == '/')
            path++;
        if (!*path)
            break;
        while (path[l] && path[l] != '/')
            l++;
        if (l > 255 || n == max)
            return -1;
        memcpy(comps[n], path, l);
        comps[n][l] = 0;
        n++;
        path += l;
    }
    return n;
}

static int walk_dirs(struct fat32 *fs, char comps[][256], int n, int create, uint32_t *out)
{
    uint32_t cur = fs->root;
    int i;

    for (i = 0; i < n; i++) {
        struct dirbuf d;
        struct find_ctx f;

        if (dir_load(fs, cur, &d) < 0)
            return -1;
        memset(&f, 0, sizeof(f));
        f.want = comps[i];
        dir_walk(&d, find_cb, &f);
        if (f.found) {
            if (!(f.ent[11] & ATTR_DIR)) {
                free(d.data);
                return -1;
            }
            cur = ent_cluster(f.ent);
            if (cur == 0)
                cur = fs->root;
            free(d.data);
            continue;
        }
        if (!create || !fs->writable) {
            free(d.data);
            return -1;
        }
        {
            uint32_t ncl = fat_alloc(fs);
            uint8_t *blk;
            int r;

            if (!ncl) {
                free(d.data);
                return -1;
            }
            blk = calloc(1, cluster_bytes(fs));
            if (!blk) {
                free(d.data);
                return -1;
            }
            memcpy(blk, ".          ", 11);
            blk[11] = ATTR_DIR;
            p16(blk + 20, (uint16_t)(ncl >> 16));
            p16(blk + 26, (uint16_t)ncl);
            p16(blk + 16, DOS_DATE);
            p16(blk + 24, DOS_DATE);
            memcpy(blk + 32, "..         ", 11);
            blk[32 + 11] = ATTR_DIR;
            {
                uint32_t pcl = cur == fs->root ? 0 : cur;

                p16(blk + 32 + 20, (uint16_t)(pcl >> 16));
                p16(blk + 32 + 26, (uint16_t)pcl);
            }
            p16(blk + 32 + 16, DOS_DATE);
            p16(blk + 32 + 24, DOS_DATE);
            r = cl_write(fs, ncl, blk);
            free(blk);
            if (r < 0 || add_entry(fs, &d, comps[i], ATTR_DIR, ncl, 0) < 0 || dir_store(fs, &d) < 0) {
                free(d.data);
                return -1;
            }
            free(d.data);
            cur = ncl;
        }
    }
    *out = cur;
    return 0;
}

int fat32_mkdir(struct fat32 *fs, const char *path)
{
    static char comps[32][256];
    uint32_t cl;
    int n = split_path(path, comps, 32);

    if (n < 0)
        return -1;
    return walk_dirs(fs, comps, n, 1, &cl);
}

static void remove_entry(struct fat32 *fs, struct dirbuf *d, uint32_t off, uint32_t nlfn)
{
    uint32_t k;

    (void)fs;
    for (k = 0; k <= nlfn; k++)
        d->data[off + k * 32] = 0xE5;
}

typedef long (*src_fn)(void *ctx, void *buf, size_t n);

static int put_stream(struct fat32 *fs, const char *path, src_fn src, void *sctx)
{
    static char comps[32][256];
    struct dirbuf d;
    struct find_ctx f;
    uint32_t dcl, first = 0, prev = 0, total = 0, cb = cluster_bytes(fs);
    uint8_t *blk;
    int n = split_path(path, comps, 32), r = -1;

    if (n <= 0 || !fs->writable)
        return -1;
    if (walk_dirs(fs, comps, n - 1, 1, &dcl) < 0)
        return -1;
    if (dir_load(fs, dcl, &d) < 0)
        return -1;
    memset(&f, 0, sizeof(f));
    f.want = comps[n - 1];
    dir_walk(&d, find_cb, &f);
    if (f.found) {
        if (f.ent[11] & ATTR_DIR)
            goto out;
        fat_free_chain(fs, ent_cluster(f.ent));
        remove_entry(fs, &d, f.off, f.nlfn);
    }
    blk = malloc(cb);
    if (!blk)
        goto out;
    for (;;) {
        size_t got = 0;
        uint32_t cl;

        while (got < cb) {
            long k = src(sctx, blk + got, cb - got);

            if (k < 0)
                goto fail;
            if (k == 0)
                break;
            got += (size_t)k;
        }
        if (got == 0)
            break;
        if (got < cb)
            memset(blk + got, 0, cb - got);
        cl = fat_alloc(fs);
        if (!cl)
            goto fail;
        if (prev)
            fs->fat[prev] = cl;
        else
            first = cl;
        prev = cl;
        if (wbuf_put(fs, cl, blk) < 0)
            goto fail;
        total += (uint32_t)got;
        if (got < cb)
            break;
    }
    if (wbuf_flush(fs) < 0)
        goto fail;
    if (add_entry(fs, &d, comps[n - 1], 0x20, first, total) < 0 || dir_store(fs, &d) < 0)
        goto fail;
    r = 0;
    free(blk);
    goto out;
fail:
    free(blk);
    if (first)
        fat_free_chain(fs, first);
out:
    free(d.data);
    return r;
}

static long fd_src(void *ctx, void *buf, size_t n)
{
    return read(*(int *)ctx, buf, n);
}

struct mem_src {
    const uint8_t *p;
    size_t         left;
};

static long mem_src_fn(void *ctx, void *buf, size_t n)
{
    struct mem_src *m = ctx;

    if (n > m->left)
        n = m->left;
    memcpy(buf, m->p, n);
    m->p += n;
    m->left -= n;
    return (long)n;
}

int fat32_put_fd(struct fat32 *fs, const char *path, int srcfd)
{
    return put_stream(fs, path, fd_src, &srcfd);
}

int fat32_put_mem(struct fat32 *fs, const char *path, const void *data, size_t size)
{
    struct mem_src m = { data, size };

    return put_stream(fs, path, mem_src_fn, &m);
}

static int resolve(struct fat32 *fs, const char *path, struct dirbuf *d, struct find_ctx *f)
{
    static char comps[32][256];
    uint32_t dcl;
    int n = split_path(path, comps, 32);

    if (n <= 0)
        return -1;
    if (walk_dirs(fs, comps, n - 1, 0, &dcl) < 0)
        return -1;
    if (dir_load(fs, dcl, d) < 0)
        return -1;
    memset(f, 0, sizeof(*f));
    f->want = comps[n - 1];
    dir_walk(d, find_cb, f);
    if (!f->found) {
        free(d->data);
        return -1;
    }
    return 0;
}

int fat32_cat(struct fat32 *fs, const char *path, int outfd)
{
    struct dirbuf d;
    struct find_ctx f;
    uint32_t cl, left;
    uint8_t *blk;
    int r = 0;

    if (resolve(fs, path, &d, &f) < 0)
        return -1;
    free(d.data);
    if (f.ent[11] & ATTR_DIR)
        return -1;
    left = g32(f.ent + 28);
    cl = ent_cluster(f.ent);
    blk = malloc(cluster_bytes(fs));
    if (!blk)
        return -1;
    while (left && cl >= 2 && cl < EOC) {
        uint32_t n = left < cluster_bytes(fs) ? left : cluster_bytes(fs);

        if (cl_read(fs, cl, blk) < 0 || write(outfd, blk, n) != (long)n) {
            r = -1;
            break;
        }
        left -= n;
        cl = fat_next(fs, cl);
    }
    free(blk);
    return r;
}

struct ls_ctx {
    void (*cb)(const char *, uint32_t, int, void *);
    void *ctx;
};

static int ls_cb(const uint8_t *e, const char *name, uint32_t off, uint32_t nlfn, void *ctx)
{
    struct ls_ctx *l = ctx;

    (void)off;
    (void)nlfn;
    if (e[0] == '.')
        return 0;
    l->cb(name, g32(e + 28), (e[11] & ATTR_DIR) != 0, l->ctx);
    return 0;
}

int fat32_ls(struct fat32 *fs, const char *path,
             void (*cb)(const char *name, uint32_t size, int isdir, void *ctx), void *ctx)
{
    static char comps[32][256];
    struct dirbuf d;
    struct ls_ctx l = { cb, ctx };
    uint32_t cl;
    int n = split_path(path, comps, 32);

    if (n < 0 || walk_dirs(fs, comps, n, 0, &cl) < 0)
        return -1;
    if (dir_load(fs, cl, &d) < 0)
        return -1;
    dir_walk(&d, ls_cb, &l);
    free(d.data);
    return 0;
}

int fat32_remove(struct fat32 *fs, const char *path)
{
    static char comps[32][256];
    struct dirbuf d;
    struct find_ctx f;
    uint32_t dcl;
    int n = split_path(path, comps, 32), r;

    if (n <= 0 || !fs->writable || walk_dirs(fs, comps, n - 1, 0, &dcl) < 0)
        return -1;
    if (dir_load(fs, dcl, &d) < 0)
        return -1;
    memset(&f, 0, sizeof(f));
    f.want = comps[n - 1];
    dir_walk(&d, find_cb, &f);
    if (!f.found || (f.ent[11] & ATTR_DIR)) {
        free(d.data);
        return -1;
    }
    fat_free_chain(fs, ent_cluster(f.ent));
    remove_entry(fs, &d, f.off, f.nlfn);
    r = dir_store(fs, &d);
    free(d.data);
    return r;
}
