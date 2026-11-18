#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "nirmake.h"

static const char *cp;
static int cerr;

static int or_expr(void);

static void ws(void)
{
    while (isspace((unsigned char)*cp))
        cp++;
}

static int is_num(const char *s, double *d)
{
    char *end;
    const char *t = s;
    while (isspace((unsigned char)*t))
        t++;
    if (*t == '\0')
        return 0;
    *d = strtod(t, &end);
    if (end == t)
        return 0;
    while (isspace((unsigned char)*end))
        end++;
    return *end == '\0';
}

static char *read_operand(int *quoted)
{
    Buf b;
    *quoted = 0;
    ws();
    buf_init(&b);
    if (*cp == '"') {
        Buf raw;
        char *e;
        *quoted = 1;
        cp++;
        buf_init(&raw);
        while (*cp && *cp != '"') {
            if (*cp == '\\' && cp[1] != '\0')
                cp++;
            buf_addc(&raw, *cp++);
        }
        if (*cp == '"')
            cp++;
        else
            cerr = 1;
        e = var_expand(raw.p);
        buf_free(&raw);
        buf_free(&b);
        return e;
    }
    while (*cp && !isspace((unsigned char)*cp) && *cp != ')' && *cp != '(') {
        if ((cp[0] == '&' && cp[1] == '&') || (cp[0] == '|' && cp[1] == '|'))
            break;
        if ((cp[0] == '=' && cp[1] == '=') || (cp[0] == '!' && cp[1] == '=') || cp[0] == '<' || cp[0] == '>')
            break;
        if (*cp == '$' && (cp[1] == '(' || cp[1] == '{')) {
            const char *end = find_close(cp + 2, cp[1], cp[1] == '(' ? ')' : '}');
            char *piece, *e;
            if (end == NULL) {
                cerr = 1;
                break;
            }
            piece = nm_strndup(cp, (size_t)(end - cp + 1));
            e = var_expand(piece);
            buf_adds(&b, e);
            free(piece);
            free(e);
            cp = end + 1;
            continue;
        }
        buf_addc(&b, *cp++);
    }
    return buf_take(&b);
}

static int truth(const char *s)
{
    double d;
    if (is_num(s, &d))
        return d != 0;
    return s[0] != '\0';
}

static int func_call(const char *name, size_t len, char *arg)
{
    int r = 0;
    char *e;
    if (len == 7 && strncmp(name, "defined", 7) == 0) {
        e = var_expand(arg);
        r = var_defined(str_trim(e));
        free(e);
    } else if (len == 5 && strncmp(name, "empty", 5) == 0) {
        Buf b;
        char *v, *t;
        buf_init(&b);
        buf_adds(&b, "${");
        buf_adds(&b, arg);
        buf_adds(&b, "}");
        v = var_expand(b.p);
        t = str_trim(v);
        r = (t[0] == '\0');
        free(v);
        buf_free(&b);
    } else if (len == 6 && strncmp(name, "exists", 6) == 0) {
        e = var_expand(arg);
        r = file_exists(str_trim(e));
        free(e);
    } else if (len == 6 && strncmp(name, "target", 6) == 0) {
        Node *n;
        e = var_expand(arg);
        n = node_lookup(str_trim(e));
        r = (n != NULL && n->is_target);
        free(e);
    } else if (len == 4 && strncmp(name, "make", 4) == 0) {
        e = var_expand(arg);
        r = sl_has(&goals, str_trim(e));
        free(e);
    } else {
        cerr = 1;
    }
    return r;
}

static int primary(void)
{
    const char *p;
    char *lhs, *rhs;
    int q1, q2, r;
    double a, b;
    int op;

    ws();
    if (*cp == '!' && cp[1] != '=') {
        cp++;
        return !primary();
    }
    if (*cp == '(') {
        cp++;
        r = or_expr();
        ws();
        if (*cp == ')')
            cp++;
        else
            cerr = 1;
        return r;
    }
    p = cp;
    while (isalpha((unsigned char)*p))
        p++;
    if (p > cp && *p == '(') {
        const char *end = find_close(p + 1, '(', ')');
        if (end != NULL) {
            char *arg = nm_strndup(p + 1, (size_t)(end - p - 1));
            r = func_call(cp, (size_t)(p - cp), arg);
            free(arg);
            cp = end + 1;
            return r;
        }
    }
    lhs = read_operand(&q1);
    ws();
    op = 0;
    if (cp[0] == '=' && cp[1] == '=')
        op = 1;
    else if (cp[0] == '!' && cp[1] == '=')
        op = 2;
    else if (cp[0] == '<' && cp[1] == '=')
        op = 3;
    else if (cp[0] == '>' && cp[1] == '=')
        op = 4;
    else if (cp[0] == '<')
        op = 5;
    else if (cp[0] == '>')
        op = 6;
    if (op == 0) {
        r = (q1 && lhs[0] != '\0') ? 1 : truth(lhs);
        free(lhs);
        return r;
    }
    cp += (op <= 4) ? 2 : 1;
    rhs = read_operand(&q2);
    if (is_num(lhs, &a) && is_num(rhs, &b)) {
        switch (op) {
        case 1:
            r = a == b;
            break;
        case 2:
            r = a != b;
            break;
        case 3:
            r = a <= b;
            break;
        case 4:
            r = a >= b;
            break;
        case 5:
            r = a < b;
            break;
        default:
            r = a > b;
            break;
        }
    } else {
        int c = strcmp(lhs, rhs);
        switch (op) {
        case 1:
            r = c == 0;
            break;
        case 2:
            r = c != 0;
            break;
        case 3:
            r = c <= 0;
            break;
        case 4:
            r = c >= 0;
            break;
        case 5:
            r = c < 0;
            break;
        default:
            r = c > 0;
            break;
        }
    }
    free(lhs);
    free(rhs);
    return r;
}

static int and_expr(void)
{
    int r = primary();
    for (;;) {
        ws();
        if (cp[0] == '&' && cp[1] == '&') {
            int n;
            cp += 2;
            n = primary();
            r = r && n;
        } else {
            break;
        }
    }
    return r;
}

static int or_expr(void)
{
    int r = and_expr();
    for (;;) {
        ws();
        if (cp[0] == '|' && cp[1] == '|') {
            int n;
            cp += 2;
            n = and_expr();
            r = r || n;
        } else {
            break;
        }
    }
    return r;
}

int cond_eval(const char *expr, int *result)
{
    cp = expr;
    cerr = 0;
    *result = or_expr();
    ws();
    if (*cp != '\0')
        cerr = 1;
    return cerr ? -1 : 0;
}
