#include <stdio.h>

struct point {
    int x;
    int y;
};

enum color { RED, GREEN = 5, BLUE };

int sum(int n, ...) {
    va_list ap;
    int i;
    int s;

    s = 0;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void) {
    struct point p;
    int k;
    char *msg;

    p.x = 3;
    p.y = 4;
    msg = "ok";
    k = 0;
    switch (p.x) {
    case 3:
        k = BLUE;
        break;
    default:
        k = -1;
    }
    printf("%s %d %d %d\n", msg, p.x + p.y, k, sum(3, 1, 2, 3));
    return p.x + p.y;
}
