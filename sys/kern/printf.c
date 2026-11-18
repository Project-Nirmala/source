#include <kern.h>
#include <serial.h>

static void putc_(char c)
{
    serial_putc(c);
}

void kputs(const char *s)
{
    while (*s)
        putc_(*s++);
}

static void put_num(uint64_t v, unsigned base, bool neg, bool upper,
                    int width, bool zero, bool left)
{
    char buf[32];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, pad;

    if (v == 0)
        buf[n++] = '0';
    while (v) {
        buf[n++] = dig[v % base];
        v /= base;
    }
    if (neg)
        buf[n++] = '-';

    pad = width - n;
    if (!left && !zero)
        while (pad-- > 0)
            putc_(' ');
    if (neg && zero && !left) {
        putc_('-');
        n--;
    }
    if (!left && zero)
        while (pad-- > 0)
            putc_('0');
    while (n > 0)
        putc_(buf[--n]);
    if (left)
        while (pad-- > 0)
            putc_(' ');
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        int width = 0, lmod = 0;
        bool zero = false, left = false;

        if (*fmt != '%') {
            putc_(*fmt);
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '0')      zero = true;
            else if (*fmt == '-') left = true;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l' || *fmt == 'z') {
            lmod++;
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            putc_((char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len, pad;
            if (!s)
                s = "(null)";
            len = (int)strlen(s);
            pad = width - len;
            if (!left)
                while (pad-- > 0)
                    putc_(' ');
            kputs(s);
            if (left)
                while (pad-- > 0)
                    putc_(' ');
            break;
        }
        case 'd':
        case 'i': {
            int64_t v = lmod ? va_arg(ap, int64_t) : va_arg(ap, int);
            bool neg = v < 0;
            put_num(neg ? (uint64_t)0 - (uint64_t)v : (uint64_t)v,
                    10, neg, false, width, zero, left);
            break;
        }
        case 'u': {
            uint64_t v = lmod ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            put_num(v, 10, false, false, width, zero, left);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = lmod ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
            put_num(v, 16, false, *fmt == 'X', width, zero, left);
            break;
        }
        case 'p':
            kputs("0x");
            put_num((uint64_t)va_arg(ap, void *), 16, false, false, 16, true, false);
            break;
        case '%':
            putc_('%');
            break;
        case '\0':
            return;
        default:
            putc_('%');
            putc_(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
