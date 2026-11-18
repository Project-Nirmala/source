#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static char **av;
static int ac, pos;

static int file_test(char op, const char *path)
{
    struct stat st;

    if (stat(path, &st) < 0)
        return 0;
    switch (op) {
    case 'e': return 1;
    case 'f': return S_ISREG(st.st_mode);
    case 'd': return S_ISDIR(st.st_mode);
    case 's': return st.st_size > 0;
    case 'r': case 'w': case 'x': return 1;
    }
    return 0;
}

static int primary(void)
{
    const char *a;

    if (pos >= ac)
        return 0;
    a = av[pos];
    if (!strcmp(a, "!")) {
        pos++;
        return !primary();
    }
    if (a[0] == '-' && a[1] && !a[2] && pos + 1 < ac && strchr("efdsrwx", a[1])) {
        pos += 2;
        return file_test(a[1], av[pos - 1]);
    }
    if (!strcmp(a, "-z") && pos + 1 < ac) {
        pos += 2;
        return av[pos - 1][0] == 0;
    }
    if (!strcmp(a, "-n") && pos + 1 < ac) {
        pos += 2;
        return av[pos - 1][0] != 0;
    }
    if (pos + 2 < ac) {
        const char *op = av[pos + 1], *b = av[pos + 2];

        if (!strcmp(op, "=") || !strcmp(op, "==")) {
            pos += 3;
            return !strcmp(a, b);
        }
        if (!strcmp(op, "!=")) {
            pos += 3;
            return strcmp(a, b) != 0;
        }
        if (op[0] == '-' && strlen(op) == 3 && strchr("eqnlgt", op[1])) {
            long x = atol(a), y = atol(b);

            pos += 3;
            if (!strcmp(op, "-eq")) return x == y;
            if (!strcmp(op, "-ne")) return x != y;
            if (!strcmp(op, "-lt")) return x < y;
            if (!strcmp(op, "-le")) return x <= y;
            if (!strcmp(op, "-gt")) return x > y;
            if (!strcmp(op, "-ge")) return x >= y;
        }
    }
    pos++;
    return a[0] != 0;
}

static int expr(void)
{
    int v = primary();

    while (pos < ac) {
        if (!strcmp(av[pos], "-a")) {
            pos++;
            v = primary() && v;
        } else if (!strcmp(av[pos], "-o")) {
            pos++;
            v = primary() || v;
        } else {
            break;
        }
    }
    return v;
}

int main(int argc, char **argv)
{
    const char *name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    int r;

    av = argv + 1;
    ac = argc - 1;
    if (!strcmp(name, "[")) {
        if (ac < 1 || strcmp(av[ac - 1], "]")) {
            dprintf(2, "[: missing ]\n");
            return 2;
        }
        ac--;
    }
    r = ac ? expr() : 0;
    if (pos < ac) {
        dprintf(2, "%s: syntax error\n", name);
        return 2;
    }
    return r ? 0 : 1;
}
