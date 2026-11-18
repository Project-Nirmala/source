#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef MALLOC_TEST
void *test_sbrk(long inc);
#define SBRK test_sbrk
#else
#define SBRK sbrk
#endif

#define HDR       16u
#define MINBLK    32u
#define GROW_MIN  (64u * 1024u)
#define TRIM_MIN  (256u * 1024u)
#define INUSE     1u
#define PINUSE    2u
#define FLAGS     3u

struct blk {
    size_t prev_size;
    size_t size;
    struct blk *next, *prev;
};

static struct blk *free_list;
static char *heap_start, *heap_end;

static size_t bsize(const struct blk *b)
{
    return b->size & ~(size_t)FLAGS;
}

static struct blk *next_blk(struct blk *b)
{
    char *n = (char *)b + bsize(b);

    return n < heap_end ? (struct blk *)n : NULL;
}

static void list_add(struct blk *b)
{
    b->prev = NULL;
    b->next = free_list;
    if (free_list)
        free_list->prev = b;
    free_list = b;
}

static void list_del(struct blk *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        free_list = b->next;
    if (b->next)
        b->next->prev = b->prev;
}

static void mark_free(struct blk *b, size_t size, size_t pinuse)
{
    struct blk *n;

    b->size = size | pinuse;
    n = next_blk(b);
    if (n) {
        n->prev_size = size;
        n->size &= ~(size_t)PINUSE;
    }
    list_add(b);
}

static struct blk *grow(size_t need)
{
    size_t inc = need < GROW_MIN ? GROW_MIN : (need + 4095u) & ~(size_t)4095u;
    char *p;
    struct blk *b, *last = NULL;

    if (!heap_start) {
        char *cur = SBRK(0);
        size_t pad;

        if (cur == (char *)-1)
            return NULL;
        pad = (16u - ((size_t)cur & 15u)) & 15u;
        if (pad && SBRK((long)pad) == (void *)-1)
            return NULL;
        heap_start = heap_end = cur + pad;
    }
    p = SBRK((long)inc);
    if (p == (char *)-1)
        return NULL;
    if (p != heap_end) {
        SBRK(-(long)inc);
        return NULL;
    }
    for (b = free_list; b; b = b->next)
        if ((char *)b + bsize(b) == heap_end)
            last = b;
    heap_end += inc;
    if (last) {
        size_t pinuse = last->size & PINUSE, size = bsize(last) + inc;

        list_del(last);
        mark_free(last, size, pinuse);
        return last;
    }
    b = (struct blk *)(heap_end - inc);
    mark_free(b, inc, PINUSE);
    return b;
}

static void *take(struct blk *b, size_t need)
{
    size_t size = bsize(b), pinuse = b->size & PINUSE;
    struct blk *n;

    list_del(b);
    if (size - need >= MINBLK) {
        struct blk *r = (struct blk *)((char *)b + need);

        b->size = need | pinuse | INUSE;
        mark_free(r, size - need, PINUSE);
    } else {
        b->size = size | pinuse | INUSE;
        n = next_blk(b);
        if (n)
            n->size |= PINUSE;
    }
    return (char *)b + HDR;
}

void *malloc(size_t n)
{
    size_t need;
    struct blk *b;

    if (n == 0)
        n = 1;
    if (n > (size_t)1 << 40) {
        errno = ENOMEM;
        return NULL;
    }
    need = (n + HDR + 15u) & ~(size_t)15u;
    if (need < MINBLK)
        need = MINBLK;
    for (b = free_list; b; b = b->next)
        if (bsize(b) >= need)
            return take(b, need);
    b = grow(need);
    if (!b) {
        errno = ENOMEM;
        return NULL;
    }
    return take(b, need);
}

static void trim(struct blk *b)
{
    size_t size = bsize(b);

    if ((char *)b + size != heap_end || size < TRIM_MIN)
        return;
    {
        size_t give = (size - GROW_MIN) & ~(size_t)4095u;
        size_t pinuse = b->size & PINUSE;

        if (give == 0)
            return;
        list_del(b);
        if (SBRK(-(long)give) == (void *)-1) {
            list_add(b);
            return;
        }
        heap_end -= give;
        mark_free(b, size - give, pinuse);
    }
}

void free(void *p)
{
    struct blk *b, *n;
    size_t size, pinuse;

    if (!p)
        return;
    b = (struct blk *)((char *)p - HDR);
    size = bsize(b);
    pinuse = b->size & PINUSE;
    n = next_blk(b);
    if (n && !(n->size & INUSE)) {
        size += bsize(n);
        list_del(n);
    }
    if (!pinuse) {
        struct blk *pr = (struct blk *)((char *)b - b->prev_size);

        list_del(pr);
        size += bsize(pr);
        pinuse = pr->size & PINUSE;
        b = pr;
    }
    mark_free(b, size, pinuse);
    trim(b);
}

void *calloc(size_t count, size_t size)
{
    void *p;

    if (size && count > (size_t)-1 / size) {
        errno = ENOMEM;
        return NULL;
    }
    p = malloc(count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct blk *b, *nx;
    size_t need, cur;
    void *q;

    if (!p)
        return malloc(n);
    if (n == 0) {
        free(p);
        return NULL;
    }
    b = (struct blk *)((char *)p - HDR);
    cur = bsize(b);
    need = (n + HDR + 15u) & ~(size_t)15u;
    if (need < MINBLK)
        need = MINBLK;
    if (need <= cur) {
        if (cur - need >= MINBLK) {
            struct blk *r = (struct blk *)((char *)b + need);
            struct blk *after = next_blk(b);
            size_t rsize = cur - need;

            b->size = need | (b->size & PINUSE) | INUSE;
            if (after && !(after->size & INUSE)) {
                rsize += bsize(after);
                list_del(after);
            }
            mark_free(r, rsize, PINUSE);
            trim(r);
        }
        return p;
    }
    nx = next_blk(b);
    if (nx && !(nx->size & INUSE) && cur + bsize(nx) >= need) {
        size_t total = cur + bsize(nx);

        list_del(nx);
        if (total - need >= MINBLK) {
            struct blk *r = (struct blk *)((char *)b + need);

            b->size = need | (b->size & PINUSE) | INUSE;
            mark_free(r, total - need, PINUSE);
        } else {
            struct blk *after;

            b->size = total | (b->size & PINUSE) | INUSE;
            after = next_blk(b);
            if (after)
                after->size |= PINUSE;
        }
        return p;
    }
    q = malloc(n);
    if (!q)
        return NULL;
    memcpy(q, p, cur - HDR < n ? cur - HDR : n);
    free(p);
    return q;
}

#ifdef MALLOC_TEST
int malloc_check(size_t *free_bytes, size_t *nfree)
{
    char *p = heap_start;
    int bad = 0;
    struct blk *prev = NULL;
    size_t listed = 0;
    struct blk *l;

    *free_bytes = *nfree = 0;
    while (p && p < heap_end) {
        struct blk *b = (struct blk *)p;
        size_t s = bsize(b);

        if (s < MINBLK || (s & 15u) || p + s > heap_end) {
            bad++;
            break;
        }
        if (prev) {
            if (!!(b->size & PINUSE) != !!(prev->size & INUSE))
                bad++;
            if (!(prev->size & INUSE) && b->prev_size != bsize(prev))
                bad++;
            if (!(prev->size & INUSE) && !(b->size & INUSE))
                bad++;
        } else if (!(b->size & PINUSE)) {
            bad++;
        }
        if (!(b->size & INUSE)) {
            *free_bytes += s;
            (*nfree)++;
        }
        prev = b;
        p += s;
    }
    for (l = free_list; l; l = l->next) {
        listed++;
        if (l->size & INUSE)
            bad++;
    }
    if (listed != *nfree)
        bad++;
    return bad;
}
#endif
