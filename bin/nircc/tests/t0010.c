#include <stdio.h>

static int counter(void) {
    static int n;

    n = n + 1;
    return n;
}

static int twice(int x) {
    return x * 2;
}

int main(void) {
    int (*fp)(int);

    printf("%d\n", counter());
    printf("%d\n", counter());
    printf("%d\n", counter());
    fp = twice;
    return fp(21);
}
