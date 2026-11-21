#include <cmdline.h>
#include <kern.h>

static char line[256];

void cmdline_set(const char *s)
{
    size_t n = s ? strlen(s) : 0;

    if (n >= sizeof(line))
        n = sizeof(line) - 1;
    if (n)
        memcpy(line, s, n);
    line[n] = 0;
}

const char *cmdline_get(void)
{
    return line;
}

bool cmdline_value(const char *key, char *out, size_t size)
{
    const char *p = line;
    size_t kl = strlen(key);

    while (*p) {
        const char *s;

        while (*p == ' ')
            p++;
        s = p;
        while (*p && *p != ' ')
            p++;
        if ((size_t)(p - s) > kl && !memcmp(s, key, kl) && s[kl] == '=') {
            size_t n = (size_t)(p - s) - kl - 1;

            if (n >= size)
                n = size - 1;
            memcpy(out, s + kl + 1, n);
            out[n] = 0;
            return true;
        }
    }
    return false;
}
