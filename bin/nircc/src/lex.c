#include "cc.h"

struct kw {
	const char *name;
	enum tok_kind kind;
};

static const struct kw kws[] = {
	{ "int", T_KW_INT },
	{ "return", T_KW_RETURN },
	{ "if", T_KW_IF },
	{ "else", T_KW_ELSE },
	{ "while", T_KW_WHILE },
	{ "for", T_KW_FOR },
	{ "do", T_KW_DO },
	{ "break", T_KW_BREAK },
	{ "continue", T_KW_CONTINUE },
	{ "char", T_KW_CHAR },
	{ "long", T_KW_LONG },
	{ "void", T_KW_VOID },
	{ "sizeof", T_KW_SIZEOF },
	{ "switch", T_KW_SWITCH },
	{ "case", T_KW_CASE },
	{ "default", T_KW_DEFAULT },
	{ "goto", T_KW_GOTO },
	{ "struct", T_KW_STRUCT },
	{ "union", T_KW_UNION },
	{ "enum", T_KW_ENUM },
	{ "typedef", T_KW_TYPEDEF },
	{ "static", T_KW_STATIC },
	{ "extern", T_KW_EXTERN },
	{ "const", T_KW_CONST },
	{ "volatile", T_KW_CONST },
	{ "unsigned", T_KW_UNSIGNED }, 
	{ "signed", T_KW_SIGNED }, 
	{ "short", T_KW_SHORT },
	{ "float", T_KW_FLOAT },
	{ "double", T_KW_DOUBLE },
	{ "register", T_KW_REGISTER },
	{ "auto", T_KW_AUTO },
	{ "inline", T_KW_INLINE },
	{ "__inline", T_KW_INLINE },
	{ "__inline__", T_KW_INLINE },
	{ "__restrict", T_KW_RESTRICT },
	{ "__restrict__", T_KW_RESTRICT },
	{ "__const", T_KW_CONST },
	{ "__const__", T_KW_CONST },
	{ "__volatile", T_KW_CONST },
	{ "__volatile__", T_KW_CONST },
	{ "__signed", T_KW_SIGNED },
	{ "__signed__", T_KW_SIGNED },
	{ "__attribute", T_KW_ATTRIBUTE },
	{ "__attribute__", T_KW_ATTRIBUTE },
	{ "__extension__", T_KW_EXTENSION },
	{ "__asm", T_KW_ASM },
	{ "__asm__", T_KW_ASM },
	{ NULL, T_EOF }
};

struct op {
	const char *text;
	enum tok_kind kind;
};

static const struct op ops[] = {
	{ "<<=", T_SHLASSIGN }, { ">>=", T_SHRASSIGN }, { "...", T_ELLIPSIS },
	{ "->", T_ARROW }, { "++", T_INC }, { "--", T_DEC },
	{ "<<", T_SHL }, { ">>", T_SHR },
	{ "<=", T_LE }, { ">=", T_GE }, { "==", T_EQ }, { "!=", T_NE },
	{ "&&", T_ANDAND }, { "||", T_OROR },
	{ "+=", T_ADDASSIGN }, { "-=", T_SUBASSIGN }, { "*=", T_MULASSIGN },
	{ "/=", T_DIVASSIGN }, { "%=", T_MODASSIGN }, { "&=", T_ANDASSIGN },
	{ "|=", T_ORASSIGN }, { "^=", T_XORASSIGN },
	{ ".", T_DOT },
	{ "+", T_PLUS }, { "-", T_MINUS }, { "*", T_STAR },
	{ "/", T_SLASH }, { "%", T_PERCENT },
	{ "<", T_LT }, { ">", T_GT }, { "=", T_ASSIGN },
	{ "(", T_LPAREN }, { ")", T_RPAREN }, { ",", T_COMMA },
	{ "{", T_LBRACE }, { "}", T_RBRACE }, { ";", T_SEMI },
	{ "[", T_LBRACKET }, { "]", T_RBRACKET }, { "&", T_AMP }, { "!", T_BANG },
	{ "?", T_QUESTION }, { ":", T_COLON }, { "|", T_PIPE },
	{ "^", T_CARET }, { "~", T_TILDE },
	{ NULL, T_EOF }
};

static int is_ident_start(int c)
{
	return isalpha(c) || c == '_';
}

static int is_ident_char(int c)
{
	return isalnum(c) || c == '_';
}

static int escape(const char **pp, int line)
{
	const char *p;
	int v;
	int n;

	p = *pp;
	if (*p != '\\') {
		*pp = p + 1;
		return (unsigned char)*p;
	}
	p++;
	switch (*p) {
	case 'n': v = '\n'; break;
	case 't': v = '\t'; break;
	case 'r': v = '\r'; break;
	case 'a': v = 7; break;
	case 'b': v = 8; break;
	case 'f': v = 12; break;
	case 'v': v = 11; break;
	case 'x':
		p++;
		v = 0;
		while (isxdigit((unsigned char)*p)) {
			v = v * 16 + (isdigit((unsigned char)*p) ? *p - '0'
			    : (tolower((unsigned char)*p) - 'a' + 10));
			p++;
		}
		*pp = p;
		return v & 255;
	case '\0':
		die_at(line, "bad escape");
		return 0;
	default:
		if (*p >= '0' && *p <= '7') {
			v = 0;
			for (n = 0; n < 3 && *p >= '0' && *p <= '7'; n++)
				v = v * 8 + (*p++ - '0');
			*pp = p;
			return v & 255;
		}
		v = (unsigned char)*p;
		break;
	}
	*pp = p + 1;
	return v;
}

#define MAX_FILES 1024

static const char *lex_files[MAX_FILES];
static int nlex_files;

int lex_file_index(const char *name)
{
	int i;

	for (i = 0; i < nlex_files; i++)
		if (strcmp(lex_files[i], name) == 0)
			return i;
	if (nlex_files == MAX_FILES)
		return 0;
	lex_files[nlex_files] = xstrdup(name);
	return nlex_files++;
}

const char *lex_file_name(int idx)
{
	if (idx <= 0 || idx >= nlex_files)
		return cur_file;
	return lex_files[idx];
}

static const char *line_marker(const char *p, int *line)
{
	char name[512];
	int n;
	int num;

	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	num = 0;
	while (isdigit((unsigned char)*p))
		num = num * 10 + (*p++ - '0');
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '"') {
		p++;
		n = 0;
		while (*p && *p != '"' && *p != '\n') {
			if (*p == '\\' && p[1])
				p++;
			if (n < (int)sizeof name - 1)
				name[n++] = *p;
			p++;
		}
		name[n] = '\0';
		if (*p == '"')
			p++;
		*line = (lex_file_index(name) << 20) | (num - 1);
	} else {
		*line = (*line & ~0xFFFFF) | (num - 1);
	}
	while (*p && *p != '\n')
		p++;
	return p;
}

static int is_float_literal(const char *p)
{
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		return 0;
	while (isdigit((unsigned char)*p))
		p++;
	if (*p == '.')
		return 1;
	return *p == 'e' || *p == 'E';
}

static long lex_int(const char **pp, int *flags)
{
	const char *p;
	unsigned long v;
	int base;
	int d;

	p = *pp;
	v = 0;
	base = 10;
	*flags = 0;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
		*flags |= NUM_NONDEC;
	} else if (p[0] == '0') {
		base = 8;
		*flags |= NUM_NONDEC;
	}
	for (;;) {
		if (isdigit((unsigned char)*p))
			d = *p - '0';
		else if (base == 16 && isxdigit((unsigned char)*p))
			d = tolower((unsigned char)*p) - 'a' + 10;
		else
			break;
		v = v * (unsigned long)base + (unsigned long)d;
		p++;
	}
	for (;;) {
		if (*p == 'u' || *p == 'U')
			*flags |= NUM_UNSIGNED;
		else if (*p == 'l' || *p == 'L')
			*flags |= NUM_LONG;
		else
			break;
		p++;
	}
	*pp = p;
	return (long)v;
}

struct token *lex_all(const char *src)
{
	struct token *toks;
	size_t cap;
	size_t n;
	const char *p;
	const char *start;
	int line;
	int i;
	size_t len;
	int bol;
	int flags;

	nlex_files = 0;
	lex_file_index(cur_file);
	bol = 1;
	cap = 64;
	n = 0;
	toks = xalloc(cap * sizeof *toks);
	line = 1;
	p = src;
	
	for (;;) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
			if (*p == '\n') {
				line++;
				bol = 1;
			}
			p++;
		}
		if (bol && *p == '#') {
			p = line_marker(p, &line);
			continue;
		}
		if (p[0] == '/' && p[1] == '*') {
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/')) {
				if (*p == '\n')
					line++;
				p++;
			}
			if (!*p)
				die_at(line, "unterminated comment");
			p += 2;
			continue;
		}
		if (n + 1 >= cap) {
			struct token *bigger;

			bigger = xalloc(cap * 2 * sizeof *toks);
			memcpy(bigger, toks, n * sizeof *toks);
			toks = bigger;
			cap *= 2;
		}
		toks[n].line = line;
		toks[n].ival = 0;
		toks[n].text = NULL;
		if (!*p) {
			toks[n++].kind = T_EOF;
			return toks;
		}
		bol = 0;
		if (isdigit((unsigned char)*p)
		    || (*p == '.' && isdigit((unsigned char)p[1]))) {
			if (*p == '.' || is_float_literal(p)) {
				start = p;
				while (isalnum((unsigned char)*p) || *p == '.'
				    || ((*p == '+' || *p == '-')
				    && (p[-1] == 'e' || p[-1] == 'E')))
					p++;
				toks[n].kind = T_FNUM;
				toks[n].text = xstrndup(start,
							(size_t)(p - start));
				n++;
				continue;
			}
			toks[n].kind = T_NUM;
			toks[n].ival = lex_int(&p, &flags);
			toks[n].len = flags;
			n++;
			continue;
		}
		if (*p == 'L' && p[1] == '"')
			die_at(line, "wide string literals are not supported");
		if (*p == 'L' && p[1] == '\'')
			p++;
		if (is_ident_start((unsigned char)*p)) {
			start = p;
			while (is_ident_char((unsigned char)*p))
				p++;
			len = (size_t)(p - start);
			toks[n].kind = T_IDENT;
			toks[n].text = xstrndup(start, len);
			for (i = 0; kws[i].name; i++)
				if (strcmp(kws[i].name, toks[n].text) == 0)
					toks[n].kind = kws[i].kind;
			n++;
			continue;
		}
		if (*p == '\'') {
			p++;
			toks[n].ival = (signed char)escape(&p, line);
			if (*p != '\'')
				die_at(line, "bad character literal");
			p++;
			toks[n].kind = T_NUM;
			n++;
			continue;
		}
		if (*p == '"') {
			char *text;
			size_t slen;

			p++;
			text = xalloc(strlen(p) + 1);
			slen = 0;
			while (*p != '"') {
				if (*p == '\0' || *p == '\n')
					die_at(line, "unterminated string");
				text[slen++] = (char)escape(&p, line);
			}
			p++;
			toks[n].kind = T_STR;
			toks[n].text = text;
			toks[n].len = (int)slen;
			n++;
			continue;
		}
		for (i = 0; ops[i].text; i++) {
			len = strlen(ops[i].text);
			if (strncmp(p, ops[i].text, len) == 0)
				break;
		}
		if (!ops[i].text)
			die_at(line, "unexpected character '%c'", *p);
		toks[n++].kind = ops[i].kind;
		p += len;
	}
}
