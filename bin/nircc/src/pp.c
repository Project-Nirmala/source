#include <ctype.h>
#include "cc.h"

#define MAX_COND 64
#define MAX_INCLUDE 16
#define MAX_EXPAND 32

struct sb {
	char *data;
	size_t len;
	size_t cap;
};

#define MAX_PARAMS 16
#define MAX_INC_DIRS 16

struct macro {
	char *name;
	char *value;
	char *params[MAX_PARAMS];
	int nparams;
	int is_func;
	int busy;
	struct macro *next;
};

static const char *inc_dirs[MAX_INC_DIRS];
static int ninc;

void pp_add_include(const char *dir)
{
	if (ninc == MAX_INC_DIRS)
		die("too many include directories");
	inc_dirs[ninc++] = dir;
}

static struct macro *macros;
static int inc_depth;
static const char *pp_file = "<input>";

static void sb_add(struct sb *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->data = realloc(b->data, b->cap);
		if (!b->data)
			die("out of memory");
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
}

static struct macro *find_macro(const char *name, size_t n)
{
	struct macro *m;

	for (m = macros; m; m = m->next)
		if (strlen(m->name) == n && strncmp(m->name, name, n) == 0)
			return m;
	return NULL;
}

void pp_define(const char *name, const char *value)
{
	struct macro *m;

	m = find_macro(name, strlen(name));
	if (!m) {
		m = xalloc(sizeof *m);
		m->name = xstrdup(name);
		m->next = macros;
		macros = m;
	}
	m->value = xstrdup(value);
	m->is_func = 0;
	m->nparams = 0;
}

static void pp_undef(const char *name, size_t n)
{
	struct macro **pp;

	for (pp = &macros; *pp; pp = &(*pp)->next)
		if (strlen((*pp)->name) == n
		    && strncmp((*pp)->name, name, n) == 0) {
			*pp = (*pp)->next;
			return;
		}
}

static void sb_init(struct sb *b)
{
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	sb_add(b, "", 0);
}

static const char *skip_str(const char *s)
{
	char quote;

	quote = *s++;
	while (*s && *s != quote) {
		if (*s == '\\' && s[1])
			s++;
		s++;
	}
	return *s ? s + 1 : s;
}

static int is_id_start(char c)
{
	return isalpha((unsigned char)c) || c == '_';
}

static int is_id_char(char c)
{
	return isalnum((unsigned char)c) || c == '_';
}

static const char *ws(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static char *trim_dup(const char *a, const char *b)
{
	char *r;

	while (a < b && (*a == ' ' || *a == '\t'))
		a++;
	while (b > a && (b[-1] == ' ' || b[-1] == '\t'))
		b--;
	r = xalloc((size_t)(b - a) + 1);
	memcpy(r, a, (size_t)(b - a));
	return r;
}

static const char *collect_args(const char *s, char **args, int *count,
				int line)
{
	const char *start;
	int level;
	int n;

	level = 1;
	n = 0;
	start = ++s;
	while (*s) {
		if (*s == '"' || *s == '\'') {
			s = skip_str(s);
			continue;
		}
		if (*s == '(') {
			level++;
		} else if (*s == ')' || (*s == ',' && level == 1)) {
			if (*s == ')' && --level == 0) {
				if (n == MAX_PARAMS)
					die_at(line, "too many macro arguments");
				args[n++] = trim_dup(start, s);
				*count = n;
				return s + 1;
			}
			if (*s == ',') {
				if (n == MAX_PARAMS)
					die_at(line, "too many macro arguments");
				args[n++] = trim_dup(start, s);
				start = s + 1;
			}
		}
		s++;
	}
	die_at(line, "unterminated macro invocation");
	return s;
}

static int param_index(struct macro *m, const char *s, size_t n)
{
	int i;

	for (i = 0; i < m->nparams; i++)
		if (strlen(m->params[i]) == n && strncmp(m->params[i], s, n) == 0)
			return i;
	return -1;
}

static void expand(const char *s, struct sb *out, int depth, int line);

static void stringize(struct sb *out, const char *a)
{
	sb_add(out, "\"", 1);
	while (*a) {
		if (*a == '"' || *a == '\\')
			sb_add(out, "\\", 1);
		sb_add(out, a, 1);
		a++;
	}
	sb_add(out, "\"", 1);
}

static void substitute(struct macro *m, char **args, struct sb *out,
		       int depth, int line)
{
	const char *b;
	const char *start;
	const char *look;
	struct sb tmp;
	int i;
	int raw;
	int pasted;

	pasted = 0;
	b = m->value;
	while (*b) {
		if (*b == '"' || *b == '\'') {
			start = b;
			b = skip_str(b);
			sb_add(out, start, (size_t)(b - start));
			pasted = 0;
		} else if (*b == '#' && b[1] == '#') {
			while (out->len && (out->data[out->len - 1] == ' '
			       || out->data[out->len - 1] == '\t'))
				out->data[--out->len] = '\0';
			b = ws(b + 2);
			pasted = 1;
		} else if (*b == '#') {
			look = ws(b + 1);
			i = is_id_start(*look) ? param_index(m, look,
			    (size_t)(0 + strspn(look, "abcdefghijklmnopqrstuvwxyz"
			    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))) : -1;
			if (i < 0) {
				sb_add(out, b++, 1);
			} else {
				stringize(out, args[i]);
				b = look + strspn(look, "abcdefghijklmnopqrstuvwxyz"
				    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
			}
			pasted = 0;
		} else if (is_id_start(*b)) {
			start = b;
			while (is_id_char(*b))
				b++;
			i = param_index(m, start, (size_t)(b - start));
			if (i < 0) {
				sb_add(out, start, (size_t)(b - start));
			} else {
				look = ws(b);
				raw = pasted || (look[0] == '#' && look[1] == '#');
				if (raw) {
					sb_add(out, args[i], strlen(args[i]));
				} else {
					sb_init(&tmp);
					expand(args[i], &tmp, depth + 1, line);
					sb_add(out, tmp.data, tmp.len);
					free(tmp.data);
				}
			}
			pasted = 0;
		} else {
			sb_add(out, b++, 1);
			pasted = 0;
		}
	}
}

static void expand(const char *s, struct sb *out, int depth, int line)
{
	const char *start;
	const char *p;
	const char *end;
	struct macro *m;
	struct sb body;
	char *args[MAX_PARAMS];
	int count = 0;

	while (*s) {
		if (*s == '"' || *s == '\'') {
			start = s;
			s = skip_str(s);
			sb_add(out, start, (size_t)(s - start));
		} else if (is_id_start(*s)) {
			start = s;
			while (is_id_char(*s))
				s++;
			if (s - start == 8
			    && strncmp(start, "__LINE__", 8) == 0) {
				char num[16];

				sprintf(num, "%d", line);
				sb_add(out, num, strlen(num));
				continue;
			}
			if (s - start == 8
			    && strncmp(start, "__FILE__", 8) == 0) {
				stringize(out, pp_file);
				continue;
			}
			m = find_macro(start, (size_t)(s - start));
			if (!m || m->busy || depth >= MAX_EXPAND) {
				sb_add(out, start, (size_t)(s - start));
				continue;
			}
			if (!m->is_func) {
				m->busy = 1;
				expand(m->value, out, depth + 1, line);
				m->busy = 0;
				continue;
			}
			p = ws(s);
			if (*p != '(') {
				sb_add(out, start, (size_t)(s - start));
				continue;
			}
			end = collect_args(p, args, &count, line);
			if (count == 1 && !args[0][0] && m->nparams == 0)
				count = 0;
			if (count != m->nparams)
				die_at(line, "macro '%s' expects %d arguments",
				       m->name, m->nparams);
			sb_init(&body);
			substitute(m, args, &body, depth, line);
			m->busy = 1;
			expand(body.data, out, depth + 1, line);
			m->busy = 0;
			free(body.data);
			s = end;
		} else {
			sb_add(out, s, 1);
			s++;
		}
	}
}

static char *strip(const char *src)
{
	struct sb b;
	const char *p;
	char quote;

	b.data = NULL;
	b.len = 0;
	b.cap = 0;
	sb_add(&b, "", 0);
	p = src;
	while (*p) {
		if (p[0] == '\\' && p[1] == '\n') {
			p += 2;
		} else if (p[0] == '/' && p[1] == '*') {
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/')) {
				if (*p == '\n')
					sb_add(&b, "\n", 1);
				p++;
			}
			if (*p)
				p += 2;
			sb_add(&b, " ", 1);
		} else if (*p == '"' || *p == '\'') {
			quote = *p;
			sb_add(&b, p++, 1);
			while (*p && *p != quote && *p != '\n') {
				if (*p == '\\' && p[1])
					sb_add(&b, p++, 1);
				sb_add(&b, p++, 1);
			}
			if (*p == quote)
				sb_add(&b, p++, 1);
		} else {
			sb_add(&b, p++, 1);
		}
	}
	return b.data;
}

static const char *skip_ws(const char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static size_t ident_len(const char *s)
{
	size_t n;

	n = 0;
	while (isalnum((unsigned char)s[n]) || s[n] == '_')
		n++;
	return n;
}

struct ev {
	const char *p;
	int line;
	int noeval;
};

static long ev_cond(struct ev *e);

static void ev_ws(struct ev *e)
{
	while (*e->p == ' ' || *e->p == '\t')
		e->p++;
}

static long ev_char(struct ev *e)
{
	long v;
	int n;

	e->p++;
	if (*e->p != '\\') {
		v = (signed char)*e->p;
		e->p++;
	} else {
		e->p++;
		switch (*e->p) {
		case 'n': v = 10; e->p++; break;
		case 't': v = 9; e->p++; break;
		case 'r': v = 13; e->p++; break;
		case 'a': v = 7; e->p++; break;
		case 'b': v = 8; e->p++; break;
		case 'f': v = 12; e->p++; break;
		case 'v': v = 11; e->p++; break;
		case 'x':
			e->p++;
			v = 0;
			while (isxdigit((unsigned char)*e->p)) {
				v = v * 16 + (isdigit((unsigned char)*e->p)
				    ? *e->p - '0'
				    : tolower((unsigned char)*e->p) - 'a' + 10);
				e->p++;
			}
			break;
		default:
			if (*e->p >= '0' && *e->p <= '7') {
				v = 0;
				for (n = 0; n < 3 && *e->p >= '0'
				    && *e->p <= '7'; n++)
					v = v * 8 + (*e->p++ - '0');
			} else {
				v = (signed char)*e->p;
				e->p++;
			}
			break;
		}
	}
	if (*e->p != '\'')
		die_at(e->line, "bad character constant in #if");
	e->p++;
	return v;
}

static long ev_number(struct ev *e)
{
	unsigned long v;
	int base;
	int d;

	v = 0;
	base = 10;
	if (e->p[0] == '0' && (e->p[1] == 'x' || e->p[1] == 'X')) {
		base = 16;
		e->p += 2;
	} else if (e->p[0] == '0') {
		base = 8;
	}
	for (;;) {
		if (isdigit((unsigned char)*e->p))
			d = *e->p - '0';
		else if (base == 16 && isxdigit((unsigned char)*e->p))
			d = tolower((unsigned char)*e->p) - 'a' + 10;
		else
			break;
		if (d >= base)
			die_at(e->line, "bad number in #if");
		v = v * (unsigned long)base + (unsigned long)d;
		e->p++;
	}
	while (*e->p == 'u' || *e->p == 'U' || *e->p == 'l' || *e->p == 'L')
		e->p++;
	return (long)v;
}

static long ev_unary(struct ev *e)
{
	long v;
	int depth;

	ev_ws(e);
	switch (*e->p) {
	case '!':
		e->p++;
		return !ev_unary(e);
	case '~':
		e->p++;
		return ~ev_unary(e);
	case '-':
		e->p++;
		return -ev_unary(e);
	case '+':
		e->p++;
		return ev_unary(e);
	case '(':
		e->p++;
		v = ev_cond(e);
		ev_ws(e);
		if (*e->p != ')')
			die_at(e->line, "missing ')' in #if");
		e->p++;
		return v;
	case '\'':
		return ev_char(e);
	default:
		break;
	}
	if (*e->p == 'L' && e->p[1] == '\'') {
		e->p++;
		return ev_char(e);
	}
	if (isdigit((unsigned char)*e->p))
		return ev_number(e);
	if (is_id_start(*e->p)) {
		while (is_id_char(*e->p))
			e->p++;
		ev_ws(e);
		if (*e->p == '(') {
			depth = 0;
			do {
				if (*e->p == '(')
					depth++;
				else if (*e->p == ')')
					depth--;
				e->p++;
			} while (*e->p && depth > 0);
		}
		return 0;
	}
	die_at(e->line, "bad expression in #if");
	return 0;
}

enum { EV_MUL = 1, EV_DIV, EV_MOD, EV_ADD, EV_SUB, EV_SHL, EV_SHR, EV_LT,
       EV_GT, EV_LE, EV_GE, EV_EQ, EV_NE, EV_AND, EV_XOR, EV_OR, EV_LAND,
       EV_LOR };

static int ev_op(const char *p, int *len, int *op)
{
	*len = 2;
	if (p[0] == '<' && p[1] == '<') { *op = EV_SHL; return 8; }
	if (p[0] == '>' && p[1] == '>') { *op = EV_SHR; return 8; }
	if (p[0] == '<' && p[1] == '=') { *op = EV_LE; return 7; }
	if (p[0] == '>' && p[1] == '=') { *op = EV_GE; return 7; }
	if (p[0] == '=' && p[1] == '=') { *op = EV_EQ; return 6; }
	if (p[0] == '!' && p[1] == '=') { *op = EV_NE; return 6; }
	if (p[0] == '&' && p[1] == '&') { *op = EV_LAND; return 2; }
	if (p[0] == '|' && p[1] == '|') { *op = EV_LOR; return 1; }
	*len = 1;
	switch (p[0]) {
	case '*': *op = EV_MUL; return 10;
	case '/': *op = EV_DIV; return 10;
	case '%': *op = EV_MOD; return 10;
	case '+': *op = EV_ADD; return 9;
	case '-': *op = EV_SUB; return 9;
	case '<': *op = EV_LT; return 7;
	case '>': *op = EV_GT; return 7;
	case '&': *op = EV_AND; return 5;
	case '^': *op = EV_XOR; return 4;
	case '|': *op = EV_OR; return 3;
	default: break;
	}
	return 0;
}

static long ev_binary(struct ev *e, int min_prec)
{
	long l;
	long r;
	int prec;
	int len;
	int op;

	l = ev_unary(e);
	for (;;) {
		ev_ws(e);
		prec = ev_op(e->p, &len, &op);
		if (prec == 0 || prec < min_prec)
			return l;
		e->p += len;
		if (op == EV_LAND || op == EV_LOR) {
			int skip;

			skip = op == EV_LAND ? l == 0 : l != 0;
			if (skip)
				e->noeval++;
			r = ev_binary(e, prec + 1);
			if (skip)
				e->noeval--;
			l = op == EV_LAND ? (l && r) : (l || r);
			continue;
		}
		r = ev_binary(e, prec + 1);
		switch (op) {
		case EV_MUL: l = l * r; break;
		case EV_DIV:
		case EV_MOD:
			if (r == 0) {
				if (!e->noeval)
					die_at(e->line,
					       "division by zero in #if");
				l = 0;
			} else {
				l = op == EV_DIV ? l / r : l % r;
			}
			break;
		case EV_ADD: l = l + r; break;
		case EV_SUB: l = l - r; break;
		case EV_SHL: l = l << r; break;
		case EV_SHR: l = l >> r; break;
		case EV_LT: l = l < r; break;
		case EV_GT: l = l > r; break;
		case EV_LE: l = l <= r; break;
		case EV_GE: l = l >= r; break;
		case EV_EQ: l = l == r; break;
		case EV_NE: l = l != r; break;
		case EV_AND: l = l & r; break;
		case EV_XOR: l = l ^ r; break;
		default: l = l | r; break;
		}
	}
}

static long ev_cond(struct ev *e)
{
	long c;
	long a;
	long b;

	c = ev_binary(e, 1);
	ev_ws(e);
	if (*e->p != '?')
		return c;
	e->p++;
	if (c == 0)
		e->noeval++;
	a = ev_cond(e);
	if (c == 0)
		e->noeval--;
	ev_ws(e);
	if (*e->p != ':')
		die_at(e->line, "missing ':' in #if");
	e->p++;
	if (c != 0)
		e->noeval++;
	b = ev_cond(e);
	if (c != 0)
		e->noeval--;
	return c ? a : b;
}

static int eval_if(const char *s, int line)
{
	struct sb b;
	struct sb x;
	struct ev e;
	const char *p;
	size_t n;
	int paren;
	long v;

	sb_init(&b);
	p = skip_ws(s);
	while (*p) {
		if (strncmp(p, "defined", 7) == 0 && !isalnum((unsigned char)p[7])
		    && p[7] != '_') {
			p = skip_ws(p + 7);
			paren = *p == '(';
			if (paren)
				p = skip_ws(p + 1);
			n = ident_len(p);
			sb_add(&b, find_macro(p, n) ? " 1 " : " 0 ", 3);
			p = skip_ws(p + n);
			if (paren && *p == ')')
				p++;
		} else if (is_id_start(*p)) {
			n = ident_len(p);
			sb_add(&b, p, n);
			p += n;
		} else {
			sb_add(&b, p++, 1);
		}
	}
	sb_init(&x);
	expand(b.data, &x, 0, line);
	e.p = x.data;
	e.line = line;
	e.noeval = 0;
	v = ev_cond(&e);
	ev_ws(&e);
	if (*e.p)
		die_at(line, "extra tokens in #if expression");
	free(b.data);
	free(x.data);
	return v != 0;
}

static void define_func(char *name, const char *a, int line)
{
	struct macro *m;
	size_t n;

	pp_define(name, "");
	m = find_macro(name, strlen(name));
	m->is_func = 1;
	a = skip_ws(a);
	while (*a && *a != ')') {
		n = ident_len(a);
		if (!n || m->nparams == MAX_PARAMS)
			die_at(line, "bad macro parameter list");
		m->params[m->nparams] = xalloc(n + 1);
		memcpy(m->params[m->nparams++], a, n);
		a = skip_ws(a + n);
		if (*a == ',')
			a = skip_ws(a + 1);
	}
	if (*a != ')')
		die_at(line, "bad macro parameter list");
	m->value = xstrdup(skip_ws(a + 1));
}

static char *find_include(const char *dir, const char *name, int angle,
			  char **found)
{
	char *path;
	FILE *f;
	int i;

	for (i = angle ? 0 : -1; i < ninc; i++) {
		if (i < 0)
			path = xalloc(strlen(dir) + strlen(name) + 2);
		else
			path = xalloc(strlen(inc_dirs[i]) + strlen(name) + 2);
		if (i < 0) {
			strcpy(path, dir);
			strcat(path, name);
		} else {
			sprintf(path, "%s/%s", inc_dirs[i], name);
		}
		f = fopen(path, "rb");
		if (f) {
			fclose(f);
			*found = path;
			return read_file(path);
		}
	}
	return NULL;
}

static char *dir_of(const char *path)
{
	char *d;
	char *slash;

	d = xstrdup(path);
	slash = strrchr(d, '/');
	if (slash)
		slash[1] = '\0';
	else
		d[0] = '\0';
	return d;
}

static int call_open(const char *s)
{
	struct macro *m;
	const char *start;
	const char *p;
	int depth;

	while (*s) {
		if (*s == '"' || *s == '\'') {
			s = skip_str(s);
			continue;
		}
		if (!is_id_start(*s)) {
			s++;
			continue;
		}
		start = s;
		while (is_id_char(*s))
			s++;
		m = find_macro(start, (size_t)(s - start));
		if (!m || !m->is_func)
			continue;
		p = ws(s);
		if (*p != '(')
			continue;
		depth = 0;
		while (*p) {
			if (*p == '"' || *p == '\'') {
				p = skip_str(p);
				continue;
			}
			if (*p == '(')
				depth++;
			else if (*p == ')' && --depth == 0)
				break;
			p++;
		}
		if (depth > 0)
			return 1;
		s = *p ? p + 1 : p;
	}
	return 0;
}

static void line_marker(struct sb *out, int line, const char *path)
{
	char num[24];

	sb_add(out, "# ", 2);
	sprintf(num, "%d", line);
	sb_add(out, num, strlen(num));
	sb_add(out, " \"", 2);
	while (*path) {
		if (*path == '"' || *path == '\\')
			sb_add(out, "\\", 1);
		sb_add(out, path, 1);
		path++;
	}
	sb_add(out, "\"\n", 2);
}

char *preprocess(const char *src, const char *path)
{
	struct sb out;
	struct sb line;
	struct sb tmp;
	char *text;
	const char *p;
	const char *q;
	const char *eol;
	char *incpath = NULL;
	char *incsrc;
	char *incout;
	char *incname;
	char *dir;
	char quote;
	size_t n;
	int active[MAX_COND];
	int taken[MAX_COND];
	int top;
	int lineno;
	int skipping;
	int extra;
	const char *saved_file;
	const char *saved_cur;

	out.data = NULL;
	out.len = 0;
	out.cap = 0;
	sb_add(&out, "", 0);
	if (inc_depth == 0) {
		pp_define("__STDC__", "1");
		pp_define(tg->bits == 64 ? "__x86_64__" : "__i386__", "1");
	}
	saved_file = pp_file;
	saved_cur = cur_file;
	pp_file = path;
	cur_file = path;
	text = strip(src);
	dir = dir_of(path);
	top = 0;
	active[0] = 1;
	taken[0] = 1;
	lineno = 0;
	p = text;
	while (*p) {
		lineno++;
		eol = strchr(p, '\n');
		if (!eol)
			eol = p + strlen(p);
		line.data = NULL;
		line.len = 0;
		line.cap = 0;
		sb_add(&line, p, (size_t)(eol - p));
		q = skip_ws(line.data);
		skipping = !active[top];
		if (*q == '#') {
			q = skip_ws(q + 1);
			n = ident_len(q);
			if ((n == 5 && strncmp(q, "ifdef", 5) == 0)
			    || (n == 6 && strncmp(q, "ifndef", 6) == 0)) {
				const char *a;
				int found;

				a = skip_ws(q + n);
				found = find_macro(a, ident_len(a)) != NULL;
				if (n == 6)
					found = !found;
				if (top + 1 >= MAX_COND)
					die_at(lineno, "conditionals too deep");
				top++;
				active[top] = active[top - 1] && found;
				taken[top] = active[top];
			} else if (n == 2 && strncmp(q, "if", 2) == 0) {
				if (top + 1 >= MAX_COND)
					die_at(lineno, "conditionals too deep");
				top++;
				active[top] = active[top - 1]
				    && eval_if(q + 2, lineno);
				taken[top] = active[top];
			} else if (n == 4 && strncmp(q, "elif", 4) == 0) {
				if (top == 0)
					die_at(lineno, "#elif without #if");
				active[top] = active[top - 1] && !taken[top]
				    && eval_if(q + 4, lineno);
				taken[top] = taken[top] || active[top];
			} else if (n == 4 && strncmp(q, "else", 4) == 0) {
				if (top == 0)
					die_at(lineno, "#else without #if");
				active[top] = active[top - 1] && !taken[top];
				taken[top] = 1;
			} else if (n == 5 && strncmp(q, "endif", 5) == 0) {
				if (top == 0)
					die_at(lineno, "#endif without #if");
				top--;
			} else if (!skipping && n == 6
				   && strncmp(q, "define", 6) == 0) {
				const char *a;
				char *name;
				size_t nl;

				a = skip_ws(q + 6);
				nl = ident_len(a);
				if (!nl)
					die_at(lineno, "macro name missing");
				name = xalloc(nl + 1);
				memcpy(name, a, nl);
				if (a[nl] == '(')
					define_func(name, a + nl + 1, lineno);
				else
					pp_define(name, skip_ws(a + nl));
			} else if (!skipping && n == 5
				   && strncmp(q, "undef", 5) == 0) {
				const char *a;

				a = skip_ws(q + 5);
				pp_undef(a, ident_len(a));
			} else if (!skipping && n == 7
				   && strncmp(q, "include", 7) == 0) {
				q = skip_ws(q + 7);
				if (*q != '"' && *q != '<') {
					sb_init(&tmp);
					expand(q, &tmp, 0, lineno);
					q = skip_ws(tmp.data);
				}
				if (*q != '"' && *q != '<')
					die_at(lineno, "bad #include");
				quote = *q++ == '<' ? '>' : '"';
				n = 0;
				while (q[n] && q[n] != quote)
					n++;
				incname = xalloc(n + 1);
				memcpy(incname, q, n);
				if (inc_depth >= MAX_INCLUDE)
					die_at(lineno, "include nested too deep");
				incsrc = find_include(dir, incname, quote == '>', &incpath);
				if (!incsrc)
					die_at(lineno, "cannot find '%s'", incname);
				inc_depth++;
				line_marker(&out, 1, incpath);
				incout = preprocess(incsrc, incpath);
				inc_depth--;
				free(incsrc);
				sb_add(&out, incout, strlen(incout));
				line_marker(&out, lineno, path);
			} else if (!skipping && n == 5
				   && strncmp(q, "error", 5) == 0) {
				die_at(lineno, "#error %s", skip_ws(q + 5));
			}
		} else if (!skipping) {
			extra = 0;
			while (*eol && call_open(line.data)) {
				const char *nx;

				nx = eol + 1;
				eol = strchr(nx, '\n');
				if (!eol)
					eol = nx + strlen(nx);
				sb_add(&line, " ", 1);
				sb_add(&line, nx, (size_t)(eol - nx));
				extra++;
			}
			lineno += extra;
			tmp.data = NULL;
			tmp.len = 0;
			tmp.cap = 0;
			sb_add(&tmp, "", 0);
			expand(line.data, &tmp, 0, lineno);
			sb_add(&out, tmp.data, tmp.len);
			free(tmp.data);
			while (extra-- > 0)
				sb_add(&out, "\n", 1);
		}
		sb_add(&out, "\n", 1);
		free(line.data);
		p = *eol ? eol + 1 : eol;
	}
	if (top != 0)
		die_at(lineno, "unterminated #if");
	pp_file = saved_file;
	cur_file = saved_cur;
	return out.data;
}
