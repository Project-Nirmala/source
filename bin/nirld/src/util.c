#include "ld.h"

int nerrors;

void die(const char *fmt, ...)
{
	va_list ap;

	fputs("nirld: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void err(const char *fmt, ...)
{
	va_list ap;

	fputs("nirld: error: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	nerrors++;
}

void warn(const char *fmt, ...)
{
	va_list ap;

	fputs("nirld: warning: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void *xalloc(size_t n)
{
	void *p;

	p = calloc(n ? n : 1, 1);
	if (!p)
		die("out of memory");
	return p;
}

void *xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if (!p)
		die("out of memory");
	return p;
}

char *xstrdup(const char *s)
{
	char *p;
	size_t n;

	n = strlen(s) + 1;
	p = xalloc(n);
	memcpy(p, s, n);
	return p;
}

unsigned char *read_file(const char *path, size_t *len)
{
	FILE *f;
	long n;
	unsigned char *p;

	f = fopen(path, "rb");
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET) != 0)
		die("%s: cannot determine file size", path);
	p = xalloc((size_t)n);
	if (fread(p, 1, (size_t)n, f) != (size_t)n)
		die("%s: read error", path);
	fclose(f);
	*len = (size_t)n;
	return p;
}

u64 rd(const unsigned char *p, int n)
{
	u64 v;
	int i;

	v = 0;
	for (i = n - 1; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

void wr(unsigned char *p, int n, u64 v)
{
	int i;

	for (i = 0; i < n; i++) {
		p[i] = (unsigned char)(v & 0xff);
		v >>= 8;
	}
}

u64 align_up(u64 v, u64 a)
{
	if (a <= 1)
		return v;
	return (v + a - 1) / a * a;
}

s64 sext(u64 v, int bytes)
{
	u64 sign;

	if (bytes >= 8)
		return (s64)v;
	sign = (u64)1 << (bytes * 8 - 1);
	v &= (sign << 1) - 1;
	if (v & sign)
		return (s64)(v - (sign << 1));
	return (s64)v;
}

void buf_add(struct buf *b, const void *d, size_t n)
{
	if (b->len + n > b->cap) {
		b->cap = (b->len + n) * 2 + 64;
		b->p = xrealloc(b->p, b->cap);
	}
	if (n)
		memcpy(b->p + b->len, d, n);
	b->len += n;
}

size_t buf_str(struct buf *b, const char *s)
{
	size_t off;

	off = b->len;
	buf_add(b, s, strlen(s) + 1);
	return off;
}
