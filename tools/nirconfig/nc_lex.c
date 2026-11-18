#include <string.h>
#include "nc.h"

static int is_wordchar(int c)
{
    switch (c) {
    case ' ': case '\t': case '\r': case '\n':
    case '"': case '#': case '&': case '|':
    case '!': case '=': case '(': case ')':
        return 0;
    default:
        return 1;
    }
}

static void put(struct nc_tok *toks, int k, enum nc_tk kind, char *text)
{
    toks[k].kind = kind;
    toks[k].text = text;
}

int nc_lex_line(const char *s, size_t n, struct nc_tok *toks, int max)
{
    size_t i = 0;
    size_t st;
    int k = 0;
    struct nc_buf b;
    char c;

    while (i < n) {
        c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
            continue;
        }
        if (c == '#')
            break;
        if (k >= max - 1)
            return -1;
        if (c == '"') {
            nc_buf_init(&b);
            i++;
            while (i < n && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < n)
                    i++;
                nc_buf_addc(&b, s[i]);
                i++;
            }
            if (i >= n)
                return -1;
            i++;
            put(toks, k++, TK_STR, nc_buf_take(&b));
            continue;
        }
        if (c == '&') {
            if (i + 1 < n && s[i + 1] == '&') {
                put(toks, k++, TK_AND, NULL);
                i += 2;
                continue;
            }
            return -1;
        }
        if (c == '|') {
            if (i + 1 < n && s[i + 1] == '|') {
                put(toks, k++, TK_OR, NULL);
                i += 2;
                continue;
            }
            return -1;
        }
        if (c == '!') {
            if (i + 1 < n && s[i + 1] == '=') {
                put(toks, k++, TK_NE, NULL);
                i += 2;
            } else {
                put(toks, k++, TK_NOT, NULL);
                i++;
            }
            continue;
        }
        if (c == '=') {
            put(toks, k++, TK_EQ, NULL);
            i++;
            if (i < n && s[i] == '=')
                i++;
            continue;
        }
        if (c == '(') {
            put(toks, k++, TK_LP, NULL);
            i++;
            continue;
        }
        if (c == ')') {
            put(toks, k++, TK_RP, NULL);
            i++;
            continue;
        }
        st = i;
        while (i < n && is_wordchar((unsigned char)s[i]))
            i++;
        if (i == st)
            return -1;
        put(toks, k++, TK_WORD, nc_xstrndup(s + st, i - st));
    }
    put(toks, k, TK_END, NULL);
    return k;
}
