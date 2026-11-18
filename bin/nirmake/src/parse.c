#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "nirmake.h"

#define MAX_COND 256
#define MAX_INCLUDE 64

typedef struct Lines {
    char **v;
    int *no;
    int n;
    int cap;
} Lines;

typedef struct Cond {
    int active;
    int taken;
    int parent;
} Cond;

typedef struct Cur {
    Rule *r;
    int started;
    int drop;
} Cur;

static Cond cstack[MAX_COND];
static int cdepth = 0;
static int cur_org = ORG_FILE;
static int cur_builtin = 0;
static int include_depth = 0;

static Cur *curv = NULL;
static int ncur = 0;
static int capcur = 0;
static int ignore_ctx = 0;
static int meta_on = 0;
static StrList meta_tgts;
static StrList meta_srcs;
static StrList meta_cmds;

static void parse_lines(char **lines, int *nos, int n, const char *fname);

static int cond_active(void)
{
    return cdepth == 0 || cstack[cdepth - 1].active;
}

static void lines_add(Lines *L, char *s, int no)
{
    if (L->n + 1 > L->cap) {
        L->cap = L->cap ? L->cap * 2 : 64;
        L->v = (char **)nm_realloc(L->v, (size_t)L->cap * sizeof(char *));
        L->no = (int *)nm_realloc(L->no, (size_t)L->cap * sizeof(int));
    }
    L->v[L->n] = s;
    L->no[L->n] = no;
    L->n++;
}

static void lines_free(Lines *L)
{
    int i;
    for (i = 0; i < L->n; i++)
        free(L->v[i]);
    free(L->v);
    free(L->no);
    L->v = NULL;
    L->no = NULL;
    L->n = L->cap = 0;
}

static void split_lines(const char *text, Lines *L)
{
    const char *p = text;
    int lineno = 1;
    while (*p) {
        int start = lineno;
        int strip_tab = 0;
        Buf b;
        buf_init(&b);
        for (;;) {
            const char *e = strchr(p, '\n');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            int bs = 0;
            size_t k;
            if (strip_tab && *p == '\t') {
                p++;
                len = e ? (size_t)(e - p) : strlen(p);
            }
            strip_tab = 0;
            if (len > 0 && p[len - 1] == '\r')
                len--;
            buf_addn(&b, p, len);
            p = e ? e + 1 : p + strlen(p);
            lineno++;
            k = b.len;
            while (k > 0 && b.p[k - 1] == '\\') {
                bs++;
                k--;
            }
            if ((bs & 1) && *p != '\0') {
                if (b.p[0] == '\t') {
                    buf_addc(&b, '\n');
                    strip_tab = 1;
                } else {
                    b.len--;
                    b.p[b.len] = '\0';
                    buf_addc(&b, ' ');
                    p = skip_ws(p);
                }
                continue;
            }
            break;
        }
        lines_add(L, buf_take(&b), start);
    }
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    Buf b;
    char chunk[4096];
    size_t n;
    if (fp == NULL)
        return NULL;
    buf_init(&b);
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0)
        buf_addn(&b, chunk, n);
    fclose(fp);
    return buf_take(&b);
}

static void strip_comment(char *s)
{
    char *r = s;
    char *w = s;
    while (*r) {
        if (*r == '\\' && r[1] == '#') {
            *w++ = '#';
            r += 2;
            continue;
        }
        if (*r == '$' && (r[1] == '(' || r[1] == '{')) {
            const char *end = find_close(r + 2, r[1], r[1] == '(' ? ')' : '}');
            if (end != NULL) {
                while (r <= end)
                    *w++ = *r++;
                continue;
            }
        }
        if (*r == '#')
            break;
        *w++ = *r++;
    }
    *w = '\0';
}

static const char *find_outside(const char *s, int ch)
{
    while (*s) {
        if (*s == '$' && (s[1] == '(' || s[1] == '{')) {
            const char *end = find_close(s + 2, s[1], s[1] == '(' ? ')' : '}');
            if (end != NULL) {
                s = end + 1;
                continue;
            }
        }
        if (*s == ch)
            return s;
        s++;
    }
    return NULL;
}

static void flush_ctx(void)
{
    int i;
    if (meta_on) {
        for (i = 0; i < meta_tgts.n; i++)
            meta_add(meta_tgts.v[i], &meta_srcs, &meta_cmds);
        sl_free(&meta_tgts);
        sl_free(&meta_srcs);
        sl_free(&meta_cmds);
        meta_on = 0;
    }
    ncur = 0;
    ignore_ctx = 0;
}

static void cur_push(Rule *r)
{
    if (ncur + 1 > capcur) {
        capcur = capcur ? capcur * 2 : 8;
        curv = (Cur *)nm_realloc(curv, (size_t)capcur * sizeof(Cur));
    }
    curv[ncur].r = r;
    curv[ncur].started = 0;
    curv[ncur].drop = 0;
    ncur++;
}

static void add_command(const char *line)
{
    const char *c = skip_ws(line);
    int i;
    if (*c == '\0')
        return;
    if (ncur == 0 && !meta_on && !ignore_ctx) {
        nm_error("Unassociated shell command \"%s\"", c);
        return;
    }
    if (meta_on)
        sl_add(&meta_cmds, c);
    for (i = 0; i < ncur; i++) {
        Rule *r = curv[i].r;
        if (r == NULL)
            continue;
        if (!curv[i].started) {
            curv[i].started = 1;
            if (r->cmds_set) {
                if (r->builtin) {
                    sl_free(&r->cmds);
                    r->builtin = 0;
                } else {
                    nm_warn("duplicate script for target ignored");
                    curv[i].drop = 1;
                }
            }
            r->cmds_set = 1;
        }
        if (!curv[i].drop)
            sl_add(&r->cmds, c);
    }
}

static int is_attr(const char *s)
{
    static const char *attrs[] = {".USE", ".EXEC", ".MADE", ".MAKE", ".NOTMAIN", ".OPTIONAL", ".RECURSIVE",
                                  ".INVISIBLE", ".JOIN", ".NOPATH", ".WAIT", ".NOMETA", ".NOMETA_CMP",
                                  ".META", ".SPECIAL", ".EXPORTSAME", NULL};
    int i;
    for (i = 0; attrs[i] != NULL; i++)
        if (strcmp(s, attrs[i]) == 0)
            return 1;
    return 0;
}

static int ignored_special(const char *t)
{
    static const char *names[] = {".ORDER", ".NOTPARALLEL", ".NOMAIN", ".NOPATH", ".WAIT", ".SHELL",
                                  ".OBJDIR", ".OBJDIRS", ".INCLUDES", ".LIBS", ".NULL", ".STALE",
                                  ".PARALLEL", ".ERROR", ".INTERRUPT", ".DELETE_ON_ERROR", NULL};
    int i;
    for (i = 0; names[i] != NULL; i++)
        if (strcmp(t, names[i]) == 0)
            return 1;
    if (strncmp(t, ".PATH", 5) == 0 || strncmp(t, ".MAKE", 5) == 0)
        return 1;
    return 0;
}

static void mark_nodes(const StrList *srcs, int what)
{
    int i;
    for (i = 0; i < srcs->n; i++) {
        Node *n = node_find(srcs->v[i]);
        switch (what) {
        case 0:
            n->phony = 1;
            n->is_target = 1;
            break;
        case 1:
            n->silent = 1;
            break;
        case 2:
            n->ignore = 1;
            break;
        default:
            n->precious = 1;
            break;
        }
    }
}

static void handle_dep(const char *line, size_t pos, size_t oplen, int dcolon, int bang)
{
    char *left = nm_strndup(line, pos);
    const char *right = line + pos + oplen;
    const char *semi = find_outside(right, ';');
    char *sraw = semi ? nm_strndup(right, (size_t)(semi - right)) : nm_strdup(right);
    char *el = var_expand(left);
    char *es = var_expand(sraw);
    StrList tg, sr;
    int i, j;

    sl_init(&tg);
    sl_init(&sr);
    split_words(el, &tg);
    split_words(es, &sr);
    free(left);
    free(sraw);
    free(el);
    free(es);
    flush_ctx();
    if (tg.n == 0) {
        sl_free(&tg);
        sl_free(&sr);
        return;
    }
    for (i = 0; i < tg.n; i++) {
        const char *t = tg.v[i];
        Node *n;
        Rule *r = NULL;
        if (strchr(t, '%') != NULL) {
            if (!meta_on) {
                meta_on = 1;
                sl_init(&meta_tgts);
                sl_init(&meta_srcs);
                sl_init(&meta_cmds);
                for (j = 0; j < sr.n; j++)
                    sl_add(&meta_srcs, sr.v[j]);
            }
            sl_add(&meta_tgts, t);
            continue;
        }
        if (strcmp(t, ".PHONY") == 0) {
            mark_nodes(&sr, 0);
            ignore_ctx = 1;
            continue;
        }
        if (strcmp(t, ".SILENT") == 0) {
            if (sr.n == 0)
                global_silent = 1;
            else
                mark_nodes(&sr, 1);
            ignore_ctx = 1;
            continue;
        }
        if (strcmp(t, ".IGNORE") == 0) {
            if (sr.n == 0)
                global_ignore = 1;
            else
                mark_nodes(&sr, 2);
            ignore_ctx = 1;
            continue;
        }
        if (strcmp(t, ".PRECIOUS") == 0) {
            if (sr.n == 0)
                global_precious = 1;
            else
                mark_nodes(&sr, 3);
            ignore_ctx = 1;
            continue;
        }
        if (strcmp(t, ".SUFFIXES") == 0) {
            if (sr.n == 0)
                suff_clear();
            for (j = 0; j < sr.n; j++)
                suff_add(sr.v[j]);
            ignore_ctx = 1;
            continue;
        }
        if (strcmp(t, ".MAIN") == 0) {
            if (sr.n > 0)
                default_goal = node_find(sr.v[0]);
            ignore_ctx = 1;
            continue;
        }
        if (strcmp(t, ".MAKEFLAGS") == 0 || strcmp(t, ".MFLAGS") == 0) {
            char *fl = sl_join(&sr, " ");
            opt_apply(fl);
            free(fl);
            ignore_ctx = 1;
            continue;
        }
        if (ignored_special(t)) {
            ignore_ctx = 1;
            continue;
        }
        n = node_find(t);
        n->is_target = 1;
        if (default_goal == NULL && !(t[0] == '.' && strchr(t, '/') == NULL))
            default_goal = n;
        if (dcolon) {
            r = node_new_rule(n, 1);
        } else {
            Rule *q;
            for (q = n->rules; q != NULL; q = q->next)
                if (!q->dcolon && !q->inferred) {
                    r = q;
                    break;
                }
            if (r == NULL)
                r = node_new_rule(n, 0);
        }
        if (bang)
            r->bang = 1;
        if (cur_builtin && !r->cmds_set)
            r->builtin = 1;
        for (j = 0; j < sr.n; j++) {
            const char *s = sr.v[j];
            if (strcmp(s, ".PHONY") == 0)
                n->phony = 1;
            else if (strcmp(s, ".SILENT") == 0)
                n->silent = 1;
            else if (strcmp(s, ".IGNORE") == 0)
                n->ignore = 1;
            else if (strcmp(s, ".PRECIOUS") == 0)
                n->precious = 1;
            else if (is_attr(s))
                continue;
            else
                nlist_add(&r->deps, node_find(s));
        }
        cur_push(r);
    }
    if (semi != NULL)
        add_command(semi + 1);
    sl_free(&tg);
    sl_free(&sr);
}

static void handle_assign(const char *line, size_t name_end, size_t val_start, int op)
{
    char *raw = nm_strndup(line, name_end);
    char *name = NULL;
    char *val;
    char *tmp;
    char *e;
    size_t n;

    tmp = str_trim(raw);
    name = strchr(tmp, '$') ? var_expand(tmp) : nm_strdup(tmp);
    free(raw);
    val = nm_strdup(skip_ws(line + val_start));
    n = strlen(val);
    while (n > 0 && (val[n - 1] == ' ' || val[n - 1] == '\t'))
        val[--n] = '\0';
    flush_ctx();
    {
        char *nn = str_trim(name);
        if (nn != name) {
            char *c = nm_strdup(nn);
            free(name);
            name = c;
        }
    }
    switch (op) {
    case 0:
        var_set(name, val, cur_org);
        break;
    case ':':
        e = var_expand(val);
        var_setx(name, e, cur_org);
        free(e);
        break;
    case '?':
        if (!var_defined(name))
            var_set(name, val, cur_org);
        break;
    case '+':
        var_append(name, val, cur_org);
        break;
    case '!':
        e = var_expand(val);
        tmp = run_capture(e, NULL);
        var_setx(name, tmp, cur_org);
        free(tmp);
        free(e);
        break;
    default:
        break;
    }
    free(name);
    free(val);
}

static int classify(const char *s)
{
    size_t i;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (c == '$' && (s[i + 1] == '(' || s[i + 1] == '{')) {
            const char *end = find_close(s + i + 2, s[i + 1], s[i + 1] == '(' ? ')' : '}');
            if (end != NULL) {
                i = (size_t)(end - s);
                continue;
            }
        }
        if (c == '\\' && s[i + 1] != '\0') {
            i++;
            continue;
        }
        if (c == '=') {
            if (i > 0 && (s[i - 1] == '+' || s[i - 1] == '?')) {
                handle_assign(s, i - 1, i + 1, s[i - 1]);
            } else if (i > 1 && s[i - 1] == ':' && s[i - 2] == ':') {
                handle_assign(s, i - 2, i + 1, ':');
            } else if (i > 0 && (s[i - 1] == ':' || s[i - 1] == '!')) {
                handle_assign(s, i - 1, i + 1, s[i - 1]);
            } else {
                handle_assign(s, i, i + 1, 0);
            }
            return 1;
        }
        if (c == ':') {
            if (s[i + 1] == '=') {
                handle_assign(s, i, i + 2, ':');
                return 1;
            }
            if (s[i + 1] == ':' && s[i + 2] == '=') {
                handle_assign(s, i, i + 3, ':');
                return 1;
            }
            if (s[i + 1] == ':')
                handle_dep(s, i, 2, 1, 0);
            else
                handle_dep(s, i, 1, 0, 0);
            return 1;
        }
        if (c == '!') {
            if (s[i + 1] == '=') {
                handle_assign(s, i, i + 2, '!');
            } else {
                handle_dep(s, i, 1, 0, 1);
            }
            return 1;
        }
    }
    return 0;
}

static int kw_match(const char *p, const char *kw, const char **rest)
{
    size_t n = strlen(kw);
    if (strncmp(p, kw, n) != 0)
        return 0;
    if (isalnum((unsigned char)p[n]) || p[n] == '_' || p[n] == '-')
        return 0;
    *rest = skip_ws(p + n);
    return 1;
}

static int eval_cond_kw(const char *kw, const char *arg)
{
    int r = 0;
    if (strcmp(kw, "if") == 0 || strcmp(kw, "elif") == 0) {
        if (cond_eval(arg, &r) < 0) {
            nm_error("Malformed conditional (%s)", arg);
            r = 0;
        }
    } else if (strcmp(kw, "ifdef") == 0 || strcmp(kw, "elifdef") == 0 || strcmp(kw, "ifndef") == 0 ||
               strcmp(kw, "elifndef") == 0) {
        char *e = var_expand(arg);
        char *w = str_trim(e);
        char *sp = strpbrk(w, " \t");
        if (sp != NULL)
            *sp = '\0';
        r = var_defined(w);
        free(e);
        if (strstr(kw, "ndef") != NULL)
            r = !r;
    } else {
        char *e = var_expand(arg);
        char *w = str_trim(e);
        r = sl_has(&goals, w);
        free(e);
        if (strcmp(kw, "ifnmake") == 0)
            r = !r;
    }
    return r;
}

static void do_include(const char *arg, int silent)
{
    char *e = var_expand(arg);
    char *s = str_trim(e);
    int sys = 0;
    size_t n;
    char *name;
    Buf path;
    int i;
    char *dir;

    if (*s == '"' || *s == '<') {
        sys = (*s == '<');
        s++;
        n = strcspn(s, sys ? ">" : "\"");
        name = nm_strndup(s, n);
    } else {
        name = nm_strdup(s);
    }
    if (include_depth >= MAX_INCLUDE) {
        nm_error("include nested too deeply: %s", name);
        free(name);
        free(e);
        return;
    }
    if (name[0] == '/') {
        if (file_exists(name)) {
            parse_file(name, 1);
            free(name);
            free(e);
            return;
        }
    } else {
        if (!sys) {
            dir = nm_dirname(nm_file ? nm_file : ".");
            buf_init(&path);
            buf_adds(&path, dir);
            buf_addc(&path, '/');
            buf_adds(&path, name);
            free(dir);
            if (file_exists(path.p)) {
                parse_file(path.p, 1);
                buf_free(&path);
                free(name);
                free(e);
                return;
            }
            buf_free(&path);
        }
        for (i = 0; i < opt.incdirs.n; i++) {
            buf_init(&path);
            buf_adds(&path, opt.incdirs.v[i]);
            buf_addc(&path, '/');
            buf_adds(&path, name);
            if (file_exists(path.p)) {
                parse_file(path.p, 1);
                buf_free(&path);
                free(name);
                free(e);
                return;
            }
            buf_free(&path);
        }
        if (file_exists(name)) {
            parse_file(name, 1);
            free(name);
            free(e);
            return;
        }
        buf_init(&path);
        buf_adds(&path, "/usr/share/mk/");
        buf_adds(&path, name);
        if (file_exists(path.p)) {
            parse_file(path.p, 1);
            buf_free(&path);
            free(name);
            free(e);
            return;
        }
        buf_free(&path);
    }
    if (!silent)
        nm_error("Could not find %s", name);
    free(name);
    free(e);
}

static char *for_subst(const char *line, const StrList *vars, const StrList *vals, int base)
{
    Buf b;
    const char *p = line;
    int k;
    buf_init(&b);
    while (*p) {
        int hit = 0;
        if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
            for (k = 0; k < vars->n; k++) {
                size_t l = strlen(vars->v[k]);
                char endc = p[1] == '(' ? ')' : '}';
                if (strncmp(p + 2, vars->v[k], l) == 0 && (p[2 + l] == endc || p[2 + l] == ':')) {
                    const char *v = vals->v[base + k];
                    buf_addc(&b, '$');
                    buf_addc(&b, p[1]);
                    buf_addc(&b, ':');
                    buf_addc(&b, 'U');
                    for (; *v; v++) {
                        if (*v == ':' || *v == '\\')
                            buf_addc(&b, '\\');
                        buf_addc(&b, *v);
                    }
                    p += 2 + l;
                    hit = 1;
                    break;
                }
            }
        }
        if (!hit)
            buf_addc(&b, *p++);
    }
    return buf_take(&b);
}

static int is_for_kw(const char *line)
{
    const char *p = line;
    const char *rest;
    if (*p != '.')
        return 0;
    p = skip_ws(p + 1);
    return kw_match(p, "for", &rest);
}

static int is_endfor_kw(const char *line)
{
    const char *p = line;
    const char *rest;
    if (*p != '.')
        return 0;
    p = skip_ws(p + 1);
    return kw_match(p, "endfor", &rest);
}

static int find_endfor(char **lines, int n, int from)
{
    int depth = 1;
    int j;
    for (j = from; j < n; j++) {
        if (lines[j][0] == '\t')
            continue;
        if (is_for_kw(lines[j]))
            depth++;
        else if (is_endfor_kw(lines[j])) {
            depth--;
            if (depth == 0)
                return j;
        }
    }
    return -1;
}

static void run_for(const char *arg, char **lines, int *nos, int from, int to, const char *fname)
{
    StrList vars, vals, words;
    char *copy = nm_strdup(arg);
    char *in = strstr(copy, " in ");
    char *listexp;
    int i, k, nv, iter, body;
    char **nl;
    int *nn;

    sl_init(&vars);
    sl_init(&vals);
    sl_init(&words);
    if (in == NULL) {
        nm_error("Malformed .for directive");
        free(copy);
        return;
    }
    *in = '\0';
    split_words(copy, &vars);
    listexp = var_expand(in + 4);
    split_words(listexp, &vals);
    free(listexp);
    nv = vars.n;
    if (nv == 0) {
        nm_error("Malformed .for directive");
        free(copy);
        return;
    }
    body = to - from;
    nl = (char **)nm_malloc((size_t)(body > 0 ? body : 1) * sizeof(char *));
    nn = (int *)nm_malloc((size_t)(body > 0 ? body : 1) * sizeof(int));
    for (iter = 0; iter + nv <= vals.n; iter += nv) {
        for (i = 0; i < body; i++) {
            nl[i] = for_subst(lines[from + i], &vars, &vals, iter);
            nn[i] = nos[from + i];
        }
        parse_lines(nl, nn, body, fname);
        for (k = 0; k < body; k++)
            free(nl[k]);
    }
    free(nl);
    free(nn);
    free(copy);
    sl_free(&vars);
    sl_free(&vals);
    sl_free(&words);
}

static void handle_directive(const char *kw, const char *arg)
{
    int i;
    StrList w;
    char *e;

    if (strcmp(kw, "include") == 0) {
        do_include(arg, 0);
    } else if (strcmp(kw, "sinclude") == 0 || strcmp(kw, "-include") == 0) {
        do_include(arg, 1);
    } else if (strcmp(kw, "undef") == 0) {
        sl_init(&w);
        e = var_expand(arg);
        split_words(e, &w);
        for (i = 0; i < w.n; i++)
            var_unset(w.v[i]);
        free(e);
        sl_free(&w);
    } else if (strcmp(kw, "export") == 0 || strcmp(kw, "export-env") == 0) {
        sl_init(&w);
        e = var_expand(arg);
        split_words(e, &w);
        for (i = 0; i < w.n; i++)
            var_export(w.v[i]);
        free(e);
        sl_free(&w);
    } else if (strcmp(kw, "unexport") == 0 || strcmp(kw, "unexport-env") == 0) {
        sl_init(&w);
        e = var_expand(arg);
        split_words(e, &w);
        for (i = 0; i < w.n; i++)
            unsetenv(w.v[i]);
        free(e);
        sl_free(&w);
    } else if (strcmp(kw, "error") == 0) {
        e = var_expand(arg);
        nm_error("%s", e);
        exit(1);
    } else if (strcmp(kw, "warning") == 0) {
        e = var_expand(arg);
        nm_warn("%s", e);
        free(e);
    } else if (strcmp(kw, "info") == 0) {
        e = var_expand(arg);
        nm_info("%s", e);
        free(e);
    }
}

static int directive_of(const char *p, const char **kw, const char **arg)
{
    static const char *names[] = {"ifndef", "ifdef", "ifnmake", "ifmake", "if", "elifndef", "elifdef", "elif",
                                  "else", "endif", "include", "sinclude", "-include", "undef", "export-env",
                                  "export", "unexport-env", "unexport", "error", "warning", "info", "for",
                                  "endfor", NULL};
    int i;
    const char *q;
    if (*p == '.') {
        q = skip_ws(p + 1);
        for (i = 0; names[i] != NULL; i++) {
            if (kw_match(q, names[i], arg)) {
                *kw = names[i];
                return 1;
            }
        }
        return 0;
    }
    if (kw_match(p, "include", arg) || kw_match(p, "sinclude", arg) || kw_match(p, "-include", arg)) {
        if (**arg == '=')
            return 0;
        *kw = (p[0] == 'i') ? "include" : (p[0] == 's' ? "sinclude" : "-include");
        return 1;
    }
    return 0;
}

static void parse_lines(char **lines, int *nos, int n, const char *fname)
{
    int i;
    const char *save_file = nm_file;
    int save_line = nm_line;
    int base_depth = cdepth;

    nm_file = fname;
    for (i = 0; i < n; i++) {
        char *copy;
        const char *p, *kw, *arg;
        nm_line = nos[i];
        if (lines[i][0] == '\t') {
            if (cond_active())
                add_command(lines[i] + 1);
            continue;
        }
        copy = nm_strdup(lines[i]);
        strip_comment(copy);
        p = skip_ws(copy);
        if (*p == '\0') {
            free(copy);
            continue;
        }
        if (directive_of(p, &kw, &arg)) {
            if (strcmp(kw, "for") == 0) {
                int end = find_endfor(lines, n, i + 1);
                if (end < 0) {
                    nm_error("for-less endfor / unterminated .for");
                    free(copy);
                    break;
                }
                if (cond_active())
                    run_for(arg, lines, nos, i + 1, end, fname);
                i = end;
                nm_file = fname;
                free(copy);
                continue;
            }
            if (strcmp(kw, "endfor") == 0) {
                nm_error("for-less endfor");
            } else if (strncmp(kw, "if", 2) == 0) {
                int parent = cond_active();
                int r = 0;
                if (parent)
                    r = eval_cond_kw(kw, arg);
                if (cdepth >= MAX_COND)
                    nm_fatal("conditionals nested too deeply");
                cstack[cdepth].parent = parent;
                cstack[cdepth].active = parent && r;
                cstack[cdepth].taken = r;
                cdepth++;
            } else if (strncmp(kw, "elif", 4) == 0) {
                if (cdepth == 0) {
                    nm_error("if-less %s", kw);
                } else {
                    Cond *c = &cstack[cdepth - 1];
                    if (c->parent) {
                        if (c->taken) {
                            c->active = 0;
                        } else {
                            int r = eval_cond_kw(kw, arg);
                            c->active = r;
                            c->taken = r;
                        }
                    }
                }
            } else if (strcmp(kw, "else") == 0) {
                if (cdepth == 0) {
                    nm_error("if-less else");
                } else {
                    Cond *c = &cstack[cdepth - 1];
                    c->active = c->parent && !c->taken;
                    c->taken = 1;
                }
            } else if (strcmp(kw, "endif") == 0) {
                if (cdepth == 0)
                    nm_error("if-less endif");
                else
                    cdepth--;
            } else if (cond_active()) {
                handle_directive(kw, arg);
            }
            free(copy);
            continue;
        }
        if (cond_active())
            classify(p);
        free(copy);
    }
    if (cdepth > base_depth) {
        nm_error("Unexpected end of file in conditional");
        cdepth = base_depth;
    }
    nm_file = save_file;
    nm_line = save_line;
}

void parse_string(const char *text, const char *name, int origin_builtin)
{
    Lines L;
    int so = cur_org;
    int sb = cur_builtin;
    memset(&L, 0, sizeof(L));
    if (origin_builtin) {
        cur_org = ORG_BUILTIN;
        cur_builtin = 1;
    }
    split_lines(text, &L);
    parse_lines(L.v, L.no, L.n, name);
    flush_ctx();
    cur_org = so;
    cur_builtin = sb;
    lines_free(&L);
}

void parse_file(const char *path, int must_exist)
{
    char *text = read_file(path);
    Lines L;
    char *name;
    if (text == NULL) {
        if (must_exist)
            nm_error("Could not open %s", path);
        return;
    }
    memset(&L, 0, sizeof(L));
    split_lines(text, &L);
    name = nm_strdup(path);
    include_depth++;
    parse_lines(L.v, L.no, L.n, name);
    include_depth--;
    lines_free(&L);
    free(name);
    free(text);
}

void parse_finish(void)
{
    flush_ctx();
}
