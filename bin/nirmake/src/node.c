#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "nirmake.h"

#if defined(__APPLE__)
#define ST_NSEC(st) ((long)(st).st_mtimespec.tv_nsec)
#elif defined(NM_NO_NSEC)
#define ST_NSEC(st) 0L
#else
#define ST_NSEC(st) ((long)(st).st_mtim.tv_nsec)
#endif

#define NHASH 1021

Node *node_first = NULL;
Node *default_goal = NULL;
int global_silent = 0;
int global_ignore = 0;
int global_precious = 0;
StrList suffixes;

static Node *ntab[NHASH];
static Node **ntail = &node_first;

typedef struct Meta {
    char *tgt;
    StrList srcs;
    StrList cmds;
    struct Meta *next;
} Meta;

static Meta *metas = NULL;
static Meta **mtail = &metas;

typedef struct Cand {
    StrList *cmds;
    StrList srcs;
    char *prefix;
} Cand;

static unsigned nhash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % NHASH;
}

Node *node_lookup(const char *name)
{
    Node *n;
    for (n = ntab[nhash(name)]; n != NULL; n = n->hnext)
        if (strcmp(n->name, name) == 0)
            return n;
    return NULL;
}

Node *node_find(const char *name)
{
    Node *n = node_lookup(name);
    unsigned h;
    if (n != NULL)
        return n;
    n = (Node *)nm_malloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->name = nm_strdup(name);
    h = nhash(name);
    n->hnext = ntab[h];
    ntab[h] = n;
    *ntail = n;
    ntail = &n->next;
    return n;
}

Rule *node_new_rule(Node *n, int dcolon)
{
    Rule *r = (Rule *)nm_malloc(sizeof(Rule));
    memset(r, 0, sizeof(Rule));
    sl_init(&r->cmds);
    r->dcolon = dcolon;
    r->file = nm_file ? nm_strdup(nm_file) : NULL;
    r->line = nm_line;
    if (n->rules_tail != NULL)
        n->rules_tail->next = r;
    else
        n->rules = r;
    n->rules_tail = r;
    return r;
}

void nlist_add(NList *l, Node *n)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (l->v[i] == n)
            return;
    if (l->n + 1 > l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->v = (Node **)nm_realloc(l->v, (size_t)l->cap * sizeof(Node *));
    }
    l->v[l->n++] = n;
}

int nm_time_cmp(NmTime a, NmTime b)
{
    if (a.s != b.s)
        return a.s < b.s ? -1 : 1;
    if (a.ns != b.ns)
        return a.ns < b.ns ? -1 : 1;
    return 0;
}

void nm_time_now(NmTime *t)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        t->s = ts.tv_sec;
        t->ns = (long)ts.tv_nsec;
    } else {
        t->s = time(NULL);
        t->ns = 0;
    }
}

void node_stat(Node *n)
{
    struct stat st;
    if (n->stat_done)
        return;
    n->stat_done = 1;
    if (stat(n->name, &st) == 0) {
        n->exists = 1;
        n->mtime.s = st.st_mtime;
        n->mtime.ns = ST_NSEC(st);
    } else {
        n->exists = 0;
        n->mtime.s = 0;
        n->mtime.ns = 0;
    }
}

void node_restat(Node *n)
{
    n->stat_done = 0;
    node_stat(n);
}

int node_has_cmds(Node *n)
{
    Rule *r;
    for (r = n->rules; r != NULL; r = r->next)
        if (r->cmds.n > 0)
            return 1;
    return 0;
}

void suff_add(const char *s)
{
    if (!sl_has(&suffixes, s))
        sl_add(&suffixes, s);
}

void suff_clear(void)
{
    sl_free(&suffixes);
}

int suff_known(const char *s)
{
    return sl_has(&suffixes, s);
}

int xform_name(const char *name)
{
    int i;
    if (name[0] != '.')
        return 0;
    if (suff_known(name))
        return 1;
    for (i = 0; i < suffixes.n; i++) {
        size_t l = strlen(suffixes.v[i]);
        if (strncmp(name, suffixes.v[i], l) == 0 && suff_known(name + l))
            return 2;
    }
    return 0;
}

void meta_add(const char *tgt, const StrList *srcs, const StrList *cmds)
{
    Meta *m;
    int i;
    for (m = metas; m != NULL; m = m->next) {
        if (strcmp(m->tgt, tgt) == 0) {
            sl_free(&m->srcs);
            sl_free(&m->cmds);
            break;
        }
    }
    if (m == NULL) {
        m = (Meta *)nm_malloc(sizeof(Meta));
        m->tgt = nm_strdup(tgt);
        m->next = NULL;
        *mtail = m;
        mtail = &m->next;
    }
    sl_init(&m->srcs);
    sl_init(&m->cmds);
    for (i = 0; i < srcs->n; i++)
        sl_add(&m->srcs, srcs->v[i]);
    for (i = 0; i < cmds->n; i++)
        sl_add(&m->cmds, cmds->v[i]);
}

static int find_src(const char *name, Cand *c, int depth);

static int src_ok(const char *src, int depth)
{
    Node *n;
    if (file_exists(src))
        return 1;
    n = node_lookup(src);
    if (n != NULL && n->is_target && (node_has_cmds(n) || n->rules != NULL))
        return 1;
    if (depth < 4) {
        Cand c;
        if (find_src(src, &c, depth + 1)) {
            sl_free(&c.srcs);
            free(c.prefix);
            return 1;
        }
    }
    return 0;
}

static char *subst_pct(const char *pat, const char *stem)
{
    Buf b;
    const char *p;
    buf_init(&b);
    for (p = pat; *p; p++) {
        if (*p == '%')
            buf_adds(&b, stem);
        else
            buf_addc(&b, *p);
    }
    return buf_take(&b);
}

static int try_meta(const char *name, Cand *c, int depth)
{
    Meta *m;
    size_t nl = strlen(name);
    for (m = metas; m != NULL; m = m->next) {
        const char *pct = strchr(m->tgt, '%');
        size_t pre, suf;
        char *stem;
        int i, ok;
        if (pct == NULL || m->cmds.n == 0)
            continue;
        pre = (size_t)(pct - m->tgt);
        suf = strlen(pct + 1);
        if (nl < pre + suf + 1 || strncmp(name, m->tgt, pre) != 0 || strcmp(name + nl - suf, pct + 1) != 0)
            continue;
        stem = nm_strndup(name + pre, nl - pre - suf);
        sl_init(&c->srcs);
        ok = 1;
        for (i = 0; i < m->srcs.n; i++) {
            char *s = subst_pct(m->srcs.v[i], stem);
            if (strcmp(s, name) == 0 || (i == 0 && !src_ok(s, depth))) {
                free(s);
                ok = 0;
                break;
            }
            sl_addown(&c->srcs, s);
        }
        if (ok) {
            c->cmds = &m->cmds;
            c->prefix = stem;
            return 1;
        }
        sl_free(&c->srcs);
        free(stem);
    }
    return 0;
}

static StrList *xform_cmds(const char *xname)
{
    Node *x = node_lookup(xname);
    Rule *r;
    int i;
    if (x == NULL)
        return NULL;
    for (r = x->rules; r != NULL; r = r->next) {
        for (i = 0; i < r->deps.n; i++)
            return NULL;
        if (r->cmds.n > 0)
            return &r->cmds;
    }
    return NULL;
}

static int try_suffix(const char *name, Cand *c, int depth)
{
    size_t nl = strlen(name);
    int i, j;
    for (i = 0; i < suffixes.n; i++) {
        const char *st = suffixes.v[i];
        size_t sl = strlen(st);
        char *base;
        if (nl <= sl || strcmp(name + nl - sl, st) != 0)
            continue;
        base = nm_strndup(name, nl - sl);
        for (j = 0; j < suffixes.n; j++) {
            Buf xb;
            StrList *cmds;
            char *src;
            buf_init(&xb);
            buf_adds(&xb, suffixes.v[j]);
            buf_adds(&xb, st);
            cmds = xform_cmds(xb.p);
            buf_free(&xb);
            if (cmds == NULL)
                continue;
            src = (char *)nm_malloc(strlen(base) + strlen(suffixes.v[j]) + 1);
            strcpy(src, base);
            strcat(src, suffixes.v[j]);
            if (strcmp(src, name) != 0 && src_ok(src, depth)) {
                sl_init(&c->srcs);
                sl_addown(&c->srcs, src);
                c->cmds = cmds;
                c->prefix = base;
                return 1;
            }
            free(src);
        }
        free(base);
    }
    for (j = 0; j < suffixes.n; j++) {
        StrList *cmds = xform_cmds(suffixes.v[j]);
        char *src;
        if (cmds == NULL)
            continue;
        src = (char *)nm_malloc(nl + strlen(suffixes.v[j]) + 1);
        strcpy(src, name);
        strcat(src, suffixes.v[j]);
        if (src_ok(src, depth)) {
            sl_init(&c->srcs);
            sl_addown(&c->srcs, src);
            c->cmds = cmds;
            c->prefix = nm_strdup(name);
            return 1;
        }
        free(src);
    }
    return 0;
}

static int find_src(const char *name, Cand *c, int depth)
{
    memset(c, 0, sizeof(Cand));
    if (try_meta(name, c, depth))
        return 1;
    return try_suffix(name, c, depth);
}

int node_infer(Node *n)
{
    Cand c;
    Rule *r;
    int i;
    if (!find_src(n->name, &c, 0))
        return 0;
    r = node_new_rule(n, 0);
    r->inferred = 1;
    r->cmds_set = 1;
    for (i = 0; i < c.cmds->n; i++)
        sl_add(&r->cmds, c.cmds->v[i]);
    for (i = 0; i < c.srcs.n; i++)
        nlist_add(&r->deps, node_find(c.srcs.v[i]));
    if (c.srcs.n > 0)
        n->impsrc = nm_strdup(c.srcs.v[0]);
    n->prefix = c.prefix;
    sl_free(&c.srcs);
    return 1;
}
