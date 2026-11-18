#include <stdio.h>

int main(void) {
    unsigned char c;
    unsigned int u;
    short s;

    c = 200;
    u = 0;
    u = u - 1;
    s = -5;
    printf("%d\n", c);
    printf("%d\n", u > 5);
    printf("%d\n", s);
    printf("%d\n", (unsigned char)(c + 100));
    return 0;
}
