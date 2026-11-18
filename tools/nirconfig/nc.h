#ifndef NC_H
#define NC_H

#include <stddef.h>

enum nc_type { NC_UNKNOWN, NC_BOOL, NC_INT, NC_HEX, NC_STRING };
enum nc_ekind { E_SYM, E_LIT, E_NOT, E_AND, E_OR, E_EQ, E_NE };
enum nc_nkind { N_MENU, N_SYM, N_COMMENT };
enum nc_tk { TK_END, TK_WORD, TK_STR, TK_AND, TK_OR, TK_NOT, TK_EQ, TK_NE, TK_LP, TK_RP };

struct nc_tok {
    enum nc_tk kind;
    char *text;
};

struct nc_buf {
    char *p;
    size_t len;
    size_t cap;
};

struct nc_expr {
    enum nc_ekind kind;
    char *text;
    struct nc_expr *l;
    struct nc_expr *r;
};

struct nc_default {
    struct nc_expr *val;
    struct nc_expr *cond;
    struct nc_default *next;
};

struct nc_select {
    char *target;
    struct nc_expr *cond;
    struct nc_select *next;
};

struct nc_sym {
    char *name;
    enum nc_type type;
    char *prompt;
    char *help;
    struct nc_default *defaults;
    struct nc_select *selects;
    struct nc_expr *deps;
    char *range_lo;
    char *range_hi;
    char *user;
    char *value;
    int visible;
    int forced;
    int in_config;
    struct nc_sym *hnext;
    struct nc_sym *lnext;
};

struct nc_node {
    enum nc_nkind kind;
    char *text;
    struct nc_sym *sym;
    struct nc_expr *dep;
    struct nc_node *parent;
    struct nc_node *child;
    struct nc_node *last;
    struct nc_node *next;
};

void *nc_xmalloc(size_t n);
void *nc_xrealloc(void *p, size_t n);
char *nc_xstrdup(const char *s);
char *nc_xstrndup(const char *s, size_t n);
void nc_die(const char *fmt, ...);
void nc_warn(const char *fmt, ...);
char *nc_read_file(const char *path, size_t *len);
void nc_buf_init(struct nc_buf *b);
void nc_buf_addn(struct nc_buf *b, const char *s, size_t n);
void nc_buf_add(struct nc_buf *b, const char *s);
void nc_buf_addc(struct nc_buf *b, int c);
char *nc_buf_take(struct nc_buf *b);

int nc_lex_line(const char *s, size_t n, struct nc_tok *toks, int max);

struct nc_expr *nc_expr_new(enum nc_ekind kind, const char *text, struct nc_expr *l, struct nc_expr *r);
struct nc_expr *nc_expr_and(struct nc_expr *a, struct nc_expr *b);
int nc_is_number(const char *s);
int nc_truthy(const char *s);
const char *nc_expr_eval(const struct nc_expr *e);
void nc_expr_check(const char *ctx, const struct nc_expr *e);

struct nc_sym *nc_sym_find(const char *name);
struct nc_sym *nc_sym_get(const char *name);
struct nc_sym *nc_sym_first(void);
int nc_sym_active(const struct nc_sym *s);
int nc_sym_set_user(struct nc_sym *s, const char *val);
void nc_resolve(void);
void nc_validate(void);

struct nc_node *nc_parse(const char *path);

int nc_conf_read(const char *path);
int nc_conf_write(const char *path);
int nc_header_write(const char *path);

int nc_menu_run(struct nc_node *root);

#endif
