int puts(char *s);

int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int print_int(int n) {
    char buf[12];
    int i;
    int is_neg;

    i = 11;
    buf[i] = '\0';
    is_neg = 0;

    if (n == 0) {
        i = i - 1;
        buf[i] = '0';
        puts(buf + i);
        return 0;
    }

    if (n < 0) {
        is_neg = 1;
        n = 0 - n;
    }

    while (n > 0) {
        i = i - 1;
        buf[i] = '0' + (n % 10);
        n = n / 10;
    }

    if (is_neg) {
        i = i - 1;
        buf[i] = '-';
    }

    puts(buf + i);
    return 0;
}

int main() {
    int res;
    res = fib(10);
    print_int(res);
    return res;
}
