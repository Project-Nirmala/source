#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nc.h"

#define NC_HASH 509
#define NC_MAXPASS 100

static struct nc_sym *table[NC_HASH];
static struct nc_sym *head;
static struct nc_sym *tail;

static unsigned hash(const char *s)
{
    unsigned h = 5381;

    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % NC_HASH;
}

struct nc_sym *nc_sym_find(const char *name)
{
    struct nc_sym *s;

    for (s = table[hash(name)]; s; s = s->hnext)
        if (!strcmp(s->name, name))
            return s;
    return NULL;
}

struct nc_sym *nc_sym_get(const char *name)
{
    struct nc_sym *s = nc_sym_find(name);
    unsigned h;

    if (s)
        return s;
    s = nc_xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->name = nc_xstrdup(name);
    h = hash(name);
    s->hnext = table[h];
    table[h] = s;
    if (tail)
        tail->lnext = s;
    else
        head = s;
    tail = s;
    return s;
}

struct nc_sym *nc_sym_first(void)
{
    return head;
}

int nc_sym_active(const struct nc_sym *s)
{
    return s->visible || s->forced;
}

static const char *default_value(const struct nc_sym *s)
{
    const struct nc_default *d;

    for (d = s->defaults; d; d = d->next)
        if (!d->cond || nc_truthy(nc_expr_eval(d->cond)))
            return nc_expr_eval(d->val);
    return NULL;
}

static int in_range(const struct nc_sym *s, long x)
{
    long lo;
    long hi;

    if (!s->range_lo || !s->range_hi)
        return 1;
    lo = strtol(s->range_lo, NULL, 0);
    hi = strtol(s->range_hi, NULL, 0);
    return x >= lo && x <= hi;
}

static char *clamp(const struct nc_sym *s, const char *v)
{
    char buf[40];
    long x;
    long lo;
    long hi;

    if (!s->range_lo || !s->range_hi)
        return nc_xstrdup(v);
    x = strtol(v, NULL, 0);
    lo = strtol(s->range_lo, NULL, 0);
    hi = strtol(s->range_hi, NULL, 0);
    if (x >= lo && x <= hi)
        return nc_xstrdup(v);
    if (x < lo)
        x = lo;
    else
        x = hi;
    if (s->type == NC_HEX)
        sprintf(buf, "0x%lx", (unsigned long)x);
    else
        sprintf(buf, "%ld", x);
    return nc_xstrdup(buf);
}

static char *compute(const struct nc_sym *s)
{
    const char *base;

    switch (s->type) {
    case NC_BOOL:
        if (s->forced)
            return nc_xstrdup("y");
        if (!s->visible)
            return nc_xstrdup("n");
        if (s->prompt && s->user)
            base = s->user;
        else
            base = default_value(s);
        return nc_xstrdup(nc_truthy(base) ? "y" : "n");
    case NC_INT:
    case NC_HEX:
        if (!s->visible)
            return nc_xstrdup("");
        if (s->prompt && s->user)
            base = s->user;
        else
            base = default_value(s);
        if (!base || !*base)
            base = "0";
        return clamp(s, base);
    case NC_STRING:
        if (!s->visible)
            return nc_xstrdup("");
        if (s->prompt && s->user)
            base = s->user;
        else
            base = default_value(s);
        return nc_xstrdup(base ? base : "");
    default:
        return nc_xstrdup("");
    }
}

static int pass(void)
{
    struct nc_sym *s;
    struct nc_sym *t;
    struct nc_select *sel;
    char *nv;
    int changed = 0;

    for (s = head; s; s = s->lnext)
        s->forced = 0;
    for (s = head; s; s = s->lnext) {
        if (s->type != NC_BOOL || !s->value || strcmp(s->value, "y"))
            continue;
        for (sel = s->selects; sel; sel = sel->next) {
            t = nc_sym_find(sel->target);
            if (t && (!sel->cond || nc_truthy(nc_expr_eval(sel->cond))))
                t->forced = 1;
        }
    }
    for (s = head; s; s = s->lnext) {
        s->visible = !s->deps || nc_truthy(nc_expr_eval(s->deps));
        nv = compute(s);
        if (!s->value || strcmp(nv, s->value)) {
            free(s->value);
            s->value = nv;
            changed = 1;
        } else {
            free(nv);
        }
    }
    return changed;
}

void nc_resolve(void)
{
    int i;

    for (i = 0; i < NC_MAXPASS; i++)
        if (!pass())
            return;
    nc_warn("configuration did not converge (dependency cycle?)");
}

int nc_sym_set_user(struct nc_sym *s, const char *val)
{
    char buf[40];
    char *end;
    char *nv;
    long iv;
    unsigned long uv;

    switch (s->type) {
    case NC_BOOL:
        if (strcmp(val, "y") && strcmp(val, "n")) {
            nc_warn("invalid value \"%s\" for %s (expected y or n)", val, s->name);
            return -1;
        }
        nv = nc_xstrdup(val);
        break;
    case NC_INT:
        iv = strtol(val, &end, 10);
        if (!*val || *end) {
            nc_warn("invalid integer \"%s\" for %s", val, s->name);
            return -1;
        }
        if (!in_range(s, iv)) {
            nc_warn("value %ld for %s is outside [%s..%s]", iv, s->name, s->range_lo, s->range_hi);
            return -1;
        }
        sprintf(buf, "%ld", iv);
        nv = nc_xstrdup(buf);
        break;
    case NC_HEX:
        uv = strtoul(val, &end, 16);
        if (!*val || *end) {
            nc_warn("invalid hex value \"%s\" for %s", val, s->name);
            return -1;
        }
        if (!in_range(s, (long)uv)) {
            nc_warn("value 0x%lx for %s is outside [%s..%s]", uv, s->name, s->range_lo, s->range_hi);
            return -1;
        }
        sprintf(buf, "0x%lx", uv);
        nv = nc_xstrdup(buf);
        break;
    case NC_STRING:
        nv = nc_xstrdup(val);
        break;
    default:
        nc_warn("symbol %s has no type", s->name);
        return -1;
    }
    free(s->user);
    s->user = nv;
    return 0;
}

void nc_validate(void)
{
    const struct nc_sym *s;
    const struct nc_default *d;
    const struct nc_select *sel;
    const struct nc_sym *t;

    for (s = head; s; s = s->lnext) {
        if (s->type == NC_UNKNOWN)
            nc_warn("symbol %s has no type", s->name);
        nc_expr_check(s->name, s->deps);
        for (d = s->defaults; d; d = d->next) {
            nc_expr_check(s->name, d->val);
            nc_expr_check(s->name, d->cond);
        }
        for (sel = s->selects; sel; sel = sel->next) {
            t = nc_sym_find(sel->target);
            if (!t)
                nc_warn("%s: selects undefined symbol %s", s->name, sel->target);
            else if (t->type != NC_BOOL)
                nc_warn("%s: selects non-bool symbol %s", s->name, sel->target);
            nc_expr_check(s->name, sel->cond);
        }
    }
}
