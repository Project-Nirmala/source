#include <stdio.h>

int main(void) {
    int i;
    int sum;

    sum = 0;
    for (i = 1; i <= 5; i++) {
        sum += i;
        printf("i=%d sum=%d\n", i, sum);
    }
    return sum;
}
