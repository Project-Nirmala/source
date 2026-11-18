#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nc.h"

#define MAXTOK 128
#define MAXBLK 64

enum { B_MENU, B_IF };
enum { ENT_NONE, ENT_MENU, ENT_SYM, ENT_COMMENT };

struct blk {
    int kind;
    struct nc_expr *dep;
    struct nc_node *cur;
};

static struct nc_node *root;
static struct nc_node *cur;
static struct nc_node *cmenu;
static struct nc_expr *dep;
static struct nc_sym *csym;
static int entry;
static struct blk stk[MAXBLK];
static int depth;
static const char *cfile;
static int cline;
static struct nc_tok *tk;
static int ti;

static void parse_file(const char *path);

static void err(const char *msg)
{
    nc_die("%s:%d: %s", cfile, cline, msg);
}

static int is_word(int i, const char *w)
{
    return tk[i].kind == TK_WORD && strcmp(tk[i].text, w) == 0;
}

static struct nc_expr *parse_or(void);

static struct nc_expr *parse_prim(void)
{
    struct nc_expr *e;

    if (tk[ti].kind == TK_LP) {
        ti++;
        e = parse_or();
        if (tk[ti].kind != TK_RP)
            err("expected ')'");
        ti++;
        return e;
    }
    if (tk[ti].kind == TK_WORD) {
        e = nc_expr_new(E_SYM, tk[ti].text, NULL, NULL);
        ti++;
        return e;
    }
    if (tk[ti].kind == TK_STR) {
        e = nc_expr_new(E_LIT, tk[ti].text, NULL, NULL);
        ti++;
        return e;
    }
    err("expected expression");
    return NULL;
}

static struct nc_expr *parse_not(void)
{
    if (tk[ti].kind == TK_NOT) {
        ti++;
        return nc_expr_new(E_NOT, NULL, parse_not(), NULL);
    }
    return parse_prim();
}

static struct nc_expr *parse_cmp(void)
{
    struct nc_expr *l = parse_not();
    enum nc_ekind k;

    if (tk[ti].kind == TK_EQ || tk[ti].kind == TK_NE) {
        k = tk[ti].kind == TK_EQ ? E_EQ : E_NE;
        ti++;
        return nc_expr_new(k, NULL, l, parse_not());
    }
    return l;
}

static struct nc_expr *parse_and(void)
{
    struct nc_expr *l = parse_cmp();

    while (tk[ti].kind == TK_AND) {
        ti++;
        l = nc_expr_new(E_AND, NULL, l, parse_cmp());
    }
    return l;
}

static struct nc_expr *parse_or(void)
{
    struct nc_expr *l = parse_and();

    while (tk[ti].kind == TK_OR) {
        ti++;
        l = nc_expr_new(E_OR, NULL, l, parse_and());
    }
    return l;
}

static struct nc_expr *opt_if(void)
{
    struct nc_expr *c = NULL;

    if (is_word(ti, "if")) {
        ti++;
        c = parse_or();
    }
    return c;
}

static void expect_end(void)
{
    if (tk[ti].kind != TK_END)
        err("unexpected trailing tokens");
}

static struct nc_node *new_node(enum nc_nkind kind, const char *text, struct nc_sym *sym)
{
    struct nc_node *n = nc_xmalloc(sizeof *n);

    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->text = text ? nc_xstrdup(text) : NULL;
    n->sym = sym;
    n->dep = dep;
    n->parent = cur;
    if (cur->last)
        cur->last->next = n;
    else
        cur->child = n;
    cur->last = n;
    return n;
}

static int type_of(const char *w, enum nc_type *t)
{
    if (!strcmp(w, "bool"))
        *t = NC_BOOL;
    else if (!strcmp(w, "int"))
        *t = NC_INT;
    else if (!strcmp(w, "hex"))
        *t = NC_HEX;
    else if (!strcmp(w, "string"))
        *t = NC_STRING;
    else
        return 0;
    return 1;
}

static void push_blk(int kind)
{
    if (depth >= MAXBLK)
        err("blocks nested too deeply");
    stk[depth].kind = kind;
    stk[depth].dep = dep;
    stk[depth].cur = cur;
    depth++;
}

static void need_sym(void)
{
    if (entry != ENT_SYM)
        err("attribute outside of a config entry");
}

static char *source_path(const char *rel)
{
    const char *slash = strrchr(cfile, '/');
    struct nc_buf b;
    FILE *f;
    char *p;

    if (slash && rel[0] != '/') {
        nc_buf_init(&b);
        nc_buf_addn(&b, cfile, (size_t)(slash - cfile) + 1);
        nc_buf_add(&b, rel);
        p = nc_buf_take(&b);
        f = fopen(p, "rb");
        if (f) {
            fclose(f);
            return p;
        }
        free(p);
    }
    return nc_xstrdup(rel);
}

static void statement(int *want_help)
{
    const char *kw;
    enum nc_type t;
    struct nc_expr *e;
    struct nc_expr *c;
    struct nc_select *sl;
    struct nc_select **sp;
    struct nc_default *d;
    struct nc_default **dp;
    struct nc_node *n;

    if (tk[0].kind != TK_WORD)
        err("syntax error");
    kw = tk[0].text;
    ti = 1;
    if (!strcmp(kw, "mainmenu")) {
        if (tk[1].kind != TK_STR)
            err("expected quoted title");
        root->text = nc_xstrdup(tk[1].text);
    } else if (!strcmp(kw, "menu")) {
        if (tk[1].kind != TK_STR)
            err("expected quoted title");
        push_blk(B_MENU);
        n = new_node(N_MENU, tk[1].text, NULL);
        cur = n;
        cmenu = n;
        entry = ENT_MENU;
    } else if (!strcmp(kw, "endmenu")) {
        if (depth == 0 || stk[depth - 1].kind != B_MENU)
            err("endmenu without menu");
        depth--;
        dep = stk[depth].dep;
        cur = stk[depth].cur;
        entry = ENT_NONE;
    } else if (!strcmp(kw, "if")) {
        push_blk(B_IF);
        e = parse_or();
        expect_end();
        dep = nc_expr_and(dep, e);
        entry = ENT_NONE;
    } else if (!strcmp(kw, "endif")) {
        if (depth == 0 || stk[depth - 1].kind != B_IF)
            err("endif without if");
        depth--;
        dep = stk[depth].dep;
        cur = stk[depth].cur;
        entry = ENT_NONE;
    } else if (!strcmp(kw, "config") || !strcmp(kw, "menuconfig")) {
        if (tk[1].kind != TK_WORD)
            err("expected symbol name");
        csym = nc_sym_get(tk[1].text);
        new_node(N_SYM, NULL, csym);
        csym->deps = nc_expr_and(csym->deps, dep);
        entry = ENT_SYM;
    } else if (!strcmp(kw, "comment")) {
        if (tk[1].kind != TK_STR)
            err("expected quoted text");
        new_node(N_COMMENT, tk[1].text, NULL);
        entry = ENT_COMMENT;
    } else if (!strcmp(kw, "source")) {
        if (tk[1].kind != TK_STR)
            err("expected quoted path");
        parse_file(source_path(tk[1].text));
    } else if (type_of(kw, &t)) {
        need_sym();
        if (csym->type != NC_UNKNOWN && csym->type != t)
            err("symbol redefined with a different type");
        csym->type = t;
        if (tk[ti].kind == TK_STR) {
            if (!csym->prompt)
                csym->prompt = nc_xstrdup(tk[ti].text);
            ti++;
        }
        c = opt_if();
        csym->deps = nc_expr_and(csym->deps, c);
        expect_end();
    } else if (!strcmp(kw, "prompt")) {
        need_sym();
        if (tk[1].kind != TK_STR)
            err("expected quoted prompt");
        if (!csym->prompt)
            csym->prompt = nc_xstrdup(tk[1].text);
        ti = 2;
        c = opt_if();
        csym->deps = nc_expr_and(csym->deps, c);
        expect_end();
    } else if (!strcmp(kw, "default")) {
        need_sym();
        e = parse_or();
        c = opt_if();
        expect_end();
        d = nc_xmalloc(sizeof *d);
        d->val = e;
        d->cond = c;
        d->next = NULL;
        for (dp = &csym->defaults; *dp; dp = &(*dp)->next)
            ;
        *dp = d;
    } else if (!strcmp(kw, "depends")) {
        if (!is_word(1, "on"))
            err("expected 'depends on'");
        ti = 2;
        e = parse_or();
        expect_end();
        if (entry == ENT_SYM) {
            csym->deps = nc_expr_and(csym->deps, e);
        } else if (entry == ENT_MENU) {
            dep = nc_expr_and(dep, e);
            cmenu->dep = dep;
        } else {
            err("depends on outside of config or menu");
        }
    } else if (!strcmp(kw, "select")) {
        need_sym();
        if (tk[1].kind != TK_WORD)
            err("expected symbol name");
        ti = 2;
        c = opt_if();
        expect_end();
        sl = nc_xmalloc(sizeof *sl);
        sl->target = nc_xstrdup(tk[1].text);
        sl->cond = c;
        sl->next = NULL;
        for (sp = &csym->selects; *sp; sp = &(*sp)->next)
            ;
        *sp = sl;
    } else if (!strcmp(kw, "range")) {
        need_sym();
        if ((tk[1].kind != TK_WORD && tk[1].kind != TK_STR) ||
            (tk[2].kind != TK_WORD && tk[2].kind != TK_STR))
            err("expected 'range LOW HIGH'");
        ti = 3;
        expect_end();
        csym->range_lo = nc_xstrdup(tk[1].text);
        csym->range_hi = nc_xstrdup(tk[2].text);
    } else if (!strcmp(kw, "help") || !strcmp(kw, "---help---")) {
        need_sym();
        expect_end();
        *want_help = 1;
    } else if (!strcmp(kw, "choice") || !strcmp(kw, "endchoice") ||
               !strcmp(kw, "tristate") || !strcmp(kw, "imply")) {
        err("unsupported keyword");
    } else {
        err("unknown keyword");
    }
}

static int indent_of(const char *s, size_t n, size_t *first)
{
    size_t i = 0;
    int col = 0;

    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) {
        if (s[i] == '\t')
            col = (col / 8 + 1) * 8;
        else if (s[i] == ' ')
            col++;
        i++;
    }
    *first = i;
    return col;
}

static int is_blank(const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r')
            return 0;
    return 1;
}

static void finish_help(struct nc_buf *h)
{
    char *p = nc_buf_take(h);
    size_t n = strlen(p);

    while (n > 0 && p[n - 1] == '\n')
        p[--n] = '\0';
    if (csym && !csym->help)
        csym->help = p;
    else
        free(p);
    nc_buf_init(h);
}

static void parse_file(const char *path)
{
    struct nc_tok toks[MAXTOK];
    struct nc_buf help;
    const char *sfile = cfile;
    int sline = cline;
    int base = depth;
    int in_help = 0;
    int help_kw = 0;
    int help_ind = -1;
    int want_help;
    int consumed;
    int ind;
    int nt;
    int i;
    size_t len;
    size_t pos;
    size_t e;
    size_t first;
    size_t ll;
    const char *line;
    char *buf;

    buf = nc_read_file(path, &len);
    if (!buf)
        nc_die("cannot open %s", path);
    cfile = path;
    cline = 0;
    pos = 0;
    nc_buf_init(&help);
    while (pos < len) {
        e = pos;
        while (e < len && buf[e] != '\n')
            e++;
        line = buf + pos;
        ll = e - pos;
        pos = e < len ? e + 1 : e;
        cline++;
        if (in_help) {
            consumed = 0;
            if (is_blank(line, ll)) {
                nc_buf_addc(&help, '\n');
                consumed = 1;
            } else {
                ind = indent_of(line, ll, &first);
                if (help_ind < 0 && ind > help_kw)
                    help_ind = ind;
                if (help_ind >= 0 && ind >= help_ind) {
                    for (i = help_ind; i < ind; i++)
                        nc_buf_addc(&help, ' ');
                    if (ll > first && line[ll - 1] == '\r')
                        ll--;
                    nc_buf_addn(&help, line + first, ll - first);
                    nc_buf_addc(&help, '\n');
                    consumed = 1;
                }
            }
            if (consumed)
                continue;
            finish_help(&help);
            in_help = 0;
        }
        nt = nc_lex_line(line, ll, toks, MAXTOK);
        if (nt < 0)
            err("syntax error");
        if (nt == 0)
            continue;
        tk = toks;
        want_help = 0;
        statement(&want_help);
        tk = toks;
        if (want_help) {
            in_help = 1;
            help_kw = indent_of(line, ll, &first);
            help_ind = -1;
        }
    }
    if (in_help)
        finish_help(&help);
    free(help.p);
    if (depth != base)
        err("unterminated menu or if block");
    free(buf);
    cfile = sfile;
    cline = sline;
}

struct nc_node *nc_parse(const char *path)
{
    root = nc_xmalloc(sizeof *root);
    memset(root, 0, sizeof *root);
    root->kind = N_MENU;
    root->text = nc_xstrdup("Configuration");
    cur = root;
    dep = NULL;
    csym = NULL;
    entry = ENT_NONE;
    depth = 0;
    parse_file(path);
    nc_validate();
    return root;
}
