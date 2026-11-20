#include <fnmatch.h>
#include <ctype.h>
#include <string.h>

static int in_class(const char *name, size_t n, int c)
{
    if (n == 5 && !strncmp(name, "alpha", 5)) return isalpha(c);
    if (n == 5 && !strncmp(name, "digit", 5)) return isdigit(c);
    if (n == 5 && !strncmp(name, "alnum", 5)) return isalnum(c);
    if (n == 5 && !strncmp(name, "upper", 5)) return isupper(c);
    if (n == 5 && !strncmp(name, "lower", 5)) return islower(c);
    if (n == 5 && !strncmp(name, "space", 5)) return isspace(c);
    if (n == 5 && !strncmp(name, "punct", 5)) return ispunct(c);
    if (n == 5 && !strncmp(name, "print", 5)) return isprint(c);
    if (n == 5 && !strncmp(name, "graph", 5)) return isgraph(c);
    if (n == 5 && !strncmp(name, "cntrl", 5)) return iscntrl(c);
    if (n == 6 && !strncmp(name, "xdigit", 6)) return isxdigit(c);
    if (n == 5 && !strncmp(name, "blank", 5)) return c == ' ' || c == '\t';
    return 0;
}

static int bracket(const char **pp, int c, int flags)
{
    const char *p = *pp;
    int negate = 0, matched = 0, first = 1;

    if (*p == '!' || *p == '^') {
        negate = 1;
        p++;
    }
    for (;; first = 0) {
        int lo, hi;

        if (!*p)
            return -1;
        if (*p == ']' && !first)
            break;
        if (*p == '[' && p[1] == ':') {
            const char *e = strstr(p + 2, ":]");

            if (e) {
                if (in_class(p + 2, (size_t)(e - p - 2), c))
                    matched = 1;
                p = e + 2;
                continue;
            }
        }
        if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
            p++;
        lo = hi = (unsigned char)*p++;
        if (*p == '-' && p[1] && p[1] != ']') {
            p++;
            if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
                p++;
            hi = (unsigned char)*p++;
        }
        if (c >= lo && c <= hi)
            matched = 1;
    }
    *pp = p + 1;
    return negate ? !matched : matched;
}

static int do_match(const char *p, const char *s, int flags, int at_start)
{
    for (;;) {
        int blocked = (flags & FNM_PERIOD) && at_start && *s == '.';

        switch (*p) {
        case 0:
            return *s ? FNM_NOMATCH : 0;
        case '?':
            if (!*s || blocked || ((flags & FNM_PATHNAME) && *s == '/'))
                return FNM_NOMATCH;
            p++;
            s++;
            break;
        case '*':
            if (blocked)
                return FNM_NOMATCH;
            while (*p == '*')
                p++;
            if (!*p)
                return (flags & FNM_PATHNAME) && strchr(s, '/') ? FNM_NOMATCH : 0;
            for (;; s++) {
                if (do_match(p, s, flags, 0) == 0)
                    return 0;
                if (!*s || ((flags & FNM_PATHNAME) && *s == '/'))
                    return FNM_NOMATCH;
            }
        case '[': {
            const char *q = p + 1;
            int r;

            if (!*s || blocked || ((flags & FNM_PATHNAME) && *s == '/'))
                return FNM_NOMATCH;
            r = bracket(&q, (unsigned char)*s, flags);
            if (r < 0) {
                if (*s != '[')
                    return FNM_NOMATCH;
                p++;
                s++;
            } else {
                if (!r)
                    return FNM_NOMATCH;
                p = q;
                s++;
            }
            break;
        }
        case '\\':
            if (!(flags & FNM_NOESCAPE) && p[1])
                p++;
            /* fall through - the escaped character is matched literally */
        default:
            if (*p != *s)
                return FNM_NOMATCH;
            p++;
            s++;
            break;
        }
        at_start = (flags & FNM_PATHNAME) && s[-1] == '/';
    }
}

int fnmatch(const char *pattern, const char *string, int flags)
{
    return do_match(pattern, string, flags, 1);
}
