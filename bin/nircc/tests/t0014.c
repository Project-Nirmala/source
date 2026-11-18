#include <stdio.h>

typedef void (*sighandler_t)(int);
typedef unsigned long size_t_;
typedef unsigned long size_t_;

extern int counter;
extern int counter;
int counter;
int counter = 5;

extern int table[];
int table[3] = { 1, 2, 3 };

void (*sigset(int, void (*)(int)))(int);
int apply(int (*)(int, int), int, int);
int (*pick(int which))(int, int);
char (*rows(char (*)[4], int))[4];

static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }
static void h1(int s) { counter += s; }

void (*sigset(int s, void (*f)(int)))(int)
{
    f(s);
    return f;
}

int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
int (*pick(int which))(int, int) { return which ? mul : add; }
char (*rows(char (*r)[4], int i))[4] { return r + i; }

struct hdr {
    int tag;
    union {
        int i;
        char c[4];
    };
    int : 0;
    int pad : 7;
    int : 3;
    char data[];
};

struct ts { long sec; int : 8 * (sizeof(long) - sizeof(long)); long nsec; };

int ok1[sizeof(long) == 8 ? 14 : 9];
int ok2[(sizeof(int) == 4) + 1];
enum { E1 = sizeof(int) * 2, E2 = E1 + (int)sizeof(char) };

#define OFFSET(t, m) ((unsigned long)&(((t *)0)->m))

int main(void)
{
    char grid[2][4];
    int (*fp)(int, int);
    void (*r)(int);
    unsigned u;
    register int reg;

    sigset(3, h1);
    r = sigset(4, h1);
    (void)r;
    printf("%d %d %d\n", counter, table[2], apply(add, 3, 4));
    fp = pick(1);
    printf("%d %d\n", fp(3, 4), pick(0)(3, 4));
    grid[1][2] = 'x';
    printf("%c\n", (*rows(grid, 1))[2]);
    printf("%d %d %d\n", (int)sizeof(ok1), (int)sizeof(ok2), E2);
    printf("%d %d\n", (int)OFFSET(struct ts, nsec), (int)sizeof(struct ts));
    {
        struct hdr h;
        h.i = 0x41424344;
        printf("%d %d\n", (int)sizeof(struct hdr), h.c[0] == 0x44);
    }
    u = 0xffffffffu;
    reg = (int)(u >> 28);
    printf("%d %u %d\n", reg, ~0u, (int)(sizeof(0xffffffffu)));
    printf("%d %d\n", -1 < 0u, (char)300);
    printf("%s\n", __func__);
    return 0;
}
