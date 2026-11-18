#include "cc.h"

struct type *ty_void;
struct type *ty_char;
struct type *ty_int;
struct type *ty_long;
struct type *ty_short;
struct type *ty_uchar; 
struct type *ty_ushort; 
struct type *ty_uint; 
struct type *ty_ulong;
struct type *ty_float;
struct type *ty_double;
struct type *ty_ldouble;

static struct type *make(enum type_kind kind, int size, int align)
{
	struct type *t;

	t = xalloc(sizeof *t);
	t->kind = kind;
	t->size = size;
	t->align = align;
	return t;
}

void types_init(void)
{
    ty_void = make(TY_VOID, 1, 1);
    ty_char = make(TY_CHAR, 1, 1);
    ty_int = make(TY_INT, 4, 4);
    ty_long = make(TY_LONG, tg->word, tg->word);
    ty_short = make(TY_SHORT, 2, 2);
    ty_uchar = make(TY_CHAR, 1, 1);
    ty_uchar->is_unsigned = 1;
    ty_ushort = make(TY_SHORT, 2, 2);
    ty_ushort->is_unsigned = 1;
    ty_uint = make(TY_INT, 4, 4);
    ty_uint->is_unsigned = 1;
    ty_ulong = make(TY_LONG, tg->word, tg->word);
    ty_ulong->is_unsigned = 1;
    ty_float = make(TY_FLOAT, 4, 4);
    ty_double = make(TY_DOUBLE, 8, tg->bits == 64 ? 8 : 4);
    ty_ldouble = make(TY_DOUBLE, tg->bits == 64 ? 16 : 12,
                      tg->bits == 64 ? 16 : 4);
}

struct type *promote(struct type *t)
{
    if (t->kind == TY_CHAR || t->kind == TY_SHORT)
        return ty_int;
    return t;
}

struct type *common_type(struct type *a, struct type *b)
{
    a = promote(a);
    b = promote(b);
    if (a->size > b->size)
        return a;
    if (b->size > a->size)
        return b;
    if (a->is_unsigned)
        return a;
    if (b->is_unsigned)
        return b;
    return a->kind == TY_LONG ? a : b;
}

struct type *ptr_to(struct type *base)
{
	struct type *t;

	t = make(TY_PTR, tg->word, tg->word);
	t->base = base;
	return t;
}

struct type *array_of(struct type *base, long len)
{
	struct type *t;

	t = make(TY_ARRAY, len < 0 ? 0 : base->size * (int)len, base->align);
	t->base = base;
	t->len = len;
	return t;
}

struct type *new_struct(int is_union)
{
	struct type *t;

	t = make(TY_STRUCT, 0, 1);
	t->is_union = is_union;
	return t;
}

int align_to(int n, int a)
{
	return (n + a - 1) / a * a;
}

int is_integer(struct type *t)
{
    return t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_INT
        || t->kind == TY_LONG;
}

int is_ptr(struct type *t)
{
	return t->kind == TY_PTR;
}

int is_scalar(struct type *t)
{
	return is_integer(t) || is_ptr(t);
}

struct type *func_of(struct type *ret)
{
    struct type *t;

    t = make(TY_FUNC, 1, 1);
    t->base = ret;
    t->nparams = -1;
    return t;
}
