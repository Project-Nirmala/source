#include "cc.h"

enum { S_LOCAL, S_GLOBAL, S_TYPEDEF, S_ENUM, S_FUNC };

struct sym {
	char *name;
	int kind;
	struct type *ty;
	int offset;
	int nparams;
	long ival;
	struct sym *next;
	char *label;
	int is_extern;
	int has_init;
	struct node *gnode;
};

struct tag {
	char *name;
	struct type *ty;
	struct tag *next;
};

static struct token *cur;

static struct tag *tags;
static struct node *cur_switch;

static struct sym *syms;
static struct sym *funcs;
static struct sym *scope_start;

struct node *g_globals;
struct node *g_strings;

static int frame;
static int brk_depth;
static int cont_depth;

static int nstatic;
static int cur_static;
static char *cur_fn;
static struct type *ty_valist;

static struct node *new_node(enum node_kind kind, int line)
{
	struct node *n;

	n = xalloc(sizeof *n);
	n->kind = kind;
	n->line = line;
	return n;
}

static void expect(enum tok_kind kind, const char *what)
{
	if (cur->kind != kind)
		die_at(cur->line, "expected %s", what);
	cur++;
}

static int accept(enum tok_kind kind)
{
	if (cur->kind != kind)
		return 0;
	cur++;
	return 1;
}

static void advance(void)
{
	cur++;
}

static struct sym *lookup(const char *name)
{
	struct sym *s;

	for (s = syms; s; s = s->next)
		if (strcmp(s->name, name) == 0)
			return s;
	return NULL;
}

static struct sym *find_func(const char *name)
{
	struct sym *s;

	for (s = funcs; s; s = s->next)
		if (strcmp(s->name, name) == 0)
			return s;
	return NULL;
}

static void add_func(char *name, struct type *ret, int nparams)
{
	struct sym *s;

	s = find_func(name);
	if (!s) {
		s = xalloc(sizeof *s);
		s->name = name;
		s->kind = S_FUNC;
		s->next = funcs;
		funcs = s;
	}
	s->ty = ret;
	s->nparams = nparams;
}

static struct sym *add_sym(char *name, int kind, int line, int offset,
			   struct type *ty)
{
	struct sym *s;

	for (s = syms; s != scope_start; s = s->next)
		if (strcmp(s->name, name) == 0)
			die_at(line, "redefinition of '%s'", name);
	s = xalloc(sizeof *s);
	s->name = name;
	s->kind = kind;
	s->offset = offset;
	s->ty = ty;
	s->next = syms;
	syms = s;
	return s;
}

static int same_type(struct type *a, struct type *b)
{
	if (a == b)
		return 1;
	if (a->kind != b->kind || a->size != b->size
	    || a->is_unsigned != b->is_unsigned)
		return 0;
	if (a->kind == TY_PTR || a->kind == TY_ARRAY)
		return same_type(a->base, b->base);
	return a->kind != TY_STRUCT;
}

static void add_typedef(char *name, int line, struct type *ty)
{
	struct sym *s;

	for (s = syms; s != scope_start; s = s->next)
		if (strcmp(s->name, name) == 0) {
			if (s->kind == S_TYPEDEF && same_type(s->ty, ty))
				return;
			die_at(line, "redefinition of '%s'", name);
		}
	add_sym(name, S_TYPEDEF, line, 0, ty);
}

static int alloc_local(struct type *ty)
{
	frame = align_to(frame + ty->size, ty->align);
	return -frame;
}

static struct type *type_name(void);
static struct node *expr(void);
static struct node *assign(void);
static struct node *binary(int min_prec);
static struct node *cast_expr(void);
static struct node *unary(void);
static struct node *stmt(void);
static struct node *block(void);
static struct node *local_decl(void);
static struct type *specifiers(int *storage);
static struct type *declarator(struct type *base, char **name);
static struct node *cond_expr(void);
static struct node *cond(void);

static void global_var(char *name, struct type *ty, int storage, int line);
static struct param *parse_params(int *nparams, int *variadic);
static struct type *decl_suffix(struct type *base);
static void skip_attrs(void);
static struct node *elem_lvalue(struct node *arr, long i, int line);
static void declare_func(char *name, struct type *ft, int line);
static struct node *var_node(struct sym *s, int line);
static struct node *mk_opassign(int op, struct node *lhs, struct node *rhs,
				int line);
static struct node *assign_stmt(struct node *lhs, struct node *rhs, int line);

static int cur_variadic;
static int cur_nparams;
static int cur_va_off;

static struct node *num(long v)
{
	struct node *n;

	n = new_node(N_NUM, cur->line);
	n->ival = v;
	n->ty = (v > 2147483647L || v < -2147483647L - 1) ? ty_long : ty_int;
	return n;
}

static struct node *num_lit(struct token *t)
{
	struct node *n;
	unsigned long v;
	int f;

	f = t->len;
	v = (unsigned long)t->ival;
	n = new_node(N_NUM, t->line);
	n->ival = t->ival;
	if (f & NUM_UNSIGNED)
		n->ty = ((f & NUM_LONG) || v > 0xFFFFFFFFUL) ? ty_ulong : ty_uint;
	else if (v > 0x7FFFFFFFFFFFFFFFUL)
		n->ty = ty_ulong;
	else if (f & NUM_LONG)
		n->ty = ty_long;
	else if (v > 0xFFFFFFFFUL)
		n->ty = ty_long;
	else if (v > 0x7FFFFFFFUL)
		n->ty = (f & NUM_NONDEC) ? ty_uint : ty_long;
	else
		n->ty = ty_int;
	return n;
}

static long trunc_to(struct type *t, long v)
{
	if (!t || !is_integer(t))
		return v;
	switch (t->size) {
	case 1:
		return t->is_unsigned ? (long)(unsigned char)v
		    : (long)(signed char)v;
	case 2:
		return t->is_unsigned ? (long)(unsigned short)v
		    : (long)(short)v;
	case 4:
		return t->is_unsigned ? (long)(unsigned int)v : (long)(int)v;
	default:
		return v;
	}
}

static long const_eval(struct node *n);

static long const_addr(struct node *n)
{
	switch (n->kind) {
	case N_MEMBER:
		return const_addr(n->lhs) + n->offset;
	case N_DEREF:
		return const_eval(n->lhs);
	default:
		break;
	}
	die_at(n->line, "constant expression required");
	return 0;
}

static long const_eval(struct node *n)
{
	unsigned long ul;
	unsigned long ur;
	long l;
	long r;
	int u;

	switch (n->kind) {
	case N_NUM:
		return n->ival;
	case N_NEG:
		return trunc_to(n->ty, -const_eval(n->lhs));
	case N_NOT:
		return !const_eval(n->lhs);
	case N_CAST:
		return trunc_to(n->ty, const_eval(n->lhs));
	case N_BNOT:
		return trunc_to(n->ty, ~const_eval(n->lhs));
	case N_ADDR:
		return const_addr(n->lhs);
	case N_COND:
		return const_eval(n->lhs) ? const_eval(n->rhs)
		    : const_eval(n->body);
	case N_AND:
		return const_eval(n->lhs) && const_eval(n->rhs);
	case N_OR:
		return const_eval(n->lhs) || const_eval(n->rhs);
	case N_BINOP:
		l = const_eval(n->lhs);
		r = const_eval(n->rhs);
		u = (int)n->ival;
		ul = (unsigned long)l;
		ur = (unsigned long)r;
		switch (n->op) {
		case T_PLUS: return trunc_to(n->ty, l + r);
		case T_MINUS: return trunc_to(n->ty, l - r);
		case T_STAR: return trunc_to(n->ty, l * r);
		case T_SLASH:
			if (!r)
				die_at(n->line, "division by zero");
			return trunc_to(n->ty, u ? (long)(ul / ur) : l / r);
		case T_PERCENT:
			if (!r)
				die_at(n->line, "division by zero");
			return trunc_to(n->ty, u ? (long)(ul % ur) : l % r);
		case T_AMP: return l & r;
		case T_PIPE: return l | r;
		case T_CARET: return l ^ r;
		case T_SHL: return trunc_to(n->ty, l << r);
		case T_SHR:
			return u ? (long)(ul >> r) : l >> r;
		case T_LT: return u ? ul < ur : l < r;
		case T_GT: return u ? ul > ur : l > r;
		case T_LE: return u ? ul <= ur : l <= r;
		case T_GE: return u ? ul >= ur : l >= r;
		case T_EQ: return l == r;
		case T_NE: return l != r;
		default: break;
		}
		break;
	default:
		break;
	}
	die_at(n->line, "constant expression required");
	return 0;
}

static int token_is_type(struct token *t)
{
	struct sym *s;

	switch (t->kind) {
	case T_KW_INT:
	case T_KW_CHAR:
	case T_KW_LONG:
	case T_KW_VOID:
	case T_KW_CONST:
	case T_KW_STRUCT:
	case T_KW_UNION:
	case T_KW_ENUM:
	case T_KW_TYPEDEF:
	case T_KW_STATIC:
	case T_KW_UNSIGNED:
	case T_KW_SIGNED:
	case T_KW_SHORT:
	case T_KW_EXTERN:
	case T_KW_FLOAT:
	case T_KW_DOUBLE:
	case T_KW_REGISTER:
	case T_KW_AUTO:
	case T_KW_INLINE:
	case T_KW_RESTRICT:
	case T_KW_ATTRIBUTE:
	case T_KW_EXTENSION:
		return 1;
	case T_IDENT:
		s = lookup(t->text);
		return s && s->kind == S_TYPEDEF;
	default:
		return 0;
	}
}

static struct type *find_tag(const char *name)
{
	struct tag *t;

	for (t = tags; t; t = t->next)
		if (strcmp(t->name, name) == 0)
			return t->ty;
	return NULL;
}

static int last_spec_anon;

static void place_bits(struct type *t, struct type *mt, long width, int named,
		       long *bitpos, int *byteoff, int *shift)
{
	long unit;
	long start;

	unit = (long)mt->size * 8;
	*byteoff = 0;
	*shift = 0;
	if (t->is_union) {
		if (mt->size > t->size)
			t->size = mt->size;
	} else if (width == 0) {
		*bitpos = (*bitpos + unit - 1) / unit * unit;
	} else {
		start = *bitpos / unit * unit;
		if (*bitpos + width > start + unit) {
			start += unit;
			*bitpos = start;
		}
		*byteoff = (int)(start / 8);
		*shift = (int)(*bitpos - start);
		*bitpos += width;
		if ((*bitpos + 7) / 8 > t->size)
			t->size = (int)((*bitpos + 7) / 8);
	}
	if (named && mt->align > t->align)
		t->align = mt->align;
}

static int place_member(struct type *t, struct type *mt, long *bitpos)
{
	int off;

	if (t->is_union) {
		if (mt->size > t->size)
			t->size = mt->size;
		off = 0;
	} else {
		off = align_to((int)((*bitpos + 7) / 8), mt->align);
		*bitpos = ((long)off + mt->size) * 8;
		t->size = (int)((*bitpos + 7) / 8);
	}
	if (mt->align > t->align)
		t->align = mt->align;
	return off;
}

static struct type *struct_spec(void)
{
	struct type *t;
	struct tag *tg_;
	struct member *m;
	struct member *tail;
	struct type *base;
	struct type *mt;
	char *name;
	char *mname;
	int is_union;
	int anon;
	int line;
	long bitpos;
	long width;
	int byteoff;
	int shift;

	line = cur->line;
	is_union = cur->kind == T_KW_UNION;
	advance();
	skip_attrs();
	name = NULL;
	if (cur->kind == T_IDENT) {
		name = cur->text;
		advance();
	}
	t = name ? find_tag(name) : NULL;
	if (!t) {
		t = new_struct(is_union);
		if (name) {
			tg_ = xalloc(sizeof *tg_);
			tg_->name = name;
			tg_->ty = t;
			tg_->next = tags;
			tags = tg_;
		}
	}
	if (cur->kind != T_LBRACE) {
		if (!name)
			die_at(cur->line, "expected struct body");
		last_spec_anon = 0;
		return t;
	}
	if (t->members || t->size)
		die_at(line, "redefinition of '%s %s'",
		       is_union ? "union" : "struct", name);
	advance();
	tail = NULL;
	bitpos = 0;
	while (cur->kind != T_RBRACE) {
		if (cur->kind == T_EOF)
			die_at(cur->line, "expected '}'");
		last_spec_anon = 0;
		base = specifiers(NULL);
		if (!base)
			die_at(cur->line, "expected member type");
		anon = last_spec_anon;
		if (cur->kind == T_SEMI && !anon) {
			advance();
			continue;
		}
		for (;;) {
			width = -1;
			mname = NULL;
			mt = base;
			if (cur->kind != T_COLON && cur->kind != T_SEMI)
				mt = declarator(base, &mname);
			if (accept(T_COLON)) {
				width = const_eval(cond());
				if (!is_integer(mt) || width < 0
				    || width > (long)mt->size * 8)
					die_at(cur->line, "bad bit-field");
			}
			if (width >= 0) {
				place_bits(t, mt, width, mname != NULL,
					   &bitpos, &byteoff, &shift);
				if (!mname || width == 0) {
					if (!accept(T_COMMA))
						break;
					continue;
				}
			} else if (!mname && !(anon && (mt->kind == TY_STRUCT))) {
				die_at(cur->line, "bad member");
			} else if (mt->kind == TY_ARRAY && mt->len < 0) {
				if (cur[0].kind != T_SEMI)
					die_at(cur->line, "bad member");
			} else if (mt->size == 0 || mt->kind == TY_FUNC) {
				die_at(cur->line, "bad member");
			}
			m = xalloc(sizeof *m);
			m->name = mname;
			m->ty = mt;
			if (width >= 0) {
				m->offset = byteoff;
				m->bit_width = (int)width;
				m->bit_shift = shift;
			} else {
				m->offset = place_member(t, mt, &bitpos);
			}
			if (tail)
				tail->next = m;
			else
				t->members = m;
			tail = m;
			if (!accept(T_COMMA))
				break;
		}
		expect(T_SEMI, "';'");
	}
	advance();
	skip_attrs();
	t->size = align_to(t->size, t->align);
	last_spec_anon = name == NULL;
	return t;
}

static struct type *enum_spec(void)
{
	struct sym *es;
	long val;
	char *name;
	int line;

	advance();
	if (cur->kind == T_IDENT)
		advance();
	if (cur->kind != T_LBRACE)
		return ty_int;
	advance();
	val = 0;
	while (cur->kind != T_RBRACE) {
		if (cur->kind != T_IDENT)
			die_at(cur->line, "expected enumerator");
		name = cur->text;
		line = cur->line;
		advance();
		if (accept(T_ASSIGN))
			val = const_eval(cond());
		es = add_sym(name, S_ENUM, line, 0, ty_int);
		es->ival = val;
		val++;
		if (!accept(T_COMMA))
			break;
	}
	expect(T_RBRACE, "'}'");
	return ty_int;
}

static struct type *finish_type(struct type *t, int uns, int sh, int lng,
				int dbl)
{
    if (dbl) {
        if (t != ty_double || uns || sh || lng > 1)
            die_at(cur->line, "invalid type specifier combination");
        return lng ? ty_ldouble : ty_double;
    }
    if (lng >= 2 && tg->bits == 32)
        die_at(cur->line, "'long long' is not supported with -m32");
    if (!t && (uns || sh))
        t = ty_int;
    if (!t)
        return NULL;
    if (sh && t->kind == TY_INT)
        return uns ? ty_ushort : ty_short;
    if (!uns)
        return t;
    if (t->kind == TY_CHAR)
        return ty_uchar;
    if (t->kind == TY_INT)
        return ty_uint;
    if (t->kind == TY_LONG)
        return ty_ulong;
    die_at(cur->line, "invalid use of unsigned");
    return t;
}

static void skip_attrs(void)
{
	int depth;

	for (;;) {
		if (cur->kind == T_KW_EXTENSION || cur->kind == T_KW_RESTRICT) {
			advance();
		} else if (cur->kind == T_KW_ATTRIBUTE
			   || cur->kind == T_KW_ASM) {
			advance();
			if (cur->kind != T_LPAREN)
				continue;
			depth = 0;
			do {
				if (cur->kind == T_LPAREN)
					depth++;
				else if (cur->kind == T_RPAREN)
					depth--;
				else if (cur->kind == T_EOF)
					die_at(cur->line, "unterminated attribute");
				advance();
			} while (depth > 0);
		} else {
			return;
		}
	}
}

static struct type *specifiers(int *storage)
{
    struct type *t;
    struct sym *s;
    int dummy;
    int uns;
    int sh;
    int lng;
    int dbl;

    if (!storage) {
        dummy = 0;
        storage = &dummy;
    }
    t = NULL;
    uns = 0;
    sh = 0;
    lng = 0;
    dbl = 0;
    for (;;) {
        switch (cur->kind) {
        case T_KW_TYPEDEF: *storage |= 1; advance(); continue;
        case T_KW_STATIC: *storage |= 2; advance(); continue;
        case T_KW_EXTERN: *storage |= 4; advance(); continue;
        case T_KW_REGISTER:
        case T_KW_AUTO:
        case T_KW_INLINE:
        case T_KW_RESTRICT:
        case T_KW_EXTENSION:
        case T_KW_CONST: advance(); continue;
        case T_KW_ATTRIBUTE: skip_attrs(); continue;
        case T_KW_UNSIGNED: uns = 1; advance(); continue;
        case T_KW_SIGNED: advance(); continue;
        case T_KW_SHORT: sh = 1; advance(); continue;
        case T_KW_INT:
            if (!t)
                t = ty_int;
            advance();
            continue;
        case T_KW_CHAR: t = ty_char; advance(); continue;
        case T_KW_LONG:
            lng++;
            if (t != ty_double)
                t = ty_long;
            advance();
            continue;
        case T_KW_FLOAT: t = ty_float; advance(); continue;
        case T_KW_DOUBLE: t = ty_double; dbl = 1; advance(); continue;
        case T_KW_VOID: t = ty_void; advance(); continue;
        case T_KW_STRUCT:
        case T_KW_UNION:
            t = struct_spec();
            continue;
        case T_KW_ENUM:
            t = enum_spec();
            continue;
        case T_IDENT:
            if (!t && !uns && !sh) {
                s = lookup(cur->text);
                if (s && s->kind == S_TYPEDEF) {
                    t = s->ty;
                    advance();
                    continue;
                }
            }
            return finish_type(t, uns, sh, lng, dbl);
        default:
            return finish_type(t, uns, sh, lng, dbl);
        }
    }
}

static int is_grouping(struct token *t)
{
	struct sym *sy;

	if (t->kind == T_STAR || t->kind == T_LPAREN)
		return 1;
	if (t->kind == T_KW_ATTRIBUTE)
		return 1;
	if (t->kind != T_IDENT)
		return 0;
	sy = lookup(t->text);
	return !(sy && sy->kind == S_TYPEDEF);
}

static struct type *decl_suffix(struct type *base)
{
	struct type *inner;
	struct type *ft;
	struct param *pl;
	long len;
	int np;
	int va;

	if (cur->kind == T_LBRACKET) {
		advance();
		len = -1;
		if (cur->kind != T_RBRACKET) {
			len = const_eval(cond());
			if (len <= 0)
				die_at(cur->line, "array size must be positive");
		}
		expect(T_RBRACKET, "']'");
		inner = decl_suffix(base);
		return array_of(inner, len);
	}
	if (cur->kind == T_LPAREN) {
		pl = parse_params(&np, &va);
		ft = func_of(base);
		ft->params = pl;
		ft->nparams = np;
		ft->variadic = va;
		skip_attrs();
		return ft;
	}
	skip_attrs();
	return base;
}

static struct type *declarator(struct type *base, char **name)
{
	struct token *save;
	struct token *end;
	struct type *ty;
	int depth;

	skip_attrs();
	while (accept(T_STAR)) {
		base = ptr_to(base);
		while (cur->kind == T_KW_CONST || cur->kind == T_KW_RESTRICT
		       || cur->kind == T_KW_ATTRIBUTE
		       || cur->kind == T_KW_EXTENSION) {
			if (cur->kind == T_KW_CONST)
				advance();
			else
				skip_attrs();
		}
	}
	if (cur->kind == T_LPAREN && is_grouping(cur + 1)) {
		save = cur + 1;
		depth = 0;
		do {
			if (cur->kind == T_LPAREN)
				depth++;
			else if (cur->kind == T_RPAREN)
				depth--;
			else if (cur->kind == T_EOF)
				die_at(cur->line, "expected ')'");
			advance();
		} while (depth > 0);
		base = decl_suffix(base);
		end = cur;
		cur = save;
		ty = declarator(base, name);
		expect(T_RPAREN, "')'");
		cur = end;
		return ty;
	}
	*name = NULL;
	if (cur->kind == T_IDENT) {
		*name = cur->text;
		advance();
	}
	return decl_suffix(base);
}

static struct type *type_name(void)
{
	struct type *base;
	char *name;

	base = specifiers(NULL);
	if (!base)
		die_at(cur->line, "expected type");
	return declarator(base, &name);
}

static struct node *decay(struct node *n)
{
	struct node *a;

	if (n->ty->kind != TY_ARRAY)
		return n;
	a = new_node(N_ADDR, n->line);
	a->lhs = n;
	a->ty = ptr_to(n->ty->base);
	return a;
}

static int is_lvalue(struct node *n)
{
	return n->kind == N_VAR || n->kind == N_GVAR || n->kind == N_DEREF
	    || n->kind == N_MEMBER;
}

static struct node *mk_deref(struct node *p, int line)
{
	struct node *n;

	p = decay(p);
	if (!is_ptr(p->ty))
		die_at(line, "cannot dereference a non-pointer");
	if (p->ty->base->kind == TY_VOID)
		die_at(line, "cannot dereference 'void *'");
	n = new_node(N_DEREF, line);
	n->lhs = p;
	n->ty = p->ty->base;
	return n;
}

static struct node *scale(struct node *r, struct type *ptr)
{
	struct node *n;
	struct node *c;
	int size;

	size = ptr->base->size ? ptr->base->size : 1;
	if (size == 1)
		return r;
	if (r->kind == N_NUM) {
		c = new_node(N_NUM, r->line);
		c->ival = r->ival * size;
		c->ty = r->ty;
		return c;
	}
	n = new_node(N_BINOP, r->line);
	n->op = T_STAR;
	n->lhs = r;
	n->rhs = num(size);
	n->ty = ty_long;
	return n;
}

static struct node *mk_binop(int op, struct node *l, struct node *r, int line)
{
    struct node *n;
    struct node *d;
    struct node *t;

    l = decay(l);
    r = decay(r);
    n = new_node(N_BINOP, line);
    n->op = op;
    if (op == T_PLUS && is_integer(l->ty) && is_ptr(r->ty)) {
        t = l;
        l = r;
        r = t;
    }
    if ((op == T_PLUS || op == T_MINUS) && is_ptr(l->ty)
        && is_integer(r->ty)) {
        n->lhs = l;
        n->rhs = scale(r, l->ty);
        n->ty = l->ty;
        return n;
    }
    if (op == T_MINUS && is_ptr(l->ty) && is_ptr(r->ty)) {
        n->lhs = l;
        n->rhs = r;
        n->ty = ty_long;
        if (l->ty->base->size > 1) {
            d = new_node(N_BINOP, line);
            d->op = T_SLASH;
            d->lhs = n;
            d->rhs = num(l->ty->base->size);
            d->ty = ty_long;
            return d;
        }
        return n;
    }
    n->lhs = l;
    n->rhs = r;
    if (op == T_LT || op == T_GT || op == T_LE || op == T_GE
        || op == T_EQ || op == T_NE) {
        if (!is_scalar(l->ty) || !is_scalar(r->ty))
            die_at(line, "invalid operands to comparison");
        n->ty = ty_int;
        n->ival = (is_integer(l->ty) && is_integer(r->ty))
            ? common_type(l->ty, r->ty)->is_unsigned : 1;
        return n;
    }
    if (!is_integer(l->ty) || !is_integer(r->ty))
        die_at(line, "invalid operands to binary operator");
    n->ty = common_type(l->ty, r->ty);
    n->ival = n->ty->is_unsigned;
    if (gen_opt() >= 2 && l->kind == N_NUM && r->kind == N_NUM
        && !n->ty->is_unsigned && (n->ty->size == 4 || n->ty->size == 8)
        && (op == T_PLUS || op == T_MINUS || op == T_STAR || op == T_AMP
            || op == T_PIPE || op == T_CARET)) {
        n->ival = const_eval(n);
        if (n->ty->size == 4)
            n->ival = (long)(int)n->ival;
        n->kind = N_NUM;
        n->lhs = NULL;
        n->rhs = NULL;
    }
    return n;
}

static struct node *call(void)
{
	struct node *n;
	struct node *a;
	struct node head;
	struct node *tail;
	struct sym *f;
	long count;

	n = new_node(N_CALL, cur->line);
	n->name = cur->text;
	f = find_func(cur->text);
	if (f && f->ival)
		die_at(cur->line, "'%s' needs floating-point support, which is "
		       "not available", cur->text);
	n->ty = f ? f->ty : ty_int;
	cur += 2;
	head.next = NULL;
	tail = &head;
	count = 0;
	if (cur->kind != T_RPAREN) {
		for (;;) {
			a = decay(assign());
			if (!is_scalar(a->ty))
				die_at(a->line, "unsupported argument type");
			tail->next = a;
			tail = a;
			count++;
			if (!accept(T_COMMA))
				break;
		}
	}
	expect(T_RPAREN, "')'");
	if (f && f->nparams >= 0 && f->nparams != count)
		die_at(n->line, "'%s' expects %d arguments", n->name,
		       f->nparams);
	n->body = head.next;
	n->ival = count;
	return n;
}

static int nstr;
static struct node *str_tail;
static struct node *gl_tail;

static struct node *add_string(struct token *t)
{
	struct node *s;
	char *buf;
	int len;
	int total;

	total = t->len;
	buf = t->text;
	while (t[1].kind == T_STR) {
		t++;
		len = total + t->len;
		{
			char *nb;

			nb = xalloc((size_t)len + 1);
			memcpy(nb, buf, (size_t)total);
			memcpy(nb + total, t->text, (size_t)t->len);
			buf = nb;
		}
		total = len;
	}
	cur = t;
	s = new_node(N_STR, cur->line);
	s->text = buf;
	s->ival = total;
	s->name = xalloc(24);
	sprintf(s->name, ".LC%d", nstr++);
	s->ty = array_of(ty_char, total + 1);
	if (str_tail)
		str_tail->next = s;
	else
		g_strings = s;
	str_tail = s;
	return s;
}

static struct node *str_ref(struct node *s, int line)
{
	struct node *e;

	e = new_node(N_STR, line);
	e->name = s->name;
	e->ty = s->ty;
	return e;
}

static void add_global_node(struct node *n)
{
	if (gl_tail)
		gl_tail->next = n;
	else
		g_globals = n;
	gl_tail = n;
}

static struct node *cond_expr(void);

static struct node *init_head;
static struct node *init_tail; 

static void init_add(struct node *e, int off)
{
    e->next = NULL;
    e->offset = off;
    if (init_tail)
        init_tail->next = e;
    else
        init_head = e;
    init_tail = e;
}

static int is_symbol_addr(struct node *e)
{
    return e->kind == N_FUNCADDR
        || (e->kind == N_ADDR && (e->lhs->kind == N_STR
        || e->lhs->kind == N_GVAR));
}

static long init_global(struct type *ty, int off, int line)
{
    struct member *m;
    struct node *e;
    struct node *str;
    long i;

    if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR
        && cur->kind == T_STR) {
        str = add_string(cur);
        advance();
        for (i = 0; i <= str->ival; i++) {
            e = num(i == str->ival ? 0 : (signed char)str->text[i]);
            e->ty = ty_char;
            init_add(e, off + (int)i);
        }
        return str->ival + 1;
    }
    if (ty->kind == TY_ARRAY) {
        expect(T_LBRACE, "'{'");
        i = 0;
        while (cur->kind != T_RBRACE) {
            if (ty->len >= 0 && i >= ty->len)
                die_at(line, "too many initializers");
            init_global(ty->base, off + (int)i * ty->base->size, line);
            i++;
            if (!accept(T_COMMA))
                break;
        }
        expect(T_RBRACE, "'}'");
        return i;
    }
    if (ty->kind == TY_STRUCT) {
        expect(T_LBRACE, "'{'");
        for (m = ty->members; m && cur->kind != T_RBRACE; m = m->next) {
            init_global(m->ty, off + m->offset, line);
            if (!accept(T_COMMA))
                break;
            if (ty->is_union)
                break;
        }
        expect(T_RBRACE, "'}'");
        return 0;
    }
    e = decay(cond());
    if (is_symbol_addr(e)) {
        e = str_ref(e->kind == N_ADDR ? e->lhs : e, line);
    } else {
        e = num(const_eval(e));
        e->ty = ty;
    }
    init_add(e, off);
    return 0;
}

static void global_var(char *name, struct type *ty, int storage, int line)
{
    struct node *g;
    struct sym *old;
    struct sym *sy;
    long count;

    old = NULL;
    for (sy = syms; sy != scope_start; sy = sy->next)
        if (strcmp(sy->name, name) == 0) {
            old = sy;
            break;
        }
    if (old && old->kind != S_GLOBAL)
        die_at(line, "redefinition of '%s'", name);
    if (old) {
        if (ty->kind == TY_ARRAY && ty->len >= 0 && old->ty->kind == TY_ARRAY
            && old->ty->len < 0)
            old->ty = ty;
        if (storage & 4)
            return;
        if (cur->kind != T_ASSIGN && old->gnode)
            return;
        if (cur->kind == T_ASSIGN && old->has_init)
            die_at(line, "redefinition of '%s'", name);
    }
    if (storage & 4) {
        sy = add_sym(name, S_GLOBAL, line, 0, ty);
        sy->is_extern = 1;
        return;
    }
    g = new_node(N_DECL, line);
    g->name = name;
    g->label = (storage & 2) != 0;
    if (accept(T_ASSIGN)) {
        g->op = 1;
        init_head = NULL;
        init_tail = NULL;
        count = init_global(ty, 0, line);
        if (ty->kind == TY_ARRAY && ty->len < 0)
            ty = array_of(ty->base, count);
        g->init = init_head;
    } else if (ty->kind == TY_ARRAY && ty->len < 0) {
        die_at(line, "array size missing");
    }
    g->ty = ty;
    if (old && old->gnode) {
        old->gnode->op = g->op;
        old->gnode->init = g->init;
        old->gnode->ty = ty;
        old->ty = ty;
        old->has_init = g->op;
        return;
    }
    if (!old)
        old = add_sym(name, S_GLOBAL, line, 0, ty);
    old->ty = ty;
    old->is_extern = 0;
    old->has_init = g->op;
    old->gnode = g;
    add_global_node(g);
}

static struct node *member_lvalue(struct node *base, struct member *m,
                  int line)
{
    struct node *n;

    n = new_node(N_MEMBER, line);
    n->lhs = base;
    n->offset = m->offset;
    n->ty = m->ty;
    return n;
}

static struct node *local_struct_init(struct sym *s, int line)
{
    struct node *blk;
    struct node head;
    struct node *t;
    struct node *v;
    struct member *m;

    blk = new_node(N_BLOCK, line);
    if (cur->kind != T_LBRACE) {
        v = decay(assign());
        if (v->ty != s->ty)
            die_at(line, "incompatible struct initializer");
        blk->body = assign_stmt(var_node(s, line), v, line);
        return blk;
    }
    advance();
    head.next = NULL;
    t = &head;
    for (m = s->ty->members; m; m = m->next) {
        if (m->ty->kind == TY_ARRAY || m->ty->kind == TY_STRUCT)
            die_at(line, "nested initializers are not supported");
        if (cur->kind != T_RBRACE) {
            v = decay(assign());
            accept(T_COMMA);
        } else {
            v = num(0);
        }
        t->next = assign_stmt(member_lvalue(var_node(s, line), m, line),
                      v, line);
        t = t->next;
    }
    if (cur->kind != T_RBRACE)
        die_at(line, "too many initializers");
    advance();
    blk->body = head.next;
    return blk;
}

static struct node *indirect_call(struct node *callee, int line)
{
    struct node *n;
    struct node *a;
    struct node head;
    struct node *t;
    long count;

    if (callee->ty->kind == TY_FUNC && callee->kind == N_DEREF)
        callee = callee->lhs;
    callee = decay(callee);
    if (!is_ptr(callee->ty) || callee->ty->base->kind != TY_FUNC)
        die_at(line, "called object is not a function");
    advance();
    n = new_node(N_CALL, line);
    n->lhs = callee;
    n->ty = callee->ty->base->base;
    head.next = NULL;
    t = &head;
    count = 0;
    if (cur->kind != T_RPAREN) {
        for (;;) {
            a = decay(assign());
            t->next = a;
            t = a;
            count++;
            if (!accept(T_COMMA))
                break;
        }
    }
    expect(T_RPAREN, "')'");
    n->body = head.next;
    n->ival = count;
    return n;
}

static int is_builtin(const char *name)
{
	if (strncmp(name, "__builtin_", 10) == 0)
		name += 10;
	return strcmp(name, "va_start") == 0 || strcmp(name, "va_arg") == 0
	    || strcmp(name, "va_end") == 0 || strcmp(name, "va_copy") == 0;
}

static struct node *va_copy_node(struct node *dst, struct node *src, int line)
{
	struct node *n;
	struct node *c;
	struct node *a;
	long i;

	if (tg->bits == 32) {
		n = new_node(N_ASSIGN, line);
		n->lhs = dst;
		n->rhs = src;
		n->ty = dst->ty;
		return n;
	}
	n = NULL;
	for (i = 0; i < 3; i++) {
		a = new_node(N_ASSIGN, line);
		a->lhs = elem_lvalue(dst, i, line);
		a->rhs = elem_lvalue(src, i, line);
		a->ty = a->lhs->ty;
		if (!n) {
			n = a;
			continue;
		}
		c = new_node(N_COMMA, line);
		c->lhs = n;
		c->rhs = a;
		c->ty = a->ty;
		n = c;
	}
	return n;
}

static struct node *builtin_va(void)
{
	struct node *n;
	struct node *ap;
	struct node *last;
	struct node *t;
	struct type *ty;
	char *name;
	int line;

	line = cur->line;
	name = cur->text;
	if (strncmp(name, "__builtin_", 10) == 0)
		name += 10;
	advance();
	expect(T_LPAREN, "'('");
	if (strcmp(name, "va_copy") == 0) {
		ap = assign();
		expect(T_COMMA, "','");
		last = assign();
		expect(T_RPAREN, "')'");
		return va_copy_node(ap, last, line);
	}
	if (strcmp(name, "va_end") == 0) {
		assign();
		expect(T_RPAREN, "')'");
		return num(0);
	}
	ap = assign();
	expect(T_COMMA, "','");
	if (strcmp(name, "va_arg") == 0) {
		ty = type_name();
		expect(T_RPAREN, "')'");
		if (tg->bits == 64) {
			n = new_node(N_VAARG, line);
			n->lhs = decay(ap);
			n->ty = ty;
			return n;
		}
		t = mk_opassign(T_PLUS, ap, num(4), line);
		t = mk_binop(T_MINUS, t, num(4), line);
		n = new_node(N_CAST, line);
		n->lhs = t;
		n->ty = ptr_to(ty);
		return mk_deref(n, line);
	}
	last = assign();
	expect(T_RPAREN, "')'");
	if (!cur_variadic)
		die_at(line, "va_start in a function that is not variadic");
	if (tg->bits == 64) {
		n = new_node(N_VASTART, line);
		n->lhs = decay(ap);
		n->ival = 8 * (cur_nparams < 6 ? cur_nparams : 6);
		n->offset = 16 + 8 * (cur_nparams > 6 ? cur_nparams - 6 : 0);
		n->label = cur_va_off;
		n->ty = ty_int;
		return n;
	}
	t = new_node(N_ADDR, line);
	t->lhs = last;
	t->ty = ptr_to(last->ty);
	n = new_node(N_CAST, line);
	n->lhs = t;
	n->ty = ptr_to(ty_char);
	t = new_node(N_ASSIGN, line);
	t->lhs = ap;
	t->rhs = mk_binop(T_PLUS, n, num(4), line);
	t->ty = ap->ty;
	return t;
}

static struct node *primary(void)
{
	struct sym *f;
    struct node *n;
    struct sym *s;

    if (cur->kind == T_NUM) {
        n = num_lit(cur);
        advance();
        return n;
    }
    if (cur->kind == T_FNUM)
        die_at(cur->line, "floating-point constants are not supported");
    if (cur->kind == T_STR) {
        n = add_string(cur);
        advance();
        return n;
    }
    if (cur->kind == T_LPAREN) {
        advance();
        n = expr();
        expect(T_RPAREN, "')'");
        return n;
    }
    if (cur->kind == T_IDENT) {
        if (cur[1].kind == T_LPAREN && is_builtin(cur->text))
            return builtin_va();
        s = lookup(cur->text);
        if (!s && cur_fn && strcmp(cur->text, "__func__") == 0) {
            struct token ft[2];
            struct token *save;

            ft[0].kind = T_STR;
            ft[0].text = cur_fn;
            ft[0].len = (int)strlen(cur_fn);
            ft[0].line = cur->line;
            ft[1].kind = T_EOF;
            save = cur;
            n = add_string(ft);
            cur = save;
            advance();
            return n;
        }
        if (cur[1].kind == T_LPAREN && (!s || s->kind == S_FUNC))
            return call();
        if (!s && (f = find_func(cur->text))) {
            n = new_node(N_FUNCADDR, cur->line);
            n->name = f->name;
            n->ty = ptr_to(func_of(f->ty));
            advance();
            return n;
        }
        if (!s)
            die_at(cur->line, "undeclared identifier '%s'",
                   cur->text);
        if (s->kind == S_FUNC) {
            n = new_node(N_FUNCADDR, cur->line);
            n->name = s->name;
            n->ty = ptr_to(func_of(s->ty));
            advance();
            return n;
        }
        if (s->kind == S_ENUM) {
            n = num(s->ival);
            advance();
            return n;
        }
        if (s->kind != S_LOCAL && s->kind != S_GLOBAL)
            die_at(cur->line, "'%s' is not a variable", cur->text);
        n = new_node(s->kind == S_LOCAL ? N_VAR : N_GVAR, cur->line);
        n->name = s->label ? s->label : s->name;
        n->offset = s->offset;
        n->ty = s->ty;
        advance();
        return n;
    }
    die_at(cur->line, "expected expression");
    return NULL;
}

static struct node *mk_opassign(int op, struct node *lhs, struct node *rhs,
				int line)
{
	struct node *n;

	if (!is_lvalue(lhs) || !is_scalar(lhs->ty))
		die_at(line, "invalid assignment target");
	rhs = decay(rhs);
	if (is_ptr(lhs->ty) && (op == T_PLUS || op == T_MINUS))
		rhs = scale(rhs, lhs->ty);
	n = new_node(N_OPASSIGN, line);
	n->op = op;
	n->lhs = lhs;
	n->rhs = rhs;
	n->ty = lhs->ty;
	return n;
}

static struct member *find_member(struct type *t, const char *name, int *extra)
{
	struct member *m;
	struct member *r;
	int sub;

	for (m = t->members; m; m = m->next) {
		if (m->name) {
			if (strcmp(m->name, name) == 0)
				return m;
		} else if (m->ty->kind == TY_STRUCT) {
			sub = 0;
			r = find_member(m->ty, name, &sub);
			if (r) {
				*extra += m->offset + sub;
				return r;
			}
		}
	}
	return NULL;
}

static struct node *member_of(struct node *base, int line)
{
	struct node *n;
	struct member *m;
	int extra;

	if (base->ty->kind != TY_STRUCT)
		die_at(line, "member access on non-struct");
	if (cur->kind != T_IDENT)
		die_at(cur->line, "expected member name");
	extra = 0;
	m = find_member(base->ty, cur->text, &extra);
	if (!m)
		die_at(cur->line, "no member named '%s'", cur->text);
	if (m->bit_width)
		die_at(cur->line, "access to bit-field '%s' is not supported",
		       cur->text);
	advance();
	n = new_node(N_MEMBER, line);
	n->lhs = base;
	n->offset = m->offset + extra;
	n->ty = m->ty;
	return n;
}

static struct node *postfix(void)
{
	struct node *n;
	struct node *idx;
	int line;

	n = primary();
	for (;;) {
		line = cur->line;
		if (cur->kind == T_LPAREN) {
			n = indirect_call(n, line);
			continue;
		}
		if (cur->kind == T_DOT) {
			advance();
			n = member_of(n, line);
			continue;
		}
		if (cur->kind == T_ARROW) {
			advance();
			n = member_of(mk_deref(n, line), line);
			continue;
		}
		if (cur->kind == T_INC || cur->kind == T_DEC) {
			idx = mk_opassign(cur->kind == T_INC ? T_PLUS : T_MINUS,
					  n, num(1), line);
			idx->kind = N_POSTINC;
			advance();
			n = idx;
			continue;
		}
		if (cur->kind != T_LBRACKET)
			return n;
		advance();
		idx = expr();
		expect(T_RBRACKET, "']'");
		n = mk_deref(mk_binop(T_PLUS, n, idx, line), line);
	}
}

static struct node *cast_expr(void)
{
	struct node *n;
	struct type *t;
	int line;

	if (cur->kind == T_LPAREN && token_is_type(cur + 1)) {
		line = cur->line;
		advance();
		t = type_name();
		expect(T_RPAREN, "')'");
		n = new_node(N_CAST, line);
		n->lhs = decay(cast_expr());
		if (!is_scalar(n->lhs->ty) || !(is_scalar(t) || t->kind == TY_VOID))
			die_at(line, "invalid cast");
		n->ty = t;
		return n;
	}
	return unary();
}

static struct node *unary(void)
{
	struct node *n;
	struct node *o;
	struct type *t;
	enum tok_kind k;
	int line;

	line = cur->line;
	switch (cur->kind) {
	case T_MINUS:
		advance();
		o = decay(cast_expr());
		if (!is_integer(o->ty))
			die_at(line, "invalid operand to unary '-'");
		n = new_node(N_NEG, line);
		n->lhs = o;
		n->ty = promote(o->ty);
		return n;
	case T_PLUS:
		advance();
		o = decay(cast_expr());
		if (!is_integer(o->ty))
			die_at(line, "invalid operand to unary '+'");
		return o;
	case T_TILDE:
		advance();
		o = decay(cast_expr());
		if (!is_integer(o->ty))
			die_at(line, "invalid operand to '~'");
		n = new_node(N_BNOT, line);
		n->lhs = o;
		n->ty = promote(o->ty);
		return n;
	case T_INC:
	case T_DEC:
		k = cur->kind;
		advance();
		o = unary();
		return mk_opassign(k == T_INC ? T_PLUS : T_MINUS,
				   o, num(1), line);
	case T_BANG:
		advance();
		n = new_node(N_NOT, line);
		n->lhs = decay(cast_expr());
		if (!is_scalar(n->lhs->ty))
			die_at(line, "invalid operand to '!'");
		n->ty = ty_int;
		return n;
	case T_STAR:
		advance();
		return mk_deref(cast_expr(), line);
	case T_AMP:
		advance();
		o = cast_expr();
		if (o->kind == N_FUNCADDR)
			return o;
		if (!is_lvalue(o))
			die_at(line, "cannot take the address of this expression");
		n = new_node(N_ADDR, line);
		n->lhs = o;
		n->ty = ptr_to(o->ty);
		return n;
	case T_KW_SIZEOF:
		advance();
		if (cur->kind == T_LPAREN && token_is_type(cur + 1)) {
			advance();
			t = type_name();
			expect(T_RPAREN, "')'");
		} else {
			t = unary()->ty;
		}
		if (t->kind == TY_VOID || t->size == 0)
			die_at(line, "sizeof applied to an incomplete type");
		n = num(t->size);
		n->ty = ty_long;
		return n;
	default:
		return postfix();
	}
}

static int prec_of(enum tok_kind k)
{
	switch (k) {
	case T_OROR: return 1;
	case T_ANDAND: return 2;
	case T_PIPE: return 3;
	case T_CARET: return 4;
	case T_AMP: return 5;
	case T_EQ: case T_NE: return 6;
	case T_LT: case T_GT: case T_LE: case T_GE: return 7;
	case T_SHL: case T_SHR: return 8;
	case T_PLUS: case T_MINUS: return 9;
	case T_STAR: case T_SLASH: case T_PERCENT: return 10;
	default: return 0;
	}
}

static struct node *binary(int min_prec)
{
	struct node *lhs;
	struct node *rhs;
	struct node *n;
	enum tok_kind op;
	int p;
	int line;

	lhs = cast_expr();
	for (;;) {
		p = prec_of(cur->kind);
		if (p == 0 || p < min_prec)
			return lhs;
		op = cur->kind;
		line = cur->line;
		advance();
		rhs = binary(p + 1);
		if (op == T_ANDAND || op == T_OROR) {
			n = new_node(op == T_ANDAND ? N_AND : N_OR, line);
			n->lhs = decay(lhs);
			n->rhs = decay(rhs);
			if (!is_scalar(n->lhs->ty) || !is_scalar(n->rhs->ty))
				die_at(line, "invalid operands to logical operator");
			n->ty = ty_int;
			lhs = n;
		} else {
			lhs = mk_binop(op, lhs, rhs, line);
		}
	}
}

static int assign_op(enum tok_kind k)
{
	switch (k) {
	case T_ADDASSIGN: return T_PLUS;
	case T_SUBASSIGN: return T_MINUS;
	case T_MULASSIGN: return T_STAR;
	case T_DIVASSIGN: return T_SLASH;
	case T_MODASSIGN: return T_PERCENT;
	case T_ANDASSIGN: return T_AMP;
	case T_ORASSIGN: return T_PIPE;
	case T_XORASSIGN: return T_CARET;
	case T_SHLASSIGN: return T_SHL;
	case T_SHRASSIGN: return T_SHR;
	default: return 0;
	}
}

static struct node *cond(void)
{
	struct node *c;
	struct node *n;
	struct node *a;
	struct node *b;

	c = binary(1);
	if (cur->kind != T_QUESTION)
		return c;
	n = new_node(N_COND, cur->line);
	advance();
	a = decay(expr());
	expect(T_COLON, "':'");
	b = decay(cond());
	n->lhs = decay(c);
	n->rhs = a;
	n->body = b;
	if (is_ptr(a->ty))
		n->ty = a->ty;
	else if (is_ptr(b->ty))
		n->ty = b->ty;
	else
		n->ty = common_type(a->ty, b->ty);
	return n;
}

static struct node *assign(void)
{
	struct node *lhs;
	struct node *n;
	int line;
	int op;

	lhs = cond();
	if (assign_op(cur->kind)) {
		line = cur->line;
		op = assign_op(cur->kind);
		advance();
		return mk_opassign(op, lhs, assign(), line);
	}
	if (cur->kind != T_ASSIGN)
		return lhs;
	line = cur->line;
	if (!is_lvalue(lhs) || lhs->ty->kind == TY_ARRAY)
		die_at(line, "invalid assignment target");
	advance();
	n = new_node(N_ASSIGN, line);
	n->lhs = lhs;
	n->rhs = decay(assign());
	if (lhs->ty->kind == TY_STRUCT) {
		if (n->rhs->ty != lhs->ty)
			die_at(line, "incompatible struct assignment");
	} else if (!is_scalar(n->rhs->ty))
		die_at(line, "invalid assignment value");
	n->ty = lhs->ty;
	return n;
}

static struct node *expr(void)
{
	struct node *n;
	struct node *c;

	n = assign();
	while (cur->kind == T_COMMA) {
		c = new_node(N_COMMA, cur->line);
		advance();
		c->lhs = n;
		c->rhs = decay(assign());
		c->ty = c->rhs->ty;
		n = c;
	}
	return n;
}

static struct node *cond_expr(void)
{
	struct node *n;

	n = decay(expr());
	if (!is_scalar(n->ty))
		die_at(n->line, "scalar value required");
	return n;
}

static struct node *block(void)
{
	struct node *n;
	struct node head;
	struct node *tail;
	struct sym *saved;
	struct sym *saved_start;

	n = new_node(N_BLOCK, cur->line);
	cur++;
	saved = syms;
	saved_start = scope_start;
	scope_start = syms;
	head.next = NULL;
	tail = &head;
	while (!accept(T_RBRACE)) {
		if (cur->kind == T_EOF)
			die_at(cur->line, "expected '}'");
		tail->next = stmt();
		tail = tail->next;
	}
	syms = saved;
	scope_start = saved_start;
	n->body = head.next;
	return n;
}

static struct node *var_node(struct sym *s, int line)
{
	struct node *n;

	n = new_node(N_VAR, line);
	n->name = s->name;
	n->offset = s->offset;
	n->ty = s->ty;
	return n;
}

static struct node *elem_lvalue(struct node *arr, long i, int line)
{
	struct node *idx;

	idx = new_node(N_NUM, line);
	idx->ival = i;
	idx->ty = ty_int;
	return mk_deref(mk_binop(T_PLUS, arr, idx, line), line);
}

static struct node *assign_stmt(struct node *lhs, struct node *rhs, int line)
{
	struct node *a;
	struct node *st;

	a = new_node(N_ASSIGN, line);
	a->lhs = lhs;
	a->rhs = rhs;
	a->ty = lhs->ty;
	st = new_node(N_EXPR_STMT, line);
	st->lhs = a;
	return st;
}

static struct node *local_array_init(struct sym *s, int line)
{
	struct node *blk;
	struct node head;
	struct node *t;
	struct node *items;
	struct node *it;
	struct node *k;
	struct node *z;
	long i;
	long count;

	blk = new_node(N_BLOCK, line);
	head.next = NULL;
	t = &head;
	items = NULL;
	z = NULL;
	count = 0;
	if (cur->kind == T_STR) {
		it = add_string(cur);
		advance();
		for (i = 0; i <= it->ival; i++) {
			k = num(i == it->ival ? 0 : (signed char)it->text[i]);
			k->next = NULL;
			if (z)
				z->next = k;
			else
				items = k;
			z = k;
			count++;
		}
	} else {
		expect(T_LBRACE, "'{'");
		while (cur->kind != T_RBRACE) {
			k = decay(assign());
			k->next = NULL;
			if (z)
				z->next = k;
			else
				items = k;
			z = k;
			count++;
			if (!accept(T_COMMA))
				break;
		}
		expect(T_RBRACE, "'}'");
	}
	if (s->ty->len < 0) {
		s->ty = array_of(s->ty->base, count);
		s->offset = alloc_local(s->ty);
	}
	if (count > s->ty->len)
		die_at(line, "too many initializers");
	i = 0;
	for (it = items; it; it = it->next, i++) {
		t->next = assign_stmt(elem_lvalue(var_node(s, line), i, line),
				      it, line);
		t = t->next;
	}
	for (; i < s->ty->len; i++) {
		t->next = assign_stmt(elem_lvalue(var_node(s, line), i, line),
				      num(0), line);
		t = t->next;
	}
	blk->body = head.next;
	return blk;
}

static struct node *local_decl(void)
{
	struct node *blk;
	struct node *n;
	struct node head;
	struct node *t;
	struct sym *s;
	struct type *base;
	struct type *ty;
	char *name;
	int storage;
	int line;

	line = cur->line;
	storage = 0;
	base = specifiers(&storage);
	if (!base)
		die_at(line, "expected type");
	blk = new_node(N_BLOCK, line);
	head.next = NULL;
	t = &head;
	if (accept(T_SEMI))
		return blk;
	for (;;) {
		ty = declarator(base, &name);
		if (!name)
			die_at(line, "expected variable name");
		if (storage & 1) {
			add_typedef(name, line, ty);
			if (!accept(T_COMMA))
				break;
			continue;
		}
		if (ty->kind == TY_FUNC) {
			declare_func(name, ty, line);
			if (!accept(T_COMMA))
				break;
			continue;
		}
		if (storage & 4) {
			if (!lookup(name))
				add_sym(name, S_GLOBAL, line, 0, ty)->is_extern = 1;
			if (!accept(T_COMMA))
				break;
			continue;
		}
		if (ty->kind == TY_VOID)
			die_at(line, "variable '%s' has type void", name);
		if (storage & 2) {
			char *lbl;

			lbl = xalloc(24);
			sprintf(lbl, ".LS%d", nstatic++);
			global_var(lbl, ty, 2, line);
			s = add_sym(name, S_GLOBAL, line, 0, ty);
			s->label = lbl;
			if (!accept(T_COMMA))
				break;
			continue;
		}
		if (ty->kind == TY_ARRAY && ty->len < 0 && cur->kind != T_ASSIGN)
			die_at(line, "array size missing for '%s'", name);
		s = add_sym(name, S_LOCAL, line,
			    ty->len < 0 ? 0 : alloc_local(ty), ty);
		if (accept(T_ASSIGN)) {
			if (ty->kind == TY_ARRAY) {
				n = local_array_init(s, line);
				t->next = n;
				t = n;
				if (!accept(T_COMMA))
					break;
				continue;
			}
			else if (ty->kind == TY_STRUCT) { n = local_struct_init(s, line); }
			n = new_node(N_DECL, line);
			n->offset = s->offset;
			n->ty = ty;
			n->rhs = decay(assign());
			if (!is_scalar(n->rhs->ty))
				die_at(line, "invalid initializer");
			t->next = n;
			t = n;
		}
		if (!accept(T_COMMA))
			break;
	}
	expect(T_SEMI, "';'");
	blk->body = head.next;
	return blk;
}

static struct node *stmt(void)
{
	struct node *n;
	struct node *c;

	if (cur->kind == T_LBRACE)
		return block();
	if (accept(T_SEMI)) {
		n = new_node(N_BLOCK, cur->line);
		return n;
	}
	if (accept(T_KW_IF)) {
		n = new_node(N_IF, cur->line);
		expect(T_LPAREN, "'('");
		n->lhs = cond_expr();
		expect(T_RPAREN, "')'");
		n->rhs = stmt();
		if (accept(T_KW_ELSE))
			n->body = stmt();
		return n;
	}
	if (accept(T_KW_WHILE)) {
		n = new_node(N_WHILE, cur->line);
		expect(T_LPAREN, "'('");
		n->lhs = cond_expr();
		expect(T_RPAREN, "')'");
		brk_depth++;
		cont_depth++;
		n->rhs = stmt();
		brk_depth--;
		cont_depth--;
		return n;
	}
	if (accept(T_KW_DO)) {
		n = new_node(N_DO, cur->line);
		brk_depth++;
		cont_depth++;
		n->rhs = stmt();
		brk_depth--;
		cont_depth--;
		expect(T_KW_WHILE, "'while'");
		expect(T_LPAREN, "'('");
		n->lhs = cond_expr();
		expect(T_RPAREN, "')'");
		expect(T_SEMI, "';'");
		return n;
	}
	if (accept(T_KW_FOR)) {
		n = new_node(N_FOR, cur->line);
		expect(T_LPAREN, "'('");
		if (!accept(T_SEMI)) {
			n->init = expr();
			expect(T_SEMI, "';'");
		}
		if (!accept(T_SEMI)) {
			n->lhs = cond_expr();
			expect(T_SEMI, "';'");
		}
		if (!accept(T_RPAREN)) {
			n->step = expr();
			expect(T_RPAREN, "')'");
		}
		brk_depth++;
		cont_depth++;
		n->rhs = stmt();
		brk_depth--;
		cont_depth--;
		return n;
	}
	if (cur->kind == T_KW_SWITCH) {
		advance();
		n = new_node(N_SWITCH, cur->line);
		expect(T_LPAREN, "'('");
		n->lhs = cond_expr();
		expect(T_RPAREN, "')'");
		c = cur_switch;
		cur_switch = n;
		brk_depth++;
		n->rhs = stmt();
		brk_depth--;
		cur_switch = c;
		return n;
	}
	if (cur->kind == T_KW_CASE || cur->kind == T_KW_DEFAULT) {
		if (!cur_switch)
			die_at(cur->line, "case outside switch");
		n = new_node(N_CASE, cur->line);
		if (cur->kind == T_KW_DEFAULT) {
			advance();
			n->op = 1;
			cur_switch->init = n;
		} else {
			advance();
			n->ival = const_eval(cond());
			if (!cur_switch->cases) {
				cur_switch->cases = n;
			} else {
				c = cur_switch->cases;
				while (c->cnext)
					c = c->cnext;
				c->cnext = n;
			}
		}
		expect(T_COLON, "':'");
		n->lhs = cur->kind == T_RBRACE ? new_node(N_BLOCK, cur->line)
		    : stmt();
		return n;
	}
	if (cur->kind == T_KW_GOTO) {
		advance();
		n = new_node(N_GOTO, cur->line);
		if (cur->kind != T_IDENT)
			die_at(cur->line, "expected label");
		n->name = cur->text;
		advance();
		expect(T_SEMI, "';'");
		return n;
	}
	if (cur->kind == T_IDENT && cur[1].kind == T_COLON) {
		n = new_node(N_LABEL, cur->line);
		n->name = cur->text;
		advance();
		advance();
		n->lhs = cur->kind == T_RBRACE ? new_node(N_BLOCK, cur->line)
		    : stmt();
		return n;
	}
	if (cur->kind == T_KW_BREAK || cur->kind == T_KW_CONTINUE) {
		if (cur->kind == T_KW_BREAK && !brk_depth)
			die_at(cur->line, "break outside loop or switch");
		if (cur->kind == T_KW_CONTINUE && !cont_depth)
			die_at(cur->line, "continue outside loop");
		n = new_node(cur->kind == T_KW_BREAK ? N_BREAK : N_CONTINUE,
			     cur->line);
		cur++;
		expect(T_SEMI, "';'");
		return n;
	}
	if (accept(T_KW_RETURN)) {
		n = new_node(N_RETURN, cur->line);
		if (cur->kind != T_SEMI) {
			n->lhs = decay(expr());
			if (!is_scalar(n->lhs->ty))
				die_at(n->line, "invalid return value");
		}
		expect(T_SEMI, "';'");
		return n;
	}
	if (token_is_type(cur))
		return local_decl();
	n = new_node(N_EXPR_STMT, cur->line);
	n->lhs = expr();
	expect(T_SEMI, "';'");
	return n;
}

static struct param *parse_params(int *nparams, int *variadic)
{
    struct param head;
    struct param *t;
    struct param *p;
    struct type *base;
    struct type *ty;
    char *name;
    int storage;

    head.next = NULL;
    t = &head;
    *nparams = 0;
    *variadic = 0;
    expect(T_LPAREN, "'('");
    if (cur->kind == T_RPAREN) {
        *nparams = -1;
    } else if (cur->kind == T_KW_VOID && cur[1].kind == T_RPAREN) {
        advance();
    } else {
        for (;;) {
            if (accept(T_ELLIPSIS)) {
                *variadic = 1;
                break;
            }
            storage = 0;
            base = specifiers(&storage);
            if (!base)
                die_at(cur->line, "expected parameter type");
            ty = declarator(base, &name);
            if (ty->kind == TY_ARRAY)
                ty = ptr_to(ty->base);
            else if (ty->kind == TY_FUNC)
                ty = ptr_to(ty);
            p = xalloc(sizeof *p);
            p->name = name;
            p->ty = ty;
            t->next = p;
            t = p;
            (*nparams)++;
            if (!accept(T_COMMA))
                break;
        }
    }
    expect(T_RPAREN, "')'");
    return head.next;
}

static int is_float_type(struct type *t)
{
	return t->kind == TY_FLOAT || t->kind == TY_DOUBLE;
}

static void declare_func(char *name, struct type *ft, int line)
{
	if (ft->base->kind == TY_ARRAY || ft->base->kind == TY_FUNC)
		die_at(line, "function cannot return an array or function");
	add_func(name, ft->base, ft->variadic ? -1 : ft->nparams);
}

static struct node *function(struct type *ft, char *name, int line)
{
	struct node *fn;
	struct node *p;
	struct node phead;
	struct node *pt;
	struct node head;
	struct node *tail;
	struct param *pr;
	struct type *ret;
	struct sym *gsave;
	int nparams;
	int variadic;

	ret = ft->base;
	nparams = ft->nparams;
	variadic = ft->variadic;
	declare_func(name, ft, line);
	if (is_float_type(ret))
		die_at(line, "floating-point types are not supported");
	fn = new_node(N_FUNC, line);
	fn->name = name;
	fn->ty = ret;
	fn->op = cur_static;
	cur_fn = name;
	gsave = syms;
	scope_start = syms;
	frame = 0;
	brk_depth = 0;
	cont_depth = 0;
	cur_switch = NULL;
	phead.next = NULL;
	pt = &phead;
	nparams = nparams < 0 ? 0 : nparams;
	{
		int idx;

		idx = 0;
		for (pr = ft->params; pr; pr = pr->next, idx++) {
			if (!pr->name)
				die_at(line, "parameter name omitted");
			if (pr->ty->kind == TY_VOID)
				die_at(line, "parameter has type void");
			if (is_float_type(pr->ty))
				die_at(line,
				       "floating-point types are not supported");
			p = new_node(N_DECL, line);
			if (tg->bits == 32) {
				p->offset = 8 + 4 * idx;
			} else if (idx < 6) {
				frame = align_to(frame + 8, 8);
				p->offset = -frame;
			} else {
				p->offset = 16 + 8 * (idx - 6);
			}
			p->ty = pr->ty;
			add_sym(pr->name, S_LOCAL, line, p->offset, pr->ty);
			pt->next = p;
			pt = p;
		}
	}
	if (cur->kind != T_LBRACE)
		die_at(cur->line, "expected '{'");
	cur_variadic = variadic;
	cur_nparams = nparams;
	cur_va_off = 0;
	if (variadic && tg->bits == 64) {
		frame = align_to(frame + 48, 8);
		cur_va_off = -frame;
		fn->label = cur_va_off;
	}
	head.next = NULL;
	tail = &head;
	cur++;
	while (!accept(T_RBRACE)) {
		if (cur->kind == T_EOF)
			die_at(cur->line, "expected '}'");
		tail->next = stmt();
		tail = tail->next;
	}
	cur_variadic = 0;
	fn->body = head.next;
	fn->lhs = phead.next;
	fn->ival = nparams;
	fn->offset = (frame + 15) & ~15;
	syms = gsave;
	scope_start = NULL;
	return fn;
}

static int fn_uses_float(struct type *ft)
{
	struct param *p;

	if (is_float_type(ft->base))
		return 1;
	for (p = ft->params; p; p = p->next)
		if (is_float_type(p->ty))
			return 1;
	return 0;
}

static void skip_body(char *name, int line)
{
	struct sym *f;
	int depth;

	f = find_func(name);
	if (f)
		f->ival = 1;
	depth = 0;
	do {
		if (cur->kind == T_LBRACE)
			depth++;
		else if (cur->kind == T_RBRACE)
			depth--;
		else if (cur->kind == T_EOF)
			die_at(line, "expected '}'");
		advance();
	} while (depth > 0);
}

struct node *parse_unit(struct token *toks)
{
	struct node head;
	struct node *tail;
	struct node *n;
	struct type *base;
	struct type *ty;
	char *name;
	int line;
	int storage;

	cur = toks;
	head.next = NULL;
	tail = &head;
	ty_valist = tg->bits == 64 ? array_of(ty_long, 3) : ptr_to(ty_char);
	add_sym("va_list", S_TYPEDEF, 0, 0, ty_valist);
	add_sym("__builtin_va_list", S_TYPEDEF, 0, 0, ty_valist);
	while (cur->kind != T_EOF) {
		line = cur->line;
		storage = 0;
		if (accept(T_SEMI))
			continue;
		base = specifiers(&storage);
		if (!base)
			die_at(line, "expected type");
		if (accept(T_SEMI))
			continue;
		for (;;) {
			ty = declarator(base, &name);
			if (!name)
				die_at(line, "expected identifier");
			if (storage & 1) {
				add_typedef(name, line, ty);
			} else if (ty->kind == TY_FUNC) {
				if (cur->kind == T_LBRACE && fn_uses_float(ty)) {
					if (!(storage & 2))
						die_at(line, "floating-point types are not supported");
					declare_func(name, ty, line);
					skip_body(name, line);
					break;
				}
				if (cur->kind == T_LBRACE) {
					cur_static = (storage & 2) != 0;
					n = function(ty, name, line);
					tail->next = n;
					tail = tail->next;
					break;
				}
				declare_func(name, ty, line);
			} else {
				global_var(name, ty, storage, line);
			}
			if (!accept(T_COMMA)) {
				expect(T_SEMI, "';'");
				break;
			}
		}
	}
	if (!head.next && !g_globals)
		die_at(cur->line, "empty translation unit");
	return head.next;
}
