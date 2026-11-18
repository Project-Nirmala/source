#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { N_CHAR, N_ANY, N_SET, N_CAT, N_ALT, N_REP, N_GROUP, N_BOL, N_EOL, N_EMPTY };
enum { I_CHAR, I_ANY, I_SET, I_SPLIT, I_JMP, I_SAVE, I_BOL, I_EOL, I_MATCH, I_MARK, I_CHK };

#define MAX_INSTS 60000
#define STEP_LIMIT 4000000L

struct node {
    int          type;
    int          c;
    int          min, max;
    struct node *a, *b;
};

struct inst {
    int op;
    int x, y;
};

struct prog {
    struct inst *code;
    int          n, cap;
    unsigned char (*sets)[32];
    int          nsets;
    int          ngroups, nmarks;
    int          icase, newline;
};

struct parser {
    const char  *p;
    struct prog *pr;
    int          err;
    int          ngroups;
};

static struct node *mk(struct parser *ps, int type)
{
    struct node *n = calloc(1, sizeof(*n));

    if (!n)
        ps->err = REG_ESPACE;
    else
        n->type = type;
    return n;
}

static void free_tree(struct node *n)
{
    if (!n)
        return;
    free_tree(n->a);
    free_tree(n->b);
    free(n);
}

static int class_has(const char *name, size_t n, int c)
{
    if (n == 5 && !strncmp(name, "alpha", 5)) return isalpha(c);
    if (n == 5 && !strncmp(name, "digit", 5)) return isdigit(c);
    if (n == 5 && !strncmp(name, "alnum", 5)) return isalnum(c);
    if (n == 5 && !strncmp(name, "upper", 5)) return isupper(c);
    if (n == 5 && !strncmp(name, "lower", 5)) return islower(c);
    if (n == 5 && !strncmp(name, "space", 5)) return isspace(c);
    if (n == 5 && !strncmp(name, "punct", 5)) return ispunct(c);
    if (n == 5 && !strncmp(name, "print", 5)) return isprint(c);
    if (n == 5 && !strncmp(name, "graph", 5)) return isgraph(c);
    if (n == 5 && !strncmp(name, "cntrl", 5)) return iscntrl(c);
    if (n == 6 && !strncmp(name, "xdigit", 6)) return isxdigit(c);
    if (n == 5 && !strncmp(name, "blank", 5)) return c == ' ' || c == '\t';
    return -1;
}

static void set_add(unsigned char *set, int c)
{
    set[c >> 3] |= (unsigned char)(1u << (c & 7));
}

static int set_has(const unsigned char *set, int c)
{
    return (set[c >> 3] >> (c & 7)) & 1;
}

static struct node *parse_bracket(struct parser *ps)
{
    struct prog *pr = ps->pr;
    unsigned char (*grown)[32];
    unsigned char *set;
    int negate = 0, first = 1, c;
    struct node *n;

    grown = realloc(pr->sets, (size_t)(pr->nsets + 1) * 32);
    if (!grown) {
        ps->err = REG_ESPACE;
        return NULL;
    }
    pr->sets = grown;
    set = pr->sets[pr->nsets];
    memset(set, 0, 32);
    if (*ps->p == '^') {
        negate = 1;
        ps->p++;
    }
    for (;; first = 0) {
        int lo, hi;

        if (!*ps->p) {
            ps->err = REG_EBRACK;
            return NULL;
        }
        if (*ps->p == ']' && !first)
            break;
        if (*ps->p == '[' && ps->p[1] == ':') {
            const char *e = strstr(ps->p + 2, ":]");

            if (!e) {
                ps->err = REG_EBRACK;
                return NULL;
            }
            for (c = 0; c < 256; c++) {
                int r = class_has(ps->p + 2, (size_t)(e - ps->p - 2), c);

                if (r < 0) {
                    ps->err = REG_ECTYPE;
                    return NULL;
                }
                if (r)
                    set_add(set, c);
            }
            ps->p = e + 2;
            continue;
        }
        lo = hi = (unsigned char)*ps->p++;
        if (*ps->p == '-' && ps->p[1] && ps->p[1] != ']') {
            ps->p++;
            hi = (unsigned char)*ps->p++;
            if (hi < lo) {
                ps->err = REG_ERANGE;
                return NULL;
            }
        }
        for (c = lo; c <= hi; c++)
            set_add(set, c);
    }
    ps->p++;
    if (pr->icase)
        for (c = 0; c < 256; c++)
            if (set_has(set, c)) {
                set_add(set, tolower(c));
                set_add(set, toupper(c));
            }
    if (negate) {
        for (c = 0; c < 256; c++)
            if (set_has(set, c))
                set[c >> 3] &= (unsigned char)~(1u << (c & 7));
            else
                set_add(set, c);
        if (pr->newline)
            set[0x0A >> 3] &= (unsigned char)~(1u << (0x0A & 7));
    }
    n = mk(ps, N_SET);
    if (n)
        n->c = pr->nsets++;
    return n;
}

static struct node *parse_alt(struct parser *ps, int depth);

static struct node *parse_atom(struct parser *ps, int depth)
{
    struct node *n;
    int c = (unsigned char)*ps->p;

    switch (c) {
    case '(': {
        int g;

        ps->p++;
        g = ++ps->ngroups;
        n = mk(ps, N_GROUP);
        if (!n)
            return NULL;
        n->c = g;
        n->a = parse_alt(ps, depth + 1);
        if (ps->err)
            return n;
        if (*ps->p != ')') {
            ps->err = REG_EPAREN;
            return n;
        }
        ps->p++;
        return n;
    }
    case '[':
        ps->p++;
        return parse_bracket(ps);
    case '.':
        ps->p++;
        return mk(ps, N_ANY);
    case '^':
        ps->p++;
        return mk(ps, N_BOL);
    case '$':
        ps->p++;
        return mk(ps, N_EOL);
    case '\\':
        ps->p++;
        if (!*ps->p) {
            ps->err = REG_EESCAPE;
            return NULL;
        }
        n = mk(ps, N_CHAR);
        if (n)
            n->c = (unsigned char)*ps->p++;
        return n;
    case '*':
    case '+':
    case '?':
        ps->err = REG_BADRPT;
        return NULL;
    default:
        ps->p++;
        n = mk(ps, N_CHAR);
        if (n)
            n->c = c;
        return n;
    }
}

static struct node *parse_repeat(struct parser *ps, int depth)
{
    struct node *a = parse_atom(ps, depth), *r;

    while (a && !ps->err && (*ps->p == '*' || *ps->p == '+' || *ps->p == '?' || *ps->p == '{')) {
        int min, max;

        if (*ps->p == '*') {
            min = 0;
            max = -1;
            ps->p++;
        } else if (*ps->p == '+') {
            min = 1;
            max = -1;
            ps->p++;
        } else if (*ps->p == '?') {
            min = 0;
            max = 1;
            ps->p++;
        } else {
            const char *q = ps->p + 1;

            if (*q < '0' || *q > '9') {
                ps->err = REG_BADBR;
                break;
            }
            min = 0;
            while (*q >= '0' && *q <= '9')
                min = min * 10 + (*q++ - '0');
            max = min;
            if (*q == ',') {
                q++;
                if (*q >= '0' && *q <= '9') {
                    max = 0;
                    while (*q >= '0' && *q <= '9')
                        max = max * 10 + (*q++ - '0');
                } else {
                    max = -1;
                }
            }
            if (*q != '}' || min > 255 || max > 255 || (max >= 0 && max < min)) {
                ps->err = REG_BADBR;
                break;
            }
            ps->p = q + 1;
        }
        r = mk(ps, N_REP);
        if (!r)
            break;
        r->a = a;
        r->min = min;
        r->max = max;
        a = r;
    }
    return a;
}

static struct node *parse_concat(struct parser *ps, int depth)
{
    struct node *head = NULL, *tail = NULL;

    while (!ps->err && *ps->p && *ps->p != '|' && !(*ps->p == ')' && depth > 0)) {
        struct node *r = parse_repeat(ps, depth), *cat;

        if (!r)
            break;
        if (!head) {
            head = r;
            continue;
        }
        cat = mk(ps, N_CAT);
        if (!cat) {
            free_tree(r);
            break;
        }
        cat->a = head;
        cat->b = r;
        head = cat;
        (void)tail;
    }
    if (!head && !ps->err)
        head = mk(ps, N_EMPTY);
    return head;
}

static struct node *parse_alt(struct parser *ps, int depth)
{
    struct node *left = parse_concat(ps, depth), *alt;

    while (left && !ps->err && *ps->p == '|') {
        ps->p++;
        alt = mk(ps, N_ALT);
        if (!alt)
            break;
        alt->a = left;
        alt->b = parse_concat(ps, depth);
        left = alt;
    }
    return left;
}

static int emit(struct prog *pr, int op, int x, int y)
{
    if (pr->n == pr->cap) {
        int cap = pr->cap ? pr->cap * 2 : 64;
        struct inst *g;

        if (cap > MAX_INSTS * 2)
            return -1;
        g = realloc(pr->code, (size_t)cap * sizeof(*g));
        if (!g)
            return -1;
        pr->code = g;
        pr->cap = cap;
    }
    if (pr->n >= MAX_INSTS)
        return -1;
    pr->code[pr->n].op = op;
    pr->code[pr->n].x = x;
    pr->code[pr->n].y = y;
    return pr->n++;
}

static int gen(struct prog *pr, const struct node *n);

static int nullable(const struct node *n)
{
    switch (n->type) {
    case N_EMPTY:
    case N_BOL:
    case N_EOL:
        return 1;
    case N_CAT:
        return nullable(n->a) && nullable(n->b);
    case N_ALT:
        return nullable(n->a) || nullable(n->b);
    case N_REP:
        return n->min == 0 || nullable(n->a);
    case N_GROUP:
        return nullable(n->a);
    }
    return 0;
}

static int gen_rep(struct prog *pr, const struct node *n)
{
    int i, split;

    for (i = 0; i < n->min; i++)
        if (gen(pr, n->a) < 0)
            return -1;
    if (n->max < 0) {
        int loop = pr->n, mark = nullable(n->a) ? pr->nmarks++ : -1;

        split = emit(pr, I_SPLIT, 0, 0);
        if (split < 0)
            return -1;
        if (mark >= 0 && emit(pr, I_MARK, mark, 0) < 0)
            return -1;
        if (gen(pr, n->a) < 0)
            return -1;
        if (mark >= 0 && emit(pr, I_CHK, mark, 0) < 0)
            return -1;
        if (emit(pr, I_JMP, loop, 0) < 0)
            return -1;
        pr->code[split].x = split + 1;
        pr->code[split].y = pr->n;
    } else {
        int splits[256], ns = 0;

        for (i = n->min; i < n->max; i++) {
            split = emit(pr, I_SPLIT, 0, 0);
            if (split < 0)
                return -1;
            splits[ns++] = split;
            pr->code[split].x = split + 1;
            if (gen(pr, n->a) < 0)
                return -1;
        }
        for (i = 0; i < ns; i++)
            pr->code[splits[i]].y = pr->n;
    }
    return 0;
}

static int gen(struct prog *pr, const struct node *n)
{
    int s, j;

    switch (n->type) {
    case N_CHAR:
        return emit(pr, I_CHAR, pr->icase ? tolower(n->c) : n->c, 0) < 0 ? -1 : 0;
    case N_ANY:
        return emit(pr, I_ANY, 0, 0) < 0 ? -1 : 0;
    case N_SET:
        return emit(pr, I_SET, n->c, 0) < 0 ? -1 : 0;
    case N_BOL:
        return emit(pr, I_BOL, 0, 0) < 0 ? -1 : 0;
    case N_EOL:
        return emit(pr, I_EOL, 0, 0) < 0 ? -1 : 0;
    case N_EMPTY:
        return 0;
    case N_CAT:
        return gen(pr, n->a) < 0 || gen(pr, n->b) < 0 ? -1 : 0;
    case N_ALT:
        s = emit(pr, I_SPLIT, 0, 0);
        if (s < 0 || gen(pr, n->a) < 0)
            return -1;
        j = emit(pr, I_JMP, 0, 0);
        if (j < 0)
            return -1;
        pr->code[s].x = s + 1;
        pr->code[s].y = pr->n;
        if (gen(pr, n->b) < 0)
            return -1;
        pr->code[j].x = pr->n;
        return 0;
    case N_REP:
        return gen_rep(pr, n);
    case N_GROUP:
        if (emit(pr, I_SAVE, 2 * n->c, 0) < 0 || gen(pr, n->a) < 0 ||
            emit(pr, I_SAVE, 2 * n->c + 1, 0) < 0)
            return -1;
        return 0;
    }
    return -1;
}

int regcomp(regex_t *re, const char *pattern, int cflags)
{
    struct parser ps;
    struct prog *pr = calloc(1, sizeof(*pr));
    struct node *root;
    int code;

    if (!pr)
        return REG_ESPACE;
    pr->icase = (cflags & REG_ICASE) != 0;
    pr->newline = (cflags & REG_NEWLINE) != 0;
    ps.p = pattern;
    ps.pr = pr;
    ps.err = 0;
    ps.ngroups = 0;
    root = parse_alt(&ps, 0);
    if (!ps.err && *ps.p == ')')
        ps.err = REG_EPAREN;
    code = ps.err;
    if (!code && (!root || gen(pr, root) < 0 || emit(pr, I_MATCH, 0, 0) < 0))
        code = REG_ESPACE;
    free_tree(root);
    if (code) {
        free(pr->code);
        free(pr->sets);
        free(pr);
        return code;
    }
    pr->ngroups = ps.ngroups;
    re->prog = pr;
    re->re_nsub = (size_t)ps.ngroups;
    re->cflags = cflags;
    return 0;
}

void regfree(regex_t *re)
{
    struct prog *pr = re->prog;

    if (pr) {
        free(pr->code);
        free(pr->sets);
        free(pr);
    }
    re->prog = NULL;
}

struct run {
    const struct prog *pr;
    const char        *subject;
    const char       **caps;
    const char       **marks;
    long               steps;
    int                notbol, noteol;
};

static int run_at(struct run *r, int pc, const char *sp)
{
    const struct prog *pr = r->pr;

    for (;;) {
        const struct inst *in = &pr->code[pc];

        if (++r->steps > STEP_LIMIT)
            return 0;
        switch (in->op) {
        case I_CHAR:
            if (*sp && (pr->icase ? tolower((unsigned char)*sp) : (unsigned char)*sp) == in->x) {
                pc++;
                sp++;
                continue;
            }
            return 0;
        case I_ANY:
            if (*sp && !(pr->newline && *sp == '\n')) {
                pc++;
                sp++;
                continue;
            }
            return 0;
        case I_SET:
            if (*sp && set_has(pr->sets[in->x], (unsigned char)*sp)) {
                pc++;
                sp++;
                continue;
            }
            return 0;
        case I_BOL:
            if ((sp == r->subject && !r->notbol) || (pr->newline && sp > r->subject && sp[-1] == '\n')) {
                pc++;
                continue;
            }
            return 0;
        case I_EOL:
            if ((!*sp && !r->noteol) || (pr->newline && *sp == '\n')) {
                pc++;
                continue;
            }
            return 0;
        case I_JMP:
            pc = in->x;
            continue;
        case I_SPLIT:
            if (run_at(r, in->x, sp))
                return 1;
            pc = in->y;
            continue;
        case I_SAVE: {
            const char *old = r->caps[in->x];

            r->caps[in->x] = sp;
            if (run_at(r, pc + 1, sp))
                return 1;
            r->caps[in->x] = old;
            return 0;
        }
        case I_MARK: {
            const char *old = r->marks[in->x];

            r->marks[in->x] = sp;
            if (run_at(r, pc + 1, sp))
                return 1;
            r->marks[in->x] = old;
            return 0;
        }
        case I_CHK:
            if (r->marks[in->x] == sp)
                return 0;
            pc++;
            continue;
        case I_MATCH:
            r->caps[1] = sp;
            return 1;
        }
        return 0;
    }
}

int regexec(const regex_t *re, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags)
{
    const struct prog *pr = re->prog;
    size_t ncap = 2 * ((size_t)pr->ngroups + 1), i;
    const char **caps = malloc((ncap + (size_t)pr->nmarks) * sizeof(char *));
    const char *sp;
    struct run r;
    int found = 0;

    if (!caps)
        return REG_ESPACE;
    r.pr = pr;
    r.subject = string;
    r.caps = caps;
    r.marks = caps + ncap;
    r.steps = 0;
    r.notbol = (eflags & REG_NOTBOL) != 0;
    r.noteol = (eflags & REG_NOTEOL) != 0;
    for (sp = string;; sp++) {
        for (i = 0; i < ncap + (size_t)pr->nmarks; i++)
            caps[i] = NULL;
        caps[0] = sp;
        if (run_at(&r, 0, sp)) {
            found = 1;
            break;
        }
        if (!*sp || r.steps > STEP_LIMIT)
            break;
    }
    if (found && !(re->cflags & REG_NOSUB))
        for (i = 0; i < nmatch; i++) {
            if (i <= (size_t)pr->ngroups && caps[2 * i] && caps[2 * i + 1]) {
                pmatch[i].rm_so = (regoff_t)(caps[2 * i] - string);
                pmatch[i].rm_eo = (regoff_t)(caps[2 * i + 1] - string);
            } else {
                pmatch[i].rm_so = pmatch[i].rm_eo = -1;
            }
        }
    free(caps);
    return found ? 0 : REG_NOMATCH;
}

size_t regerror(int code, const regex_t *re, char *buf, size_t size)
{
    static const char *const msg[] = {
        "Success", "No match", "Invalid regular expression", "Invalid collating element",
        "Invalid character class name", "Trailing backslash", "Invalid back reference",
        "Unmatched [ or [^", "Unmatched ( or \\(", "Unmatched \\{", "Invalid content of \\{\\}",
        "Invalid range end", "Memory exhausted", "Invalid preceding regular expression"
    };
    const char *m = code >= 0 && code < 14 ? msg[code] : "Unknown error";
    size_t n = strlen(m) + 1;

    (void)re;
    if (size) {
        size_t c = n < size ? n : size - 1;

        memcpy(buf, m, c);
        buf[c] = 0;
    }
    return n;
}
