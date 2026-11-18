#include <stdio.h>

#define A 1
#define B 0
#define ADD(x, y) ((x) + (y))
#define STR(x) #x
#define HDR "t0013.h"

#if !defined(NOPE) && (A || B) && defined(A)
#define R1 1
#else
#define R1 0
#endif

#if defined(NOPE)
#define R2 0
#elif !defined(__GNUC_NOT_SET__) && (2 + 3 * 4 == 14) && (7 >> 1) == 3
#define R2 1
#else
#define R2 0
#endif

#if 0x7fffffffffffffffL > 0xffffffffL && -1 < 0 && 'a' == 97 && L'\0' == 0
#define R3 1
#else
#define R3 0
#endif

#if (A ? 10 : 20) == 10 && UNDEFINED_NAME == 0 && (B && (1 / B))
#define R4 0
#else
#define R4 1
#endif

int main(void)
{
    int v;

    v = ADD(1,
            2);
    printf("%d %d %d %d\n", R1, R2, R3, R4);
    printf("%d\n", v);
    printf("%d\n", __LINE__);
    printf("%s\n", STR(a + b));
    return 0;
}
