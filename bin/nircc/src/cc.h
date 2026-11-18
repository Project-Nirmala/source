#ifndef CC_H
#define CC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

enum tok_kind {
	T_EOF, T_NUM, T_IDENT, T_STR,
	T_KW_INT, T_KW_CHAR, T_KW_LONG, T_KW_VOID, T_KW_STRUCT, T_KW_UNION,
	T_KW_ENUM, T_KW_TYPEDEF, T_KW_STATIC, T_KW_EXTERN, T_KW_CONST,
	T_KW_IF, T_KW_ELSE, T_KW_WHILE, T_KW_FOR, T_KW_DO, T_KW_BREAK,
	T_KW_CONTINUE, T_KW_RETURN, T_KW_SWITCH, T_KW_CASE, T_KW_DEFAULT,
	T_KW_GOTO, T_KW_SIZEOF,
	T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
	T_SEMI, T_COMMA, T_QUESTION, T_COLON, T_DOT, T_ARROW, T_ELLIPSIS,
	T_ASSIGN, T_ADDASSIGN, T_SUBASSIGN, T_MULASSIGN, T_DIVASSIGN,
	T_MODASSIGN, T_ANDASSIGN, T_ORASSIGN, T_XORASSIGN, T_SHLASSIGN,
	T_SHRASSIGN,
	T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_AMP, T_PIPE, T_CARET,
	T_TILDE, T_BANG, T_SHL, T_SHR,
	T_LT, T_GT, T_LE, T_GE, T_EQ, T_NE, T_ANDAND, T_OROR, T_INC, T_DEC,
	T_KW_UNSIGNED, T_KW_SIGNED, T_KW_SHORT,
	T_KW_FLOAT, T_KW_DOUBLE, T_KW_REGISTER, T_KW_AUTO, T_KW_INLINE,
	T_KW_RESTRICT, T_KW_ATTRIBUTE, T_KW_EXTENSION, T_KW_ASM, T_FNUM
};

#define NUM_UNSIGNED 1
#define NUM_LONG 2
#define NUM_NONDEC 4

struct token {
	enum tok_kind kind;
	long ival;
	char *text;
	int len;
	int line;
};

enum type_kind {
	TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG, TY_PTR, TY_ARRAY, TY_STRUCT, TY_FUNC,
	TY_FLOAT, TY_DOUBLE
};

struct param;

struct member {
	char *name;
	struct type *ty;
	int offset;
	int bit_width;
	int bit_shift;
	struct member *next;
};

struct type {
	enum type_kind kind;
	int size;
	int align;
	int is_union;
	int is_unsigned;
	long len;
	struct type *base;
	struct member *members;
	struct param *params;
	int nparams;
	int variadic;
};

struct param {
	char *name;
	struct type *ty;
	struct param *next;
};

enum node_kind {
	N_NUM, N_STR, N_VAR, N_GVAR, N_NEG, N_NOT, N_BNOT, N_BINOP, N_AND,
	N_OR, N_ASSIGN, N_OPASSIGN, N_POSTINC, N_COND, N_COMMA, N_ADDR,
	N_DEREF, N_MEMBER, N_CAST, N_CALL,
	N_RETURN, N_EXPR_STMT, N_DECL, N_BLOCK, N_IF, N_WHILE, N_DO, N_FOR,
	N_BREAK, N_CONTINUE, N_SWITCH, N_CASE, N_GOTO, N_LABEL, N_FUNC, N_FUNCADDR,
	N_VASTART, N_VAARG
};

struct node {
	enum node_kind kind;
	int op;
	long ival;
	int offset;
	int label;
	char *name;
	char *text;
	struct type *ty;
	struct node *lhs;
	struct node *rhs;
	struct node *body;
	struct node *init;
	struct node *step;
	struct node *next;
	struct node *cases;
	struct node *cnext;
	int line;
};

struct target {
	int bits;
	int word;
	char sfx;
	const char *ax;
	const char *bx;
	const char *cx;
	const char *dx;
	const char *di;
	const char *si;
	const char *bp;
	const char *sp;
	const char *as_flag;
	const char *ld_emul;
	const char *cc_flag;
};

extern const struct target *tg;
extern const char *cur_file;
extern struct type *ty_void;
extern struct type *ty_char;
extern struct type *ty_int;
extern struct type *ty_long;
extern struct node *g_globals;
extern struct node *g_strings;

extern struct type *ty_short;
extern struct type *ty_uchar;
extern struct type *ty_ushort;
extern struct type *ty_uint;
extern struct type *ty_ulong;
extern struct type *ty_float;
extern struct type *ty_double;
extern struct type *ty_ldouble;
struct type *promote(struct type *t);
struct type *common_type(struct type *a, struct type *b);

struct type *func_of(struct type *ret);

void die(const char *fmt, ...);
void die_at(int line, const char *fmt, ...);
int lex_file_index(const char *name);
const char *lex_file_name(int idx);
void *xalloc(size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *read_file(const char *path);

char *preprocess(const char *src, const char *path);
void pp_define(const char *name, const char *value);
void pp_add_include(const char *dir);
void gen_set_opt(int level);
int gen_opt(void);
void peep_run(char **lines, int count, FILE *out, int opt);

struct token *lex_all(const char *src);
struct node *parse_unit(struct token *toks);
void gen_unit(struct node *unit, FILE *out);
int enc_run(char *text, const char *path);
int cc_sscanf(const char *s, const char *fmt, ...);
#define sscanf cc_sscanf

char *preprocess(const char *src, const char *path);
void pp_define(const char *name, const char *value);

void target_set(int bits);
int host_bits(void);

void types_init(void);
struct type *ptr_to(struct type *base);
struct type *array_of(struct type *base, long len);
struct type *new_struct(int is_union);
int align_to(int n, int a);
int is_integer(struct type *t);
int is_ptr(struct type *t);
int is_scalar(struct type *t);

#endif
