#include <string.h>
#include <unistd.h>

static int put_escaped(const char *s)
{
    for (; *s; s++) {
        char c = *s;

        if (c == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'e': c = 0x1B; break;
            case '\\': c = '\\'; break;
            case 'c': return 1;
            default:
                write(1, "\\", 1);
                c = *s;
                break;
            }
        }
        write(1, &c, 1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i = 1, nl = 1, esc = 0;

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *f = argv[i] + 1;

        while (*f == 'n' || *f == 'e' || *f == 'E')
            f++;
        if (*f)
            break;
        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'n')
                nl = 0;
            else if (*f == 'e')
                esc = 1;
            else
                esc = 0;
        }
    }
    for (; i < argc; i++) {
        if (esc ? put_escaped(argv[i]) : (write(1, argv[i], strlen(argv[i])), 0)) {
            nl = 0;
            break;
        }
        if (i + 1 < argc)
            write(1, " ", 1);
    }
    if (nl)
        write(1, "\n", 1);
    return 0;
}
