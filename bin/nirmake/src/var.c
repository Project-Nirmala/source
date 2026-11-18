#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fnmatch.h>
#include <regex.h>
#include "nirmake.h"

typedef struct Var {
    char *name;
    char *val;
    int lazy;
    int origin;
    int expanding;
    struct Var *next;
} Var;

#define VHASH 1021

static Var *vtab[VHASH];
static StrList exported;

static unsigned vhash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % VHASH;
}

static Var *vfind(const char *name)
{
    Var *v;
    for (v = vtab[vhash(name)]; v != NULL; v = v->next)
        if (strcmp(v->name, name) == 0)
            return v;
    return NULL;
}

static int rank(int org)
{
    switch (org) {
    case ORG_BUILTIN:
        return 0;
    case ORG_ENV:
        return opt.env_override ? 3 : 1;
    case ORG_FILE:
        return 2;
    case ORG_CMD:
        return 4;
    default:
        return 5;
    }
}

static void store(const char *name, const char *val, int org, int lazy)
{
    Var *v = vfind(name);
    if (v != NULL) {
        if (rank(org) < rank(v->origin))
            return;
        free(v->val);
    } else {
        unsigned h = vhash(name);
        v = (Var *)nm_malloc(sizeof(Var));
        v->name = nm_strdup(name);
        v->expanding = 0;
        v->next = vtab[h];
        vtab[h] = v;
    }
    v->val = nm_strdup(val);
    v->lazy = lazy;
    v->origin = org;
    if (org == ORG_CMD && !lazy)
        setenv(name, val, 1);
}

void var_set(const char *name, const char *val, int origin)
{
    store(name, val, origin, 1);
}

void var_setx(const char *name, const char *val, int origin)
{
    store(name, val, origin, 0);
}

void var_append(const char *name, const char *val, int origin)
{
    Var *v = vfind(name);
    Buf b;
    char *add;
    if (v == NULL) {
        store(name, val, origin, 1);
        return;
    }
    if (rank(origin) < rank(v->origin))
        return;
    buf_init(&b);
    buf_adds(&b, v->val);
    if (v->lazy) {
        if (v->val[0] != '\0' && val[0] != '\0')
            buf_addc(&b, ' ');
        buf_adds(&b, val);
    } else {
        add = var_expand(val);
        if (v->val[0] != '\0' && add[0] != '\0')
            buf_addc(&b, ' ');
        buf_adds(&b, add);
        free(add);
    }
    free(v->val);
    v->val = buf_take(&b);
}

void var_unset(const char *name)
{
    unsigned h = vhash(name);
    Var **pp = &vtab[h];
    while (*pp != NULL) {
        if (strcmp((*pp)->name, name) == 0) {
            Var *v = *pp;
            *pp = v->next;
            free(v->name);
            free(v->val);
            free(v);
            return;
        }
        pp = &(*pp)->next;
    }
}

int var_defined(const char *name)
{
    return vfind(name) != NULL;
}

char *var_get(const char *name)
{
    Var *v = vfind(name);
    char *r;
    if (v == NULL)
        return NULL;
    if (!v->lazy)
        return nm_strdup(v->val);
    if (v->expanding)
        nm_fatal("Variable %s is recursive", v->name);
    v->expanding = 1;
    r = var_expand(v->val);
    v->expanding = 0;
    return r;
}

void var_export(const char *name)
{
    char *val;
    if (!sl_has(&exported, name))
        sl_add(&exported, name);
    val = var_get(name);
    if (val != NULL) {
        setenv(name, val, 1);
        free(val);
    }
}

void var_export_all(void)
{
    int i;
    for (i = 0; i < exported.n; i++) {
        char *val = var_get(exported.v[i]);
        if (val != NULL) {
            setenv(exported.v[i], val, 1);
            free(val);
        }
    }
}

void var_init(char **envp)
{
    for (; envp != NULL && *envp != NULL; envp++) {
        const char *eq = strchr(*envp, '=');
        if (eq == NULL || eq == *envp)
            continue;
        {
            char *name = nm_strndup(*envp, (size_t)(eq - *envp));
            store(name, eq + 1, ORG_ENV, 0);
            free(name);
        }
    }
}

const char *find_close(const char *s, int open, int close)
{
    int depth = 1;
    while (*s) {
        if (*s == '\\' && s[1] != '\0') {
            s += 2;
            continue;
        }
        if (*s == open)
            depth++;
        else if (*s == close) {
            depth--;
            if (depth == 0)
                return s;
        }
        s++;
    }
    return NULL;
}

static char *join_words(StrList *l)
{
    char *r = sl_join(l, " ");
    sl_free(l);
    return r;
}

static char *map_words(const char *val, int mode)
{
    StrList in, out;
    int i;
    sl_init(&in);
    sl_init(&out);
    split_words(val, &in);
    for (i = 0; i < in.n; i++) {
        const char *w = in.v[i];
        Buf b;
        const char *dot;
        const char *base;
        buf_init(&b);
        switch (mode) {
        case 'H': {
            const char *slash = strrchr(w, '/');
            if (slash == NULL)
                buf_addc(&b, '.');
            else
                buf_addn(&b, w, (size_t)(slash - w));
            break;
        }
        case 'T':
            buf_adds(&b, nm_basename(w));
            break;
        case 'R':
            base = nm_basename(w);
            dot = strrchr(base, '.');
            if (dot != NULL && dot != base)
                buf_addn(&b, w, (size_t)(dot - w));
            else
                buf_adds(&b, w);
            break;
        case 'E':
            base = nm_basename(w);
            dot = strrchr(base, '.');
            if (dot != NULL && dot != base)
                buf_adds(&b, dot + 1);
            break;
        case 'l':
        case 'u': {
            const char *c;
            for (c = w; *c; c++)
                buf_addc(&b, mode == 'l' ? tolower((unsigned char)*c) : toupper((unsigned char)*c));
            break;
        }
        case 'Q':
        case 'q': {
            const char *c;
            for (c = w; *c; c++) {
                if (strchr(" \t\n!\"#$&'()*;<>?[\\]^`{|}~", *c) != NULL)
                    buf_addc(&b, '\\');
                buf_addc(&b, *c);
            }
            break;
        }
        default:
            buf_adds(&b, w);
            break;
        }
        if (mode == 'E' && b.len == 0)
            buf_free(&b);
        else
            sl_addown(&out, buf_take(&b));
    }
    sl_free(&in);
    return join_words(&out);
}

static char *quote_str(const char *val)
{
    Buf b;
    const char *c;
    buf_init(&b);
    for (c = val; *c; c++) {
        if (strchr(" \t\n!\"#$&'()*;<>?[\\]^`{|}~", *c) != NULL)
            buf_addc(&b, '\\');
        buf_addc(&b, *c);
    }
    return buf_take(&b);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int cmp_num(const void *a, const void *b)
{
    double x = atof(*(char *const *)a);
    double y = atof(*(char *const *)b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

static char *take_colon(const char **pm)
{
    const char *p = *pm;
    Buf b;
    buf_init(&b);
    while (*p) {
        if (*p == '\\' && p[1] == ':') {
            buf_addc(&b, ':');
            p += 2;
            continue;
        }
        if (*p == '\\' && p[1] != '\0') {
            buf_addc(&b, *p++);
            buf_addc(&b, *p++);
            continue;
        }
        if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
            const char *end = find_close(p + 2, p[1], p[1] == '(' ? ')' : '}');
            if (end != NULL) {
                buf_addn(&b, p, (size_t)(end - p + 1));
                p = end + 1;
                continue;
            }
        }
        if (*p == ':')
            break;
        buf_addc(&b, *p++);
    }
    *pm = p;
    return buf_take(&b);
}

static char *take_delim(const char **pm, int delim)
{
    const char *p = *pm;
    Buf b;
    buf_init(&b);
    while (*p) {
        if (*p == '\\' && p[1] == delim) {
            buf_addc(&b, delim);
            p += 2;
            continue;
        }
        if (*p == '\\' && p[1] != '\0') {
            buf_addc(&b, *p++);
            buf_addc(&b, *p++);
            continue;
        }
        if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
            const char *end = find_close(p + 2, p[1], p[1] == '(' ? ')' : '}');
            if (end != NULL) {
                buf_addn(&b, p, (size_t)(end - p + 1));
                p = end + 1;
                continue;
            }
        }
        if (*p == delim) {
            *pm = p + 1;
            return buf_take(&b);
        }
        buf_addc(&b, *p++);
    }
    buf_free(&b);
    *pm = p;
    return NULL;
}

static void add_rep(Buf *b, const char *rep, const char *match, size_t mlen)
{
    for (; *rep; rep++) {
        if (*rep == '\\' && rep[1] != '\0') {
            rep++;
            buf_addc(b, *rep);
        } else if (*rep == '&') {
            buf_addn(b, match, mlen);
        } else {
            buf_addc(b, *rep);
        }
    }
}

static char *sub_str(const char *w, const char *core, size_t clen, int astart, int aend,
                     const char *rep, int global, int *did)
{
    Buf b;
    const char *p = w;
    size_t wl = strlen(w);
    buf_init(&b);
    if (clen == 0) {
        if (astart) {
            add_rep(&b, rep, "", 0);
            buf_adds(&b, w);
            *did = 1;
        } else if (aend) {
            buf_adds(&b, w);
            add_rep(&b, rep, "", 0);
            *did = 1;
        } else {
            buf_adds(&b, w);
        }
        return buf_take(&b);
    }
    while (*p) {
        int match = 0;
        if (strncmp(p, core, clen) == 0) {
            match = 1;
            if (astart && p != w)
                match = 0;
            if (aend && p + clen != w + wl)
                match = 0;
        }
        if (match) {
            add_rep(&b, rep, p, clen);
            p += clen;
            *did = 1;
            if (!global) {
                buf_adds(&b, p);
                return buf_take(&b);
            }
        } else {
            buf_addc(&b, *p++);
        }
    }
    return buf_take(&b);
}

static char *mod_subst(const char *val, const char *pat, const char *rep, const char *flags)
{
    StrList in, out;
    int i, global = 0, once = 0, whole = 0, did = 0;
    size_t plen;
    int astart = 0, aend = 0;
    const char *core;
    char *corebuf;
    const char *f;

    for (f = flags; *f; f++) {
        if (*f == 'g')
            global = 1;
        else if (*f == '1')
            once = 1;
        else if (*f == 'W')
            whole = 1;
    }
    plen = strlen(pat);
    core = pat;
    if (plen > 0 && pat[0] == '^') {
        astart = 1;
        core++;
        plen--;
    }
    if (plen > 0 && core[plen - 1] == '$') {
        aend = 1;
        plen--;
    }
    corebuf = nm_strndup(core, plen);
    sl_init(&in);
    sl_init(&out);
    if (whole)
        sl_add(&in, val);
    else
        split_words(val, &in);
    for (i = 0; i < in.n; i++) {
        if (once && did)
            sl_add(&out, in.v[i]);
        else
            sl_addown(&out, sub_str(in.v[i], corebuf, plen, astart, aend, rep, global, &did));
    }
    free(corebuf);
    sl_free(&in);
    return join_words(&out);
}

static void add_rx_rep(Buf *b, const char *rep, const char *src, regmatch_t *m)
{
    for (; *rep; rep++) {
        if (*rep == '\\' && isdigit((unsigned char)rep[1])) {
            int g = rep[1] - '0';
            rep++;
            if (m[g].rm_so >= 0)
                buf_addn(b, src + m[g].rm_so, (size_t)(m[g].rm_eo - m[g].rm_so));
        } else if (*rep == '\\' && rep[1] != '\0') {
            rep++;
            buf_addc(b, *rep);
        } else if (*rep == '&') {
            buf_addn(b, src + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
        } else {
            buf_addc(b, *rep);
        }
    }
}

static char *rx_word(const char *w, regex_t *re, const char *rep, int global, int *did)
{
    Buf b;
    regmatch_t m[10];
    const char *p = w;
    int notbol = 0;
    buf_init(&b);
    for (;;) {
        if (regexec(re, p, 10, m, notbol ? REG_NOTBOL : 0) != 0) {
            buf_adds(&b, p);
            break;
        }
        buf_addn(&b, p, (size_t)m[0].rm_so);
        add_rx_rep(&b, rep, p, m);
        *did = 1;
        if (m[0].rm_eo == m[0].rm_so) {
            if (p[m[0].rm_eo] == '\0')
                break;
            buf_addc(&b, p[m[0].rm_eo]);
            p += m[0].rm_eo + 1;
        } else {
            p += m[0].rm_eo;
        }
        notbol = 1;
        if (!global) {
            buf_adds(&b, p);
            break;
        }
    }
    return buf_take(&b);
}

static char *mod_regex(const char *val, const char *pat, const char *rep, const char *flags)
{
    regex_t re;
    StrList in, out;
    int i, global = 0, once = 0, whole = 0, did = 0;
    const char *f;
    for (f = flags; *f; f++) {
        if (*f == 'g')
            global = 1;
        else if (*f == '1')
            once = 1;
        else if (*f == 'W')
            whole = 1;
    }
    if (regcomp(&re, pat, REG_EXTENDED) != 0) {
        nm_error("Bad regular expression `%s'", pat);
        return nm_strdup(val);
    }
    sl_init(&in);
    sl_init(&out);
    if (whole)
        sl_add(&in, val);
    else
        split_words(val, &in);
    for (i = 0; i < in.n; i++) {
        if (once && did)
            sl_add(&out, in.v[i]);
        else
            sl_addown(&out, rx_word(in.v[i], &re, rep, global, &did));
    }
    regfree(&re);
    sl_free(&in);
    return join_words(&out);
}

static char *mod_sysv(const char *val, const char *lhs, const char *rhs)
{
    StrList in, out;
    int i;
    const char *pct = strchr(lhs, '%');
    sl_init(&in);
    sl_init(&out);
    split_words(val, &in);
    for (i = 0; i < in.n; i++) {
        const char *w = in.v[i];
        size_t wl = strlen(w);
        Buf b;
        buf_init(&b);
        if (pct != NULL) {
            size_t pre = (size_t)(pct - lhs);
            size_t suf = strlen(pct + 1);
            if (wl >= pre + suf && strncmp(w, lhs, pre) == 0 && strcmp(w + wl - suf, pct + 1) == 0) {
                const char *rp = strchr(rhs, '%');
                if (rp != NULL) {
                    buf_addn(&b, rhs, (size_t)(rp - rhs));
                    buf_addn(&b, w + pre, wl - pre - suf);
                    buf_adds(&b, rp + 1);
                } else {
                    buf_adds(&b, rhs);
                }
            } else {
                buf_adds(&b, w);
            }
        } else {
            size_t ll = strlen(lhs);
            if (wl >= ll && strcmp(w + wl - ll, lhs) == 0) {
                buf_addn(&b, w, wl - ll);
                buf_adds(&b, rhs);
            } else {
                buf_adds(&b, w);
            }
        }
        sl_addown(&out, buf_take(&b));
    }
    sl_free(&in);
    return join_words(&out);
}

static int starts(const char *p, const char *kw)
{
    size_t n = strlen(kw);
    return strncmp(p, kw, n) == 0 && (p[n] == ':' || p[n] == '\0');
}

static char *maybe_expand(const char *s)
{
    return strchr(s, '$') != NULL ? var_expand(s) : nm_strdup(s);
}

static char *mod_range(const char *val, const char *spec)
{
    StrList in;
    char *r;
    int n;
    sl_init(&in);
    split_words(val, &in);
    if (strcmp(spec, "#") == 0) {
        char tmp[32];
        sprintf(tmp, "%d", in.n);
        r = nm_strdup(tmp);
    } else if (strcmp(spec, "*") == 0) {
        r = nm_strdup(val);
    } else if (strcmp(spec, "@") == 0) {
        r = sl_join(&in, " ");
    } else {
        n = atoi(spec);
        if (strcmp(spec, "$") == 0)
            n = in.n;
        if (n < 0)
            n = in.n + n + 1;
        if (n >= 1 && n <= in.n)
            r = nm_strdup(in.v[n - 1]);
        else
            r = nm_strdup("");
    }
    sl_free(&in);
    return r;
}

static char *apply_mods(char *val, int *defined, const char *name, const char *mods)
{
    const char *p = mods;
    while (*p) {
        char *nv = NULL;
        if (starts(p, "Q") || starts(p, "q")) {
            nv = quote_str(val);
            p++;
        } else if (starts(p, "H") || starts(p, "T") || starts(p, "R") || starts(p, "E")) {
            nv = map_words(val, *p);
            p++;
        } else if (starts(p, "tl") || starts(p, "tu")) {
            nv = map_words(val, p[1]);
            p += 2;
        } else if (p[0] == 'O' && strspn(p + 1, "xrn") == strcspn(p + 1, ":")) {
            StrList l;
            int rev = 0, num = 0, shuf = 0, i;
            const char *f;
            for (f = p + 1; *f && *f != ':'; f++) {
                if (*f == 'r')
                    rev = 1;
                else if (*f == 'n')
                    num = 1;
                else
                    shuf = 1;
            }
            sl_init(&l);
            split_words(val, &l);
            if (shuf) {
                for (i = l.n - 1; i > 0; i--) {
                    int k = rand() % (i + 1);
                    char *t = l.v[i];
                    l.v[i] = l.v[k];
                    l.v[k] = t;
                }
            } else {
                qsort(l.v, (size_t)l.n, sizeof(char *), num ? cmp_num : cmp_str);
                if (rev)
                    for (i = 0; i < l.n / 2; i++) {
                        char *t = l.v[i];
                        l.v[i] = l.v[l.n - 1 - i];
                        l.v[l.n - 1 - i] = t;
                    }
            }
            nv = join_words(&l);
            p = f;
        } else if (starts(p, "u")) {
            StrList in, out;
            int i;
            sl_init(&in);
            sl_init(&out);
            split_words(val, &in);
            for (i = 0; i < in.n; i++)
                if (i == 0 || strcmp(in.v[i], in.v[i - 1]) != 0)
                    sl_add(&out, in.v[i]);
            sl_free(&in);
            nv = join_words(&out);
            p++;
        } else if (starts(p, "L")) {
            nv = nm_strdup(name);
            *defined = 1;
            p++;
        } else if (starts(p, "sh")) {
            nv = run_capture(val, NULL);
            p += 2;
        } else if (starts(p, "ts") || (p[0] == 't' && p[1] == 's')) {
            StrList l;
            char sep[2];
            sep[0] = '\0';
            sep[1] = '\0';
            p += 2;
            if (p[0] == ':' && p[1] == '\0') {
                sep[0] = ':';
                p++;
            } else if (*p != ':' && *p != '\0') {
                if (*p == '\\' && p[1] == 'n') {
                    sep[0] = '\n';
                    p += 2;
                } else if (*p == '\\' && p[1] == 't') {
                    sep[0] = '\t';
                    p += 2;
                } else {
                    sep[0] = *p++;
                }
            }
            sl_init(&l);
            split_words(val, &l);
            nv = sl_join(&l, sep);
            sl_free(&l);
        } else if (*p == 'M' || *p == 'N') {
            int want = (*p == 'M');
            char *pat, *ep;
            StrList in, out;
            int i;
            p++;
            pat = take_colon(&p);
            ep = maybe_expand(pat);
            sl_init(&in);
            sl_init(&out);
            split_words(val, &in);
            for (i = 0; i < in.n; i++)
                if ((fnmatch(ep, in.v[i], 0) == 0) == want)
                    sl_add(&out, in.v[i]);
            sl_free(&in);
            free(pat);
            free(ep);
            nv = join_words(&out);
        } else if (*p == 'U' || *p == 'D') {
            int isu = (*p == 'U');
            char *arg;
            p++;
            arg = take_colon(&p);
            if ((isu && !*defined) || (!isu && *defined)) {
                nv = var_expand(arg);
                *defined = 1;
            } else {
                nv = nm_strdup(isu ? val : "");
            }
            free(arg);
        } else if (*p == 'S' || *p == 'C') {
            int isS = (*p == 'S');
            int delim;
            char *pat, *rep, *fl, *ep, *er;
            const char *fs;
            p++;
            delim = *p;
            if (delim == '\0' || delim == ':') {
                nm_error("Bad modifier `:%s' for %s", mods, name);
                return val;
            }
            p++;
            pat = take_delim(&p, delim);
            rep = pat ? take_delim(&p, delim) : NULL;
            if (pat == NULL || rep == NULL) {
                nm_error("Unfinished modifier for %s", name);
                free(pat);
                return val;
            }
            fs = p;
            while (*p && *p != ':')
                p++;
            fl = nm_strndup(fs, (size_t)(p - fs));
            ep = maybe_expand(pat);
            er = maybe_expand(rep);
            nv = isS ? mod_subst(val, ep, er, fl) : mod_regex(val, ep, er, fl);
            free(pat);
            free(rep);
            free(fl);
            free(ep);
            free(er);
        } else if (*p == '@') {
            char *var, *body;
            StrList in, out;
            int i;
            p++;
            var = take_delim(&p, '@');
            body = var ? take_delim(&p, '@') : NULL;
            if (var == NULL || body == NULL) {
                nm_error("Unfinished :@ modifier for %s", name);
                free(var);
                return val;
            }
            sl_init(&in);
            sl_init(&out);
            split_words(val, &in);
            for (i = 0; i < in.n; i++) {
                var_setx(var, in.v[i], ORG_LOCAL);
                sl_addown(&out, var_expand(body));
            }
            var_unset(var);
            sl_free(&in);
            free(var);
            free(body);
            nv = join_words(&out);
        } else if (*p == '!') {
            char *cmd, *ec;
            p++;
            cmd = take_delim(&p, '!');
            if (cmd == NULL) {
                nm_error("Unfinished :! modifier for %s", name);
                return val;
            }
            ec = maybe_expand(cmd);
            nv = run_capture(ec, NULL);
            free(cmd);
            free(ec);
        } else if (*p == '[') {
            char *spec;
            p++;
            spec = take_delim(&p, ']');
            if (spec == NULL) {
                nm_error("Unfinished :[ modifier for %s", name);
                return val;
            }
            nv = mod_range(val, spec);
            free(spec);
        } else {
            char *raw = take_colon(&p);
            char *eq = strchr(raw, '=');
            if (eq == NULL) {
                nm_error("Bad modifier `:%s' for %s", raw, name);
                free(raw);
                return val;
            }
            *eq = '\0';
            {
                char *lhs = maybe_expand(raw);
                char *rhs = maybe_expand(eq + 1);
                nv = mod_sysv(val, lhs, rhs);
                free(lhs);
                free(rhs);
            }
            free(raw);
        }
        free(val);
        val = nv;
        if (*p == ':')
            p++;
        else if (*p != '\0') {
            nm_error("Bad modifier `:%s' for %s", p, name);
            break;
        }
    }
    return val;
}

static char *get_value(const char *name, int *defined)
{
    char *v = var_get(name);
    size_t n = strlen(name);
    if (v != NULL) {
        *defined = 1;
        return v;
    }
    if (n == 2 && (name[1] == 'D' || name[1] == 'F') && strchr("@<*?^>%!", name[0]) != NULL) {
        char base[2];
        char *bv;
        base[0] = name[0];
        base[1] = '\0';
        bv = var_get(base);
        if (bv != NULL) {
            char *r = map_words(bv, name[1] == 'D' ? 'H' : 'T');
            free(bv);
            *defined = 1;
            return r;
        }
    }
    *defined = 0;
    return nm_strdup("");
}

static void eval_spec(Buf *out, const char *spec)
{
    int depth = 0;
    size_t i;
    char *name, *val;
    int defined;
    for (i = 0; spec[i]; i++) {
        char c = spec[i];
        if (c == '(' || c == '{')
            depth++;
        else if ((c == ')' || c == '}') && depth > 0)
            depth--;
        else if (c == ':' && depth == 0)
            break;
    }
    name = nm_strndup(spec, i);
    if (strchr(name, '$') != NULL) {
        char *e = var_expand(name);
        free(name);
        name = e;
    }
    val = get_value(name, &defined);
    if (spec[i] == ':')
        val = apply_mods(val, &defined, name, spec + i + 1);
    buf_adds(out, val);
    free(val);
    free(name);
}

char *var_expand(const char *s)
{
    Buf out;
    buf_init(&out);
    while (*s) {
        if (*s != '$') {
            buf_addc(&out, *s++);
            continue;
        }
        s++;
        if (*s == '\0') {
            buf_addc(&out, '$');
            break;
        }
        if (*s == '$') {
            buf_addc(&out, '$');
            s++;
            continue;
        }
        if (*s == '(' || *s == '{') {
            int open = *s;
            const char *end = find_close(s + 1, open, open == '(' ? ')' : '}');
            char *spec;
            if (end == NULL) {
                nm_error("Unclosed variable specification");
                break;
            }
            spec = nm_strndup(s + 1, (size_t)(end - s - 1));
            eval_spec(&out, spec);
            free(spec);
            s = end + 1;
        } else {
            char nm[2];
            nm[0] = *s++;
            nm[1] = '\0';
            eval_spec(&out, nm);
        }
    }
    return buf_take(&out);
}
