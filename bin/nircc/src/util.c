#include "cc.h"

#define ARENA_CHUNK 65536

struct chunk {
	struct chunk *next;
	size_t cap;
	size_t used;
	char *data;
};

static struct chunk *chunks;

const char *cur_file = "<input>";

void die(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "nircc: error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void die_at(int line, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s:%d: error: ", lex_file_name(line >> 20),
		line & 0xFFFFF);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void *xalloc(size_t n)
{
	struct chunk *c;
	size_t cap;
	void *p;

	n = (n + 7) & ~(size_t)7;
	c = chunks;
	if (!c || c->used + n > c->cap) {
		cap = n > ARENA_CHUNK ? n : ARENA_CHUNK;
		c = calloc(1, sizeof *c + cap);
		if (!c)
			die("out of memory");
		c->cap = cap;
		c->data = (char *)(c + 1);
		c->next = chunks;
		chunks = c;
	}
	p = c->data + c->used;
	c->used += n;
	return p;
}

char *xstrndup(const char *s, size_t n)
{
	char *p;

	p = xalloc(n + 1);
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

char *xstrdup(const char *s)
{
	return xstrndup(s, strlen(s));
}

char *read_file(const char *path)
{
	FILE *f;
	char *buf;
	long len;

	f = fopen(path, "rb");
	if (!f)
		die("cannot open '%s'", path);
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc((size_t)len + 1);
	if (!buf)
		die("out of memory");
	if (fread(buf, 1, (size_t)len, f) != (size_t)len)
		die("cannot read '%s'", path);
	buf[len] = '\0';
	fclose(f);
	return buf;
}

static const char *scan_skip(const char *s)
{
	while (isspace((unsigned char)*s))
		s++;
	return s;
}

int cc_sscanf(const char *s, const char *fmt, ...)
{
	va_list ap;
	int n;
	int w;
	int lng;
	int i;
	int neg;
	int inv;
	int hit;
	long v;
	char *d;
	char *cp;
	long *lp;
	int *ip;
	const char *set;
	const char *e;
	const char *t;

	n = 0;
	va_start(ap, fmt);
	while (*fmt) {
		if (isspace((unsigned char)*fmt)) {
			s = scan_skip(s);
			fmt++;
			continue;
		}
		if (*fmt != '%') {
			if (*s != *fmt)
				break;
			s++;
			fmt++;
			continue;
		}
		fmt++;
		if (*fmt == '%') {
			if (*s != '%')
				break;
			s++;
			fmt++;
			continue;
		}
		w = 0;
		while (isdigit((unsigned char)*fmt))
			w = w * 10 + (*fmt++ - '0');
		lng = 0;
		if (*fmt == 'l') {
			lng = 1;
			fmt++;
		}
		if (*fmt == 'c') {
			if (!*s)
				break;
			cp = va_arg(ap, char *);
			*cp = *s++;
			n++;
			fmt++;
			continue;
		}
		if (*fmt == 's') {
			s = scan_skip(s);
			if (!*s)
				break;
			d = va_arg(ap, char *);
			i = 0;
			while (*s && !isspace((unsigned char)*s) && (!w || i < w))
				d[i++] = *s++;
			d[i] = '\0';
			n++;
			fmt++;
			continue;
		}
		if (*fmt == 'd') {
			s = scan_skip(s);
			neg = 0;
			if (*s == '-' || *s == '+') {
				neg = *s == '-';
				s++;
			}
			if (!isdigit((unsigned char)*s))
				break;
			v = 0;
			while (isdigit((unsigned char)*s))
				v = v * 10 + (*s++ - '0');
			if (neg)
				v = -v;
			if (lng) {
				lp = va_arg(ap, long *);
				*lp = v;
			} else {
				ip = va_arg(ap, int *);
				*ip = (int)v;
			}
			n++;
			fmt++;
			continue;
		}
		if (*fmt == '[') {
			fmt++;
			inv = 0;
			if (*fmt == '^') {
				inv = 1;
				fmt++;
			}
			set = fmt;
			while (*fmt && *fmt != ']')
				fmt++;
			e = fmt;
			if (*fmt == ']')
				fmt++;
			d = va_arg(ap, char *);
			i = 0;
			while (*s && (!w || i < w)) {
				hit = 0;
				for (t = set; t < e; t++)
					if (*t == *s)
						hit = 1;
				if (hit == inv)
					break;
				d[i++] = *s++;
			}
			if (i == 0)
				break;
			d[i] = '\0';
			n++;
			continue;
		}
		break;
	}
	va_end(ap);
	return n;
}
