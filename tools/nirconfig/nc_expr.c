#include <string.h>
#include "nc.h"

struct nc_expr *nc_expr_new(enum nc_ekind kind, const char *text, struct nc_expr *l, struct nc_expr *r)
{
    struct nc_expr *e = nc_xmalloc(sizeof *e);

    e->kind = kind;
    e->text = text ? nc_xstrdup(text) : NULL;
    e->l = l;
    e->r = r;
    return e;
}

struct nc_expr *nc_expr_and(struct nc_expr *a, struct nc_expr *b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    return nc_expr_new(E_AND, NULL, a, b);
}

int nc_is_number(const char *s)
{
    if (*s == '-')
        s++;
    return *s >= '0' && *s <= '9';
}

int nc_truthy(const char *s)
{
    return s && s[0] && strcmp(s, "n") != 0 && strcmp(s, "0") != 0;
}

const char *nc_expr_eval(const struct nc_expr *e)
{
    const struct nc_sym *s;
    const char *a;
    const char *b;

    switch (e->kind) {
    case E_LIT:
        return e->text;
    case E_SYM:
        if (!strcmp(e->text, "y") || !strcmp(e->text, "n") || nc_is_number(e->text))
            return e->text;
        s = nc_sym_find(e->text);
        if (!s)
            return "n";
        if (s->value)
            return s->value;
        return s->type == NC_BOOL ? "n" : "";
    case E_NOT:
        return nc_truthy(nc_expr_eval(e->l)) ? "n" : "y";
    case E_AND:
        return nc_truthy(nc_expr_eval(e->l)) && nc_truthy(nc_expr_eval(e->r)) ? "y" : "n";
    case E_OR:
        return nc_truthy(nc_expr_eval(e->l)) || nc_truthy(nc_expr_eval(e->r)) ? "y" : "n";
    case E_EQ:
        a = nc_expr_eval(e->l);
        b = nc_expr_eval(e->r);
        return strcmp(a, b) == 0 ? "y" : "n";
    case E_NE:
        a = nc_expr_eval(e->l);
        b = nc_expr_eval(e->r);
        return strcmp(a, b) != 0 ? "y" : "n";
    }
    return "n";
}

void nc_expr_check(const char *ctx, const struct nc_expr *e)
{
    if (!e)
        return;
    if (e->kind == E_SYM) {
        if (strcmp(e->text, "y") && strcmp(e->text, "n") && !nc_is_number(e->text) && !nc_sym_find(e->text))
            nc_warn("%s: undefined symbol %s", ctx, e->text);
        return;
    }
    nc_expr_check(ctx, e->l);
    nc_expr_check(ctx, e->r);
}
