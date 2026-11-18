#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

char **environ;

#define MAX_ATEXIT 32
static void (*exit_fns[MAX_ATEXIT])(void);
static int nexit_fns;

extern void __stdio_exit(void) __attribute__((weak));

int atexit(void (*fn)(void))
{
    if (nexit_fns == MAX_ATEXIT)
        return -1;
    exit_fns[nexit_fns++] = fn;
    return 0;
}

void exit(int status)
{
    while (nexit_fns > 0)
        exit_fns[--nexit_fns]();
    if (__stdio_exit)
        __stdio_exit();
    _exit(status);
}

void abort(void)
{
    _exit(134);
}

int system(const char *cmd)
{
    pid_t pid;
    int status = 0;

    if (!cmd)
        return 1;
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return status;
}

long strtol(const char *s, char **end, int base)
{
    long v = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }
    for (;; s++) {
        int d;

        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (*s >= 'a' && *s <= 'z')
            d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z')
            d = *s - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
    }
    if (end)
        *end = (char *)s;
    return neg ? -v : v;
}

int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}

long atol(const char *s)
{
    return strtol(s, 0, 10);
}

static void swap(char *a, char *b, size_t n)
{
    while (n--) {
        char t = *a;

        *a++ = *b;
        *b++ = t;
    }
}

static void sift(char *base, size_t size, size_t start, size_t n,
                 int (*cmp)(const void *, const void *))
{
    size_t root = start;

    while (root * 2 + 1 < n) {
        size_t child = root * 2 + 1;

        if (child + 1 < n && cmp(base + child * size, base + (child + 1) * size) < 0)
            child++;
        if (cmp(base + root * size, base + child * size) >= 0)
            return;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}

void qsort(void *vbase, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    char *base = vbase;
    size_t i;

    if (n < 2)
        return;
    for (i = n / 2; i > 0; i--)
        sift(base, size, i - 1, n, cmp);
    for (i = n - 1; i > 0; i--) {
        swap(base, base + i * size, size);
        sift(base, size, 0, i, cmp);
    }
}

const char *strerror(int err)
{
    switch (err) {
    case 0:         return "Success";
    case EPERM:     return "Operation not permitted";
    case ENOENT:    return "No such file or directory";
    case ESRCH:     return "No such process";
    case EINTR:     return "Interrupted";
    case EIO:       return "I/O error";
    case E2BIG:     return "Argument list too long";
    case ENOEXEC:   return "Exec format error";
    case EBADF:     return "Bad file descriptor";
    case ECHILD:    return "No child processes";
    case ENOMEM:    return "Out of memory";
    case EACCES:    return "Permission denied";
    case EFAULT:    return "Bad address";
    case EBUSY:     return "Resource busy";
    case EEXIST:    return "File exists";
    case EXDEV:     return "Cross-device link";
    case ENOTDIR:   return "Not a directory";
    case EISDIR:    return "Is a directory";
    case EINVAL:    return "Invalid argument";
    case EMFILE:    return "Too many open files";
    case ENOTTY:    return "Not a terminal";
    case EFBIG:     return "File too large";
    case ENOSPC:    return "No space left on device";
    case ESPIPE:    return "Illegal seek";
    case EROFS:     return "Read-only file system";
    case EPIPE:     return "Broken pipe";
    case ERANGE:    return "Result too large";
    case ENAMETOOLONG: return "File name too long";
    case ENOSYS:    return "Function not implemented";
    case ENOTEMPTY: return "Directory not empty";
    default:        return "Unknown error";
    }
}

static int env_owned;

static char *env_find(const char *name, size_t n, char ***slot)
{
    char **e;

    for (e = environ; e && *e; e++)
        if (!strncmp(*e, name, n) && (*e)[n] == '=') {
            if (slot)
                *slot = e;
            return *e + n + 1;
        }
    return 0;
}

char *getenv(const char *name)
{
    return env_find(name, strlen(name), 0);
}

static int env_make_owned(size_t extra)
{
    size_t n = 0;
    char **copy;

    while (environ && environ[n])
        n++;
    copy = malloc((n + extra + 1) * sizeof(char *));
    if (!copy)
        return -1;
    if (n)
        memcpy(copy, environ, n * sizeof(char *));
    copy[n] = 0;
    environ = copy;
    env_owned = 1;
    return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
    size_t nl = strlen(name), vl = strlen(value), n = 0;
    char **slot, *entry;

    if (nl == 0 || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (env_find(name, nl, &slot) && !overwrite)
        return 0;
    entry = malloc(nl + vl + 2);
    if (!entry)
        return -1;
    memcpy(entry, name, nl);
    entry[nl] = '=';
    memcpy(entry + nl + 1, value, vl + 1);
    if (env_find(name, nl, &slot)) {
        *slot = entry;
        return 0;
    }
    while (environ && environ[n])
        n++;
    if (!env_owned && env_make_owned(1) < 0) {
        free(entry);
        return -1;
    }
    environ = realloc(environ, (n + 2) * sizeof(char *));
    if (!environ) {
        free(entry);
        return -1;
    }
    environ[n] = entry;
    environ[n + 1] = 0;
    return 0;
}

int putenv(char *string)
{
    char *eq = strchr(string, '=');
    size_t nl;
    char **slot;
    size_t n = 0;

    if (!eq)
        return unsetenv(string);
    nl = (size_t)(eq - string);
    if (env_find(string, nl, &slot)) {
        *slot = string;
        return 0;
    }
    while (environ && environ[n])
        n++;
    if (!env_owned && env_make_owned(1) < 0)
        return -1;
    environ = realloc(environ, (n + 2) * sizeof(char *));
    if (!environ)
        return -1;
    environ[n] = string;
    environ[n + 1] = 0;
    return 0;
}

int unsetenv(const char *name)
{
    size_t nl = strlen(name);
    char **slot;

    if (nl == 0 || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!env_owned && env_find(name, nl, &slot) && env_make_owned(0) < 0)
        return -1;
    while (env_find(name, nl, &slot)) {
        char **e;

        for (e = slot; *e; e++)
            e[0] = e[1];
    }
    return 0;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return (unsigned long)strtol(s, end, base);
}

double strtod(const char *s, char **end)
{
    const char *p = s;
    int neg = 0, digits = 0, ndig = 0;
    unsigned long long mant = 0;
    long exp10 = 0;
    double v, scale = 1.0;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v')
        p++;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    for (; *p >= '0' && *p <= '9'; p++, digits++) {
        if (ndig < 19) {
            mant = mant * 10 + (unsigned)(*p - '0');
            if (mant)
                ndig++;
        } else {
            exp10++;
        }
    }
    if (*p == '.') {
        p++;
        for (; *p >= '0' && *p <= '9'; p++, digits++) {
            if (ndig < 19) {
                mant = mant * 10 + (unsigned)(*p - '0');
                exp10--;
                if (mant)
                    ndig++;
            }
        }
    }
    if (!digits) {
        if (end)
            *end = (char *)s;
        return 0.0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int eneg = 0;
        long e = 0;

        if (*q == '-' || *q == '+')
            eneg = *q++ == '-';
        if (*q >= '0' && *q <= '9') {
            for (; *q >= '0' && *q <= '9'; q++)
                if (e < 100000)
                    e = e * 10 + (*q - '0');
            exp10 += eneg ? -e : e;
            p = q;
        }
    }
    v = (double)mant;
    if (exp10 < 0) {
        long n = -exp10;

        for (; n > 0 && scale < 1e300; n--)
            scale *= 10.0;
        v = n > 0 ? 0.0 : v / scale;
    } else if (exp10 > 0) {
        long n = exp10;

        for (; n > 0 && scale < 1e300; n--)
            scale *= 10.0;
        v = n > 0 ? v * 1e300 * 1e300 : v * scale;
    }
    if (end)
        *end = (char *)p;
    return neg ? -v : v;
}

double atof(const char *s)
{
    return strtod(s, 0);
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

static unsigned long rand_state = 1;

int rand(void)
{
    rand_state = rand_state * 6364136223846793005UL + 1442695040888963407UL;
    return (int)((rand_state >> 33) & RAND_MAX);
}

void srand(unsigned seed)
{
    rand_state = seed;
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *))
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *p = (const char *)base + mid * size;
        int c = cmp(key, p);

        if (c == 0)
            return (void *)p;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}
