#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

int malloc_check(size_t *free_bytes, size_t *nfree);
int nm_errno;

#define malloc  nm_malloc
#define free    nm_free
#define calloc  nm_calloc
#define realloc nm_realloc
void *nm_malloc(size_t); void nm_free(void *); void *nm_calloc(size_t, size_t); void *nm_realloc(void *, size_t);

#define ARENA (64u << 20)
static char *arena; static size_t brk_off; static size_t max_brk, limit = ARENA;
void *test_sbrk(long inc)
{
    if (!arena) { arena = mmap((void *)0x20000000, ARENA, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0); if (arena == MAP_FAILED) { perror("mmap"); exit(2); } brk_off = 7; }
    if (inc < 0 && (size_t)(-inc) > brk_off) return (void *)-1;
    if (inc > 0 && brk_off + (size_t)inc > limit) return (void *)-1;
    void *old = arena + brk_off; brk_off += (size_t)inc; if (brk_off > max_brk) max_brk = brk_off;
    if (inc < 0) memset(arena + brk_off, 0xEE, (size_t)(-inc));
    return old;
}

static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)
struct slot { unsigned char *p; size_t n; unsigned char tag; };
static int intact(struct slot *s, size_t upto) { for (size_t i = 0; i < upto; i++) if (s->p[i] != (unsigned char)(s->tag + i)) return 0; return 1; }
static void fill(struct slot *s, size_t from) { for (size_t i = from; i < s->n; i++) s->p[i] = (unsigned char)(s->tag + i); }

int main(void)
{
    void *a = malloc(1);
    T(a && ((uintptr_t)a & 15) == 0, "16-byte alignment even though the break starts misaligned");
    free(a);
    srand(99);
    enum { N = 600 };
    static struct slot sl[N];
    size_t fb, nf;
    for (long iter = 0; iter < 400000; iter++) {
        int i = rand() % N; struct slot *s = &sl[i]; int op = rand() % 10;
        size_t sz = (rand() % 8 == 0) ? (size_t)(rand() % 70000) : (size_t)(rand() % 300);
        if (sz == 0) sz = 1;
        if (!s->p) { s->p = malloc(sz); if (!s->p) { printf("FAIL: malloc(%zu) returned NULL\n", sz); fails++; break; } s->n = sz; s->tag = (unsigned char)rand(); fill(s, 0); T(((uintptr_t)s->p & 15) == 0, "aligned"); }
        else if (op < 4) { if (!intact(s, s->n)) { printf("FAIL: corruption in slot %d at iter %ld\n", i, iter); fails++; break; } free(s->p); s->p = NULL; }
        else if (op < 8) { size_t keep = sz < s->n ? sz : s->n; unsigned char *q = realloc(s->p, sz); if (!q) { printf("FAIL: realloc NULL\n"); fails++; break; } s->p = q; if (!intact(s, keep)) { printf("FAIL: realloc lost data in slot %d (iter %ld)\n", i, iter); fails++; break; } s->n = sz; fill(s, keep); }
        else { if (!intact(s, s->n)) { printf("FAIL: corruption\n"); fails++; break; } }
        if (iter % 5000 == 0 && malloc_check(&fb, &nf)) { printf("FAIL: heap invariants broken at iter %ld\n", iter); fails++; break; }
        if (iter % 997 == 0) {
            for (int a2 = 0; a2 < N; a2++) for (int b2 = a2 + 1; b2 < N && b2 < a2 + 8; b2++) if (sl[a2].p && sl[b2].p && sl[a2].p < sl[b2].p + sl[b2].n && sl[b2].p < sl[a2].p + sl[a2].n) { printf("FAIL: overlap %d %d\n", a2, b2); fails++; return 1; }
        }
    }
    for (int i = 0; i < N; i++) { if (sl[i].p) { T(intact(&sl[i], sl[i].n), "data intact at the end"); free(sl[i].p); sl[i].p = NULL; } }
    T(malloc_check(&fb, &nf) == 0, "heap invariants hold after freeing everything");
    T(nf == 1, "everything coalesced into a single free block");
    printf("  high-water mark %zu KiB, final break %zu KiB (heap trimmed back)\n", max_brk >> 10, brk_off >> 10);
    T(brk_off < (1u << 20), "free memory at the top of the heap went back to the kernel");

    unsigned char *z = nm_calloc(1000, 8); int allz = 1; for (int i = 0; i < 8000; i++) if (z[i]) allz = 0; T(allz, "calloc returns zeroed memory even from recycled blocks");
    nm_free(z);
    T(nm_calloc((size_t)1 << 62, 8) == NULL, "calloc overflow is detected");
    limit = brk_off + (1 << 20); T(nm_malloc(10u << 20) == NULL, "out of memory returns NULL"); void *ok = nm_malloc(100); T(ok != NULL, "the allocator still works after a failure"); nm_free(ok);
    T(nm_realloc(NULL, 50) != NULL, "realloc(NULL, n) is malloc");
    { void *q = nm_malloc(40); T(nm_realloc(q, 0) == NULL, "realloc(p, 0) frees and returns NULL"); }
    printf(fails ? "malloc: %d FAILURES\n" : "malloc: all tests passed\n", fails);
    return fails != 0;
}
