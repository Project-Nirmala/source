#include "cc.h"

#define SEC_TEXT 0
#define SEC_DATA 1
#define SEC_RODATA 2
#define SEC_BSS 3
#define NSEC 4

#define R_64 1
#define R_PC32 2
#define R_PLT32 4
#define R_32 10
#define R_32S 11

#define K_REG 1
#define K_IMM 2
#define K_MEM 3

struct esec {
	unsigned char *d;
	long n;
	long cap;
	long align;
};

struct esym {
	char *name;
	int sec;
	long val;
	int global;
	int defd;
	int common;
	long size;
	int func;
	int local_decl;
	int idx;
	int next;
};

struct erel {
	int sec;
	long off;
	int sym;
	int type;
	long addend;
};

struct opd {
	int kind;
	int reg;
	int size;
	long imm;
	char *sym;
	long disp;
	int base;
	int rip;
	int rex8;
	int ind;
};

#define HSIZE 4096

static struct esec secs[NSEC];
static struct esym *syms;
static int nsyms;
static int capsyms;
static int heads[HSIZE];
static int hinit;
static struct erel *rels;
static int nrels;
static int caprels;
static int cur;
static int want_note;
static char *curline;

static const char *r64[16] = {
	"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};
static const char *r32[16] = {
	"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
	"r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
};
static const char *r16[16] = {
	"ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
	"r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
};
static const char *r8[16] = {
	"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
	"r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
};
static const char *alun[8] = {
	"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"
};
static const char *ccn[] = {
	"o", "no", "b", "c", "nae", "ae", "nb", "nc", "e", "z", "ne", "nz",
	"be", "na", "a", "nbe", "s", "ns", "p", "pe", "np", "po", "l", "nge",
	"ge", "nl", "le", "ng", "g", "nle", NULL
};
static const int ccv[] = {
	0, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5,
	6, 6, 7, 7, 8, 9, 10, 10, 11, 11, 12, 12,
	13, 13, 14, 14, 15, 15
};

static void fail(const char *what)
{
	die("assembler: %s: %s", what, curline ? curline : "");
}

static char *dupn(const char *s, long n)
{
	char *p;

	p = malloc((size_t)n + 1);
	if (!p)
		die("out of memory");
	memcpy(p, s, (size_t)n);
	p[n] = '\0';
	return p;
}

static void eb(int c)
{
	struct esec *s;

	s = &secs[cur];
	if (cur == SEC_BSS) {
		s->n++;
		return;
	}
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 256;
		s->d = realloc(s->d, (size_t)s->cap);
		if (!s->d)
			die("out of memory");
	}
	s->d[s->n++] = (unsigned char)(c & 0xff);
}

static void eb16(long v)
{
	eb((int)(v & 0xff));
	eb((int)((v >> 8) & 0xff));
}

static void eb32(long v)
{
	eb16(v & 0xffff);
	eb16((v >> 16) & 0xffff);
}

static void eb64(long v)
{
	eb32(v & 0xffffffffL);
	eb32((v >> 32) & 0xffffffffL);
}

static void patch32(int sec, long off, long v)
{
	int i;

	for (i = 0; i < 4; i++)
		secs[sec].d[off + i] = (unsigned char)((v >> (8 * i)) & 0xff);
}

static int sym_find(const char *name)
{
	unsigned long h;
	const char *p;
	int i;

	if (!hinit) {
		for (i = 0; i < HSIZE; i++)
			heads[i] = -1;
		hinit = 1;
	}
	h = 5381;
	for (p = name; *p; p++)
		h = h * 33 + (unsigned char)*p;
	h %= HSIZE;
	for (i = heads[h]; i >= 0; i = syms[i].next)
		if (strcmp(syms[i].name, name) == 0)
			return i;
	if (nsyms == capsyms) {
		capsyms = capsyms ? capsyms * 2 : 256;
		syms = realloc(syms, (size_t)capsyms * sizeof *syms);
		if (!syms)
			die("out of memory");
	}
	i = nsyms++;
	memset(&syms[i], 0, sizeof syms[i]);
	syms[i].name = dupn(name, (long)strlen(name));
	syms[i].sec = -1;
	syms[i].idx = -1;
	syms[i].next = heads[h];
	heads[h] = i;
	return i;
}

static void add_rel(int sec, long off, int sym, int type, long addend)
{
	if (nrels == caprels) {
		caprels = caprels ? caprels * 2 : 256;
		rels = realloc(rels, (size_t)caprels * sizeof *rels);
		if (!rels)
			die("out of memory");
	}
	rels[nrels].sec = sec;
	rels[nrels].off = off;
	rels[nrels].sym = sym;
	rels[nrels].type = type;
	rels[nrels].addend = addend;
	nrels++;
}

static int is_idc(int c)
{
	return isalnum(c) || c == '_' || c == '.' || c == '$';
}

static char *skipsp(char *p)
{
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}

static char *parse_expr(char *p, char **sym, long *val)
{
	char *q;
	char *e;
	long v;

	*sym = NULL;
	*val = 0;
	p = skipsp(p);
	if (*p == '-' || *p == '+' || isdigit((unsigned char)*p)) {
		*val = strtol(p, &q, 0);
		return q;
	}
	q = p;
	while (is_idc((unsigned char)*q))
		q++;
	if (q == p)
		fail("bad expression");
	*sym = dupn(p, (long)(q - p));
	p = q;
	if (*p == '+' || *p == '-') {
		v = strtol(p, &e, 0);
		*val = v;
		p = e;
	}
	return p;
}

static int parse_reg(const char *name, int *size)
{
	int i;
	const char *e;

	e = name;
	while (isalnum((unsigned char)*e))
		e++;
	for (i = 0; i < 16; i++) {
		if (strlen(r64[i]) == (size_t)(e - name) && strncmp(name, r64[i], (size_t)(e - name)) == 0) {
			*size = 64;
			return i;
		}
		if (strlen(r32[i]) == (size_t)(e - name) && strncmp(name, r32[i], (size_t)(e - name)) == 0) {
			*size = 32;
			return i;
		}
		if (strlen(r16[i]) == (size_t)(e - name) && strncmp(name, r16[i], (size_t)(e - name)) == 0) {
			*size = 16;
			return i;
		}
		if (strlen(r8[i]) == (size_t)(e - name) && strncmp(name, r8[i], (size_t)(e - name)) == 0) {
			*size = 8;
			return i;
		}
	}
	if (e - name == 3 && strncmp(name, "rip", 3) == 0) {
		*size = 64;
		return 99;
	}
	return -1;
}

static void parse_opd(char *s, struct opd *o)
{
	char *p;
	int r;
	int sz;

	memset(o, 0, sizeof *o);
	o->base = -1;
	s = skipsp(s);
	if (*s == '*') {
		o->ind = 1;
		s = skipsp(s + 1);
	}
	if (*s == '$') {
		o->kind = K_IMM;
		parse_expr(s + 1, &o->sym, &o->imm);
		return;
	}
	if (*s == '%') {
		r = parse_reg(s + 1, &sz);
		if (r < 0 || r == 99)
			fail("bad register");
		o->kind = K_REG;
		o->reg = r;
		o->size = sz;
		o->rex8 = sz == 8 && r >= 4 && r < 8;
		return;
	}
	o->kind = K_MEM;
	p = s;
	if (*p != '(')
		p = parse_expr(p, &o->sym, &o->disp);
	p = skipsp(p);
	if (*p == '(') {
		p = skipsp(p + 1);
		if (*p != '%')
			fail("bad memory operand");
		r = parse_reg(p + 1, &sz);
		if (r < 0)
			fail("bad base register");
		if (r == 99)
			o->rip = 1;
		else
			o->base = r;
		while (*p && *p != ')') {
			if (*p == ',')
				fail("index addressing unsupported");
			p++;
		}
	}
}

static int split_ops(char *s, char **out, int max)
{
	int n;
	int depth;
	char *start;

	n = 0;
	depth = 0;
	s = skipsp(s);
	if (!*s)
		return 0;
	start = s;
	for (;; s++) {
		if (*s == '(')
			depth++;
		else if (*s == ')')
			depth--;
		if ((*s == ',' && depth == 0) || *s == '\0') {
			if (n >= max)
				fail("too many operands");
			out[n++] = start;
			if (*s == '\0')
				break;
			*s = '\0';
			start = s + 1;
		}
	}
	return n;
}

static void emit_imm(struct opd *o, int nbytes, int is_signed)
{
	int s;

	if (o->sym) {
		s = sym_find(o->sym);
		if (nbytes == 8)
			add_rel(cur, secs[cur].n, s, R_64, o->imm);
		else if (nbytes == 4)
			add_rel(cur, secs[cur].n, s, is_signed ? R_32S : R_32, o->imm);
		else
			fail("symbol immediate size");
		eb32(0);
		if (nbytes == 8)
			eb32(0);
		return;
	}
	if (nbytes == 1)
		eb((int)(o->imm & 0xff));
	else if (nbytes == 2)
		eb16(o->imm);
	else if (nbytes == 4)
		eb32(o->imm);
	else
		eb64(o->imm);
}

static void emit_modrm(int sz, const unsigned char *op, int nop, int regf,
	int regrex8, struct opd *rm, int immlen)
{
	int w;
	int rexr;
	int rexb;
	int need;
	int i;
	int bn;
	int mod;
	int s;

	w = sz == 64;
	rexr = (regf >> 3) & 1;
	rexb = 0;
	if (rm->kind == K_REG)
		rexb = rm->reg >> 3;
	else if (!rm->rip && rm->base >= 0)
		rexb = rm->base >> 3;
	need = w || rexr || rexb || regrex8 || (rm->kind == K_REG && rm->rex8);
	if (sz == 16)
		eb(0x66);
	if (need)
		eb(0x40 | (w << 3) | (rexr << 2) | rexb);
	for (i = 0; i < nop; i++)
		eb(op[i]);
	if (rm->kind == K_REG) {
		eb(0xC0 | ((regf & 7) << 3) | (rm->reg & 7));
		return;
	}
	if (rm->rip) {
		eb(((regf & 7) << 3) | 5);
		if (rm->sym) {
			s = sym_find(rm->sym);
			add_rel(cur, secs[cur].n, s, R_PC32, rm->disp - 4 - immlen);
			eb32(0);
		} else {
			eb32(rm->disp);
		}
		return;
	}
	if (rm->base < 0) {
		eb(((regf & 7) << 3) | 4);
		eb(0x25);
		if (rm->sym) {
			s = sym_find(rm->sym);
			add_rel(cur, secs[cur].n, s, R_32S, rm->disp);
			eb32(0);
		} else {
			eb32(rm->disp);
		}
		return;
	}
	bn = rm->base & 7;
	if (rm->sym)
		mod = 2;
	else if (rm->disp == 0 && bn != 5)
		mod = 0;
	else if (rm->disp >= -128 && rm->disp <= 127)
		mod = 1;
	else
		mod = 2;
	eb((mod << 6) | ((regf & 7) << 3) | (bn == 4 ? 4 : bn));
	if (bn == 4)
		eb(0x24);
	if (mod == 1) {
		eb((int)(rm->disp & 0xff));
	} else if (mod == 2) {
		if (rm->sym) {
			s = sym_find(rm->sym);
			add_rel(cur, secs[cur].n, s, R_32S, rm->disp);
			eb32(0);
		} else {
			eb32(rm->disp);
		}
	}
}

static int fits8(struct opd *o)
{
	return !o->sym && o->imm >= -128 && o->imm <= 127;
}

static int fits32(struct opd *o)
{
	return o->sym || (o->imm >= -2147483648L && o->imm <= 2147483647L);
}

static void op1(int sz, int opc, int regf, int regrex8, struct opd *rm, int immlen)
{
	unsigned char b[1];

	b[0] = (unsigned char)opc;
	emit_modrm(sz, b, 1, regf, regrex8, rm, immlen);
}

static void enc_alu(int idx, int sz, struct opd *o, int n)
{
	struct opd *src;
	struct opd *dst;
	int base;

	if (n != 2)
		fail("operand count");
	src = &o[0];
	dst = &o[1];
	base = idx * 8;
	if (src->kind == K_IMM) {
		if (dst->kind == K_REG && dst->reg == 0 && (sz == 8 || !fits8(src))) {
			if (sz == 16)
				eb(0x66);
			if (sz == 64)
				eb(0x48);
			eb(base + (sz == 8 ? 4 : 5));
			emit_imm(src, sz == 8 ? 1 : sz == 16 ? 2 : 4, sz == 64);
			return;
		}
		if (sz == 8) {
			op1(sz, 0x80, idx, 0, dst, 1);
			emit_imm(src, 1, 0);
		} else if (fits8(src)) {
			op1(sz, 0x83, idx, 0, dst, 1);
			emit_imm(src, 1, 0);
		} else {
			if (!fits32(src))
				fail("immediate out of range");
			op1(sz, 0x81, idx, 0, dst, sz == 16 ? 2 : 4);
			emit_imm(src, sz == 16 ? 2 : 4, sz == 64);
		}
		return;
	}
	if (src->kind == K_REG) {
		op1(sz, base + (sz == 8 ? 0 : 1), src->reg, src->rex8, dst, 0);
		return;
	}
	if (dst->kind != K_REG)
		fail("memory to memory");
	op1(sz, base + (sz == 8 ? 2 : 3), dst->reg, dst->rex8, src, 0);
}

static void enc_mov(int sz, struct opd *o, int n)
{
	struct opd *src;
	struct opd *dst;
	int ilen;

	if (n != 2)
		fail("operand count");
	src = &o[0];
	dst = &o[1];
	ilen = sz == 8 ? 1 : sz == 16 ? 2 : 4;
	if (src->kind == K_IMM) {
		if (dst->kind == K_REG) {
			if (sz == 64 && !fits32(src)) {
				eb(0x48 | (dst->reg >> 3));
				eb(0xB8 + (dst->reg & 7));
				emit_imm(src, 8, 0);
				return;
			}
			if (sz == 64) {
				op1(sz, 0xC7, 0, 0, dst, 4);
				emit_imm(src, 4, 1);
				return;
			}
			if (sz == 16)
				eb(0x66);
			if (dst->reg >= 8 || (sz == 8 && dst->rex8))
				eb(0x40 | (dst->reg >> 3));
			eb((sz == 8 ? 0xB0 : 0xB8) + (dst->reg & 7));
			emit_imm(src, ilen, 0);
			return;
		}
		op1(sz, sz == 8 ? 0xC6 : 0xC7, 0, 0, dst, ilen);
		emit_imm(src, ilen, sz == 64);
		return;
	}
	if (src->kind == K_REG) {
		op1(sz, sz == 8 ? 0x88 : 0x89, src->reg, src->rex8, dst, 0);
		return;
	}
	if (dst->kind != K_REG)
		fail("memory to memory");
	op1(sz, sz == 8 ? 0x8A : 0x8B, dst->reg, dst->rex8, src, 0);
}

static int cc_find(const char *s)
{
	int i;

	for (i = 0; ccn[i]; i++)
		if (strcmp(ccn[i], s) == 0)
			return ccv[i];
	return -1;
}

static int sfx_size(int c)
{
	switch (c) {
	case 'b': return 8;
	case 'w': return 16;
	case 'l': return 32;
	case 'q': return 64;
	}
	return 0;
}

static void enc_branch(int kind, struct opd *t, int cc)
{
	unsigned char b[1];
	int s;
	int type;

	if (t->ind) {
		b[0] = 0xFF;
		emit_modrm(32, b, 1, kind == 0xE8 ? 2 : 4, 0, t, 0);
		return;
	}
	if (t->kind != K_MEM || !t->sym || t->base >= 0 || t->rip)
		fail("bad branch target");
	if (kind == 0x0F) {
		eb(0x0F);
		eb(0x80 + cc);
	} else {
		eb(kind);
	}
	s = sym_find(t->sym);
	type = kind == 0xE8 ? R_PLT32 : R_PC32;
	add_rel(cur, secs[cur].n, s, type, t->disp - 4);
	eb32(0);
}

static int shift_idx(const char *m)
{
	if (strcmp(m, "rol") == 0) return 0;
	if (strcmp(m, "ror") == 0) return 1;
	if (strcmp(m, "shl") == 0 || strcmp(m, "sal") == 0) return 4;
	if (strcmp(m, "shr") == 0) return 5;
	if (strcmp(m, "sar") == 0) return 7;
	return -1;
}

static int unary_idx(const char *m)
{
	if (strcmp(m, "not") == 0) return 2;
	if (strcmp(m, "neg") == 0) return 3;
	if (strcmp(m, "mul") == 0) return 4;
	if (strcmp(m, "div") == 0) return 6;
	if (strcmp(m, "idiv") == 0) return 7;
	return -1;
}

static int alu_idx(const char *m)
{
	int i;

	for (i = 0; i < 8; i++)
		if (strcmp(alun[i], m) == 0)
			return i;
	return -1;
}

static int known_base(const char *m)
{
	return alu_idx(m) >= 0 || shift_idx(m) >= 0 || unary_idx(m) >= 0
		|| strcmp(m, "mov") == 0 || strcmp(m, "lea") == 0
		|| strcmp(m, "push") == 0 || strcmp(m, "pop") == 0
		|| strcmp(m, "imul") == 0 || strcmp(m, "test") == 0
		|| strcmp(m, "inc") == 0 || strcmp(m, "dec") == 0;
}

static void enc_movx(char *mn, struct opd *o, int n)
{
	unsigned char b[2];
	int nb;
	int sz;
	int src_sz;

	if (n != 2 || o[1].kind != K_REG)
		fail("operand count");
	sz = sfx_size(mn[5]);
	src_sz = sfx_size(mn[4]);
	if (!sz || !src_sz)
		fail("bad suffix");
	nb = 2;
	b[0] = 0x0F;
	if (mn[3] == 'z' && src_sz == 8)
		b[1] = 0xB6;
	else if (mn[3] == 'z' && src_sz == 16)
		b[1] = 0xB7;
	else if (mn[3] == 's' && src_sz == 8)
		b[1] = 0xBE;
	else if (mn[3] == 's' && src_sz == 16)
		b[1] = 0xBF;
	else if (mn[3] == 's' && src_sz == 32 && sz == 64) {
		b[0] = 0x63;
		nb = 1;
	} else
		fail("bad extension");
	emit_modrm(sz, b, nb, o[1].reg, o[1].rex8, &o[0], 0);
}

static void enc_insn(char *mn, char **opstr, int n)
{
	struct opd o[3];
	char base[16];
	int i;
	int sz;
	int sfx;
	int len;
	int idx;
	int cc;
	unsigned char b[2];

	for (i = 0; i < n; i++)
		parse_opd(opstr[i], &o[i]);
	len = (int)strlen(mn);
	if (strcmp(mn, "ret") == 0) { eb(0xC3); return; }
	if (strcmp(mn, "leave") == 0) { eb(0xC9); return; }
	if (strcmp(mn, "nop") == 0) { eb(0x90); return; }
	if (strcmp(mn, "hlt") == 0) { eb(0xF4); return; }
	if (strcmp(mn, "cqto") == 0) { eb(0x48); eb(0x99); return; }
	if (strcmp(mn, "cltd") == 0) { eb(0x99); return; }
	if (strcmp(mn, "cltq") == 0) { eb(0x48); eb(0x98); return; }
	if (strcmp(mn, "cwtl") == 0) { eb(0x98); return; }
	if (strcmp(mn, "cld") == 0) { eb(0xFC); return; }
	if (strcmp(mn, "std") == 0) { eb(0xFD); return; }
	if (strcmp(mn, "int3") == 0) { eb(0xCC); return; }
	if (strcmp(mn, "syscall") == 0) { eb(0x0F); eb(0x05); return; }
	if (strcmp(mn, "ud2") == 0) { eb(0x0F); eb(0x0B); return; }
	if (strcmp(mn, "call") == 0 && n == 1) { enc_branch(0xE8, &o[0], 0); return; }
	if (strcmp(mn, "jmp") == 0 && n == 1) { enc_branch(0xE9, &o[0], 0); return; }
	if (mn[0] == 'j' && n == 1 && (cc = cc_find(mn + 1)) >= 0) {
		enc_branch(0x0F, &o[0], cc);
		return;
	}
	if (strncmp(mn, "set", 3) == 0 && n == 1 && (cc = cc_find(mn + 3)) >= 0) {
		b[0] = 0x0F;
		b[1] = (unsigned char)(0x90 + cc);
		emit_modrm(8, b, 2, 0, 0, &o[0], 0);
		return;
	}
	if ((strcmp(mn, "movabs") == 0 || strcmp(mn, "movabsq") == 0) && n == 2
		&& o[0].kind == K_IMM && o[1].kind == K_REG) {
		eb(0x48 | (o[1].reg >> 3));
		eb(0xB8 + (o[1].reg & 7));
		emit_imm(&o[0], 8, 0);
		return;
	}
	if (len == 6 && strncmp(mn, "mov", 3) == 0 && (mn[3] == 's' || mn[3] == 'z')) {
		enc_movx(mn, o, n);
		return;
	}
	sfx = 0;
	if (known_base(mn)) {
		strcpy(base, mn);
	} else if (len > 1 && len < 15 && sfx_size(mn[len - 1])) {
		memcpy(base, mn, (size_t)len - 1);
		base[len - 1] = '\0';
		if (!known_base(base))
			fail("unknown instruction");
		sfx = sfx_size(mn[len - 1]);
	} else {
		fail("unknown instruction");
	}
	sz = sfx;
	if (!sz && n > 0 && o[n - 1].kind == K_REG)
		sz = o[n - 1].size;
	if (!sz && n > 1 && o[0].kind == K_REG)
		sz = o[0].size;
	if (strcmp(base, "push") == 0 || strcmp(base, "pop") == 0) {
		if (n != 1)
			fail("operand count");
		if (o[0].kind == K_REG) {
			if (o[0].reg >= 8)
				eb(0x41);
			eb((strcmp(base, "push") == 0 ? 0x50 : 0x58) + (o[0].reg & 7));
		} else if (o[0].kind == K_IMM && base[1] == 'u') {
			if (fits8(&o[0])) {
				eb(0x6A);
				emit_imm(&o[0], 1, 0);
			} else {
				eb(0x68);
				emit_imm(&o[0], 4, 1);
			}
		} else if (base[1] == 'u') {
			op1(32, 0xFF, 6, 0, &o[0], 0);
		} else {
			op1(32, 0x8F, 0, 0, &o[0], 0);
		}
		return;
	}
	if (!sz)
		fail("ambiguous operand size");
	if ((idx = alu_idx(base)) >= 0) {
		enc_alu(idx, sz, o, n);
		return;
	}
	if (strcmp(base, "mov") == 0) {
		enc_mov(sz, o, n);
		return;
	}
	if (strcmp(base, "lea") == 0) {
		if (n != 2 || o[0].kind != K_MEM || o[1].kind != K_REG)
			fail("bad lea");
		op1(sz, 0x8D, o[1].reg, 0, &o[0], 0);
		return;
	}
	if (strcmp(base, "test") == 0) {
		if (n != 2 || o[0].kind != K_REG)
			fail("bad test");
		op1(sz, sz == 8 ? 0x84 : 0x85, o[0].reg, o[0].rex8, &o[1], 0);
		return;
	}
	if (strcmp(base, "imul") == 0) {
		b[0] = 0x0F;
		b[1] = 0xAF;
		if (n == 2 && o[0].kind == K_IMM && o[1].kind == K_REG) {
			if (fits8(&o[0])) {
				op1(sz, 0x6B, o[1].reg, o[1].rex8, &o[1], 1);
				emit_imm(&o[0], 1, 0);
			} else {
				op1(sz, 0x69, o[1].reg, o[1].rex8, &o[1], sz == 16 ? 2 : 4);
				emit_imm(&o[0], sz == 16 ? 2 : 4, sz == 64);
			}
			return;
		}
		if (n == 2 && o[1].kind == K_REG) {
			emit_modrm(sz, b, 2, o[1].reg, o[1].rex8, &o[0], 0);
			return;
		}
		if (n == 1) {
			op1(sz, sz == 8 ? 0xF6 : 0xF7, 5, 0, &o[0], 0);
			return;
		}
		fail("bad imul");
	}
	if (strcmp(base, "inc") == 0 || strcmp(base, "dec") == 0) {
		if (n != 1)
			fail("operand count");
		op1(sz, sz == 8 ? 0xFE : 0xFF, base[0] == 'i' ? 0 : 1, 0, &o[0], 0);
		return;
	}
	if ((idx = unary_idx(base)) >= 0) {
		if (n != 1)
			fail("operand count");
		op1(sz, sz == 8 ? 0xF6 : 0xF7, idx, 0, &o[0], 0);
		return;
	}
	if ((idx = shift_idx(base)) >= 0) {
		if (n == 1) {
			op1(sz, sz == 8 ? 0xD0 : 0xD1, idx, 0, &o[0], 0);
		} else if (o[0].kind == K_REG && o[0].reg == 1 && o[0].size == 8) {
			op1(sz, sz == 8 ? 0xD2 : 0xD3, idx, 0, &o[1], 0);
		} else if (o[0].kind == K_IMM && !o[0].sym) {
			if (o[0].imm == 1) {
				op1(sz, sz == 8 ? 0xD0 : 0xD1, idx, 0, &o[1], 0);
			} else {
				op1(sz, sz == 8 ? 0xC0 : 0xC1, idx, 0, &o[1], 1);
				eb((int)(o[0].imm & 0xff));
			}
		} else {
			fail("bad shift");
		}
		return;
	}
	fail("unknown instruction");
}

static int sec_of(const char *name)
{
	if (strncmp(name, ".text", 5) == 0)
		return SEC_TEXT;
	if (strncmp(name, ".rodata", 7) == 0)
		return SEC_RODATA;
	if (strncmp(name, ".data", 5) == 0)
		return SEC_DATA;
	if (strncmp(name, ".bss", 4) == 0)
		return SEC_BSS;
	return -1;
}

static void sec_align(long a)
{
	struct esec *s;

	if (a < 1)
		a = 1;
	s = &secs[cur];
	if (a > s->align)
		s->align = a;
	while (s->n % a)
		eb(cur == SEC_TEXT ? 0x90 : 0);
}

static void data_item(int size, char *arg)
{
	char *sym;
	long val;
	int s;

	parse_expr(arg, &sym, &val);
	if (sym) {
		if (size != 4 && size != 8)
			fail("symbol in data");
		s = sym_find(sym);
		add_rel(cur, secs[cur].n, s, size == 8 ? R_64 : R_32, val);
		val = 0;
	}
	if (size == 1)
		eb((int)(val & 0xff));
	else if (size == 2)
		eb16(val);
	else if (size == 4)
		eb32(val);
	else
		eb64(val);
}

static void emit_string(char *p, int nul)
{
	int c;
	int v;
	int k;

	p = skipsp(p);
	if (*p != '"')
		fail("bad string");
	p++;
	while (*p && *p != '"') {
		c = (unsigned char)*p++;
		if (c == '\\') {
			c = (unsigned char)*p++;
			if (c >= '0' && c <= '7') {
				v = c - '0';
				for (k = 0; k < 2 && *p >= '0' && *p <= '7'; k++)
					v = v * 8 + (*p++ - '0');
				c = v;
			} else if (c == 'n') {
				c = '\n';
			} else if (c == 't') {
				c = '\t';
			} else if (c == 'r') {
				c = '\r';
			} else if (c == 'a') {
				c = 7;
			} else if (c == 'b') {
				c = 8;
			} else if (c == 'f') {
				c = 12;
			} else if (c == 'v') {
				c = 11;
			}
		}
		eb(c);
	}
	if (nul)
		eb(0);
}

static void directive(char *d, char *rest)
{
	char *ops[64];
	char *sym;
	long val;
	long val2;
	int n;
	int i;
	int s;
	int sc;

	rest = skipsp(rest);
	if (strcmp(d, ".text") == 0) { cur = SEC_TEXT; return; }
	if (strcmp(d, ".data") == 0) { cur = SEC_DATA; return; }
	if (strcmp(d, ".bss") == 0) { cur = SEC_BSS; return; }
	if (strcmp(d, ".section") == 0) {
		sc = sec_of(rest);
		if (sc >= 0)
			cur = sc;
		else if (strncmp(rest, ".note.GNU-stack", 15) == 0)
			want_note = 1;
		else
			fail("unsupported section");
		return;
	}
	if (strcmp(d, ".globl") == 0 || strcmp(d, ".global") == 0) {
		parse_expr(rest, &sym, &val);
		s = sym_find(sym);
		syms[s].global = 1;
		return;
	}
	if (strcmp(d, ".local") == 0) {
		parse_expr(rest, &sym, &val);
		s = sym_find(sym);
		syms[s].local_decl = 1;
		return;
	}
	if (strcmp(d, ".type") == 0) {
		n = split_ops(rest, ops, 3);
		if (n == 2) {
			parse_expr(ops[0], &sym, &val);
			s = sym_find(sym);
			if (strstr(ops[1], "function"))
				syms[s].func = 1;
		}
		return;
	}
	if (strcmp(d, ".comm") == 0 || strcmp(d, ".lcomm") == 0) {
		n = split_ops(rest, ops, 3);
		if (n < 2)
			fail("bad .comm");
		parse_expr(ops[0], &sym, &val);
		parse_expr(ops[1], &sym, &val2);
		s = sym_find(ops[0]);
		val = n > 2 ? strtol(ops[2], NULL, 0) : 1;
		if (syms[s].local_decl || d[1] == 'l') {
			sc = cur;
			cur = SEC_BSS;
			sec_align(val);
			syms[s].sec = SEC_BSS;
			syms[s].val = secs[SEC_BSS].n;
			syms[s].defd = 1;
			syms[s].size = val2;
			secs[SEC_BSS].n += val2;
			cur = sc;
		} else {
			syms[s].common = 1;
			syms[s].defd = 1;
			syms[s].global = 1;
			syms[s].val = val;
			syms[s].size = val2;
		}
		return;
	}
	if (strcmp(d, ".align") == 0 || strcmp(d, ".balign") == 0) {
		parse_expr(rest, &sym, &val);
		sec_align(val);
		return;
	}
	if (strcmp(d, ".p2align") == 0) {
		parse_expr(rest, &sym, &val);
		sec_align(1L << val);
		return;
	}
	if (strcmp(d, ".zero") == 0 || strcmp(d, ".skip") == 0 || strcmp(d, ".space") == 0) {
		parse_expr(rest, &sym, &val);
		for (i = 0; i < val; i++)
			eb(0);
		return;
	}
	if (strcmp(d, ".string") == 0 || strcmp(d, ".asciz") == 0) { emit_string(rest, 1); return; }
	if (strcmp(d, ".ascii") == 0) { emit_string(rest, 0); return; }
	sc = 0;
	if (strcmp(d, ".byte") == 0) sc = 1;
	else if (strcmp(d, ".word") == 0 || strcmp(d, ".short") == 0 || strcmp(d, ".value") == 0) sc = 2;
	else if (strcmp(d, ".long") == 0 || strcmp(d, ".int") == 0) sc = 4;
	else if (strcmp(d, ".quad") == 0) sc = 8;
	if (sc) {
		n = split_ops(rest, ops, 64);
		for (i = 0; i < n; i++)
			data_item(sc, ops[i]);
		return;
	}
	if (strcmp(d, ".size") == 0 || strcmp(d, ".file") == 0 || strcmp(d, ".ident") == 0)
		return;
	fail("unsupported directive");
}

static void define_label(char *name)
{
	int s;

	s = sym_find(name);
	if (syms[s].defd)
		fail("duplicate label");
	syms[s].defd = 1;
	syms[s].sec = cur;
	syms[s].val = secs[cur].n;
}

static void assemble_line(char *line)
{
	char *p;
	char *q;
	char *ops[4];
	char *mn;
	int n;
	int instr;

	curline = line;
	p = line;
	instr = 0;
	for (q = p; *q; q++) {
		if (instr) {
			if (*q == '\\' && q[1])
				q++;
			else if (*q == '"')
				instr = 0;
		} else if (*q == '"') {
			instr = 1;
		} else if (*q == '#') {
			break;
		}
	}
	if (*q == '#')
		*q = '\0';
	p = skipsp(p);
	for (;;) {
		q = p;
		while (is_idc((unsigned char)*q))
			q++;
		if (q > p && *q == ':') {
			*q = '\0';
			define_label(p);
			p = skipsp(q + 1);
			continue;
		}
		break;
	}
	if (!*p)
		return;
	q = p;
	while (*q && *q != ' ' && *q != '\t')
		q++;
	mn = p;
	if (*q)
		*q++ = '\0';
	if (mn[0] == '.') {
		directive(mn, q);
		return;
	}
	n = split_ops(q, ops, 4);
	if (n > 3)
		fail("too many operands");
	enc_insn(mn, ops, n);
}

static void w8(FILE *f, long v)
{
	fputc((int)(v & 0xff), f);
}

static void w16(FILE *f, long v)
{
	w8(f, v);
	w8(f, v >> 8);
}

static void w32(FILE *f, long v)
{
	w16(f, v);
	w16(f, v >> 16);
}

static void w64(FILE *f, long v)
{
	w32(f, v & 0xffffffffL);
	w32(f, (v >> 32) & 0xffffffffL);
}

struct shdr {
	long name;
	long type;
	long flags;
	long off;
	long size;
	long link;
	long info;
	long align;
	long entsize;
};

static char shstr[256];
static long shstrn;

static long add_shname(const char *s)
{
	long at;

	at = shstrn;
	strcpy(shstr + shstrn, s);
	shstrn += (long)strlen(s) + 1;
	return at;
}

static long file_pos;

static void padto(FILE *f, long a)
{
	while (file_pos % a) {
		fputc(0, f);
		file_pos++;
	}
}

static void put_bytes(FILE *f, const unsigned char *d, long n)
{
	if (n > 0)
		fwrite(d, 1, (size_t)n, f);
	file_pos += n;
}

static int is_dotL(const char *s)
{
	return s[0] == '.' && s[1] == 'L';
}

static void write_elf(FILE *f)
{
	struct shdr sh[16];
	long nsh;
	long relsec[NSEC];
	long nrel[NSEC];
	long relidx[NSEC];
	long symtab_i;
	long strtab_i;
	long shstr_i;
	long note_i;
	long nlocal;
	long nglobal;
	long first_global;
	long strsz;
	long *stroff;
	unsigned char *relbuf[NSEC];
	long ri;
	long i;
	int s;
	int sec;
	long a;
	struct esym *sy;
	static const char *secnames[NSEC] = {".text", ".data", ".rodata", ".bss"};
	static const char *relnames[NSEC] = {".rela.text", ".rela.data", ".rela.rodata", ".rela.bss"};
	long snames[NSEC];
	long rnames[NSEC];
	long symname;
	long strname;
	long shname;
	long notename;
	struct erel *r;
	long pos;
	unsigned char *rb;
	long symi;
	long adds;
	int nrf;

	nlocal = 0;
	nglobal = 0;
	for (i = 0; i < nsyms; i++) {
		sy = &syms[i];
		if (!sy->defd && is_dotL(sy->name))
			die("assembler: undefined label %s", sy->name);
		if (sy->defd && is_dotL(sy->name) && !sy->global && !sy->common)
			continue;
		if (sy->defd && !sy->global && !sy->common)
			sy->idx = (int)(1 + NSEC + nlocal++);
		else
			sy->idx = -2;
	}
	first_global = 1 + NSEC + nlocal;
	for (i = 0; i < nsyms; i++)
		if (syms[i].idx == -2)
			syms[i].idx = (int)(first_global + nglobal++);
	nrf = 0;
	for (i = 0; i < NSEC; i++) {
		nrel[i] = 0;
		relbuf[i] = NULL;
	}
	for (ri = 0; ri < nrels; ri++) {
		r = &rels[ri];
		sy = &syms[r->sym];
		if (sy->defd && !sy->global && !sy->common) {
			if ((r->type == R_PC32 || r->type == R_PLT32) && r->sec == sy->sec) {
				patch32(r->sec, r->off, sy->val + r->addend - r->off);
				r->type = -1;
				continue;
			}
		}
		nrel[r->sec]++;
	}
	for (i = 0; i < NSEC; i++) {
		if (nrel[i] == 0)
			continue;
		relbuf[i] = malloc((size_t)nrel[i] * 24);
		if (!relbuf[i])
			die("out of memory");
		nrf++;
	}
	for (i = 0; i < NSEC; i++)
		relidx[i] = 0;
	for (ri = 0; ri < nrels; ri++) {
		r = &rels[ri];
		if (r->type < 0)
			continue;
		sy = &syms[r->sym];
		adds = r->addend;
		if (sy->defd && !sy->global && !sy->common) {
			symi = 1 + sy->sec;
			adds += sy->val;
		} else {
			symi = sy->idx;
		}
		rb = relbuf[r->sec] + relidx[r->sec] * 24;
		relidx[r->sec]++;
		for (i = 0; i < 8; i++)
			rb[i] = (unsigned char)((r->off >> (8 * i)) & 0xff);
		for (i = 0; i < 4; i++)
			rb[8 + i] = (unsigned char)((r->type >> (8 * i)) & 0xff);
		for (i = 0; i < 4; i++)
			rb[12 + i] = (unsigned char)((symi >> (8 * i)) & 0xff);
		for (i = 0; i < 8; i++)
			rb[16 + i] = (unsigned char)((adds >> (8 * i)) & 0xff);
	}
	shstrn = 1;
	shstr[0] = '\0';
	for (i = 0; i < NSEC; i++) {
		snames[i] = add_shname(secnames[i]);
		rnames[i] = add_shname(relnames[i]);
	}
	symname = add_shname(".symtab");
	strname = add_shname(".strtab");
	shname = add_shname(".shstrtab");
	notename = add_shname(".note.GNU-stack");
	memset(sh, 0, sizeof sh);
	nsh = 1;
	for (i = 0; i < NSEC; i++) {
		sh[nsh].name = snames[i];
		sh[nsh].type = i == SEC_BSS ? 8 : 1;
		sh[nsh].flags = i == SEC_TEXT ? 6 : i == SEC_RODATA ? 2 : 3;
		sh[nsh].size = secs[i].n;
		a = secs[i].align;
		if (a < 1)
			a = i == SEC_TEXT ? 16 : 8;
		sh[nsh].align = a;
		nsh++;
	}
	for (i = 0; i < NSEC; i++) {
		relsec[i] = 0;
		if (nrel[i] == 0)
			continue;
		relsec[i] = nsh;
		sh[nsh].name = rnames[i];
		sh[nsh].type = 4;
		sh[nsh].flags = 0x40;
		sh[nsh].size = nrel[i] * 24;
		sh[nsh].info = 1 + i;
		sh[nsh].align = 8;
		sh[nsh].entsize = 24;
		nsh++;
	}
	symtab_i = nsh++;
	strtab_i = nsh++;
	shstr_i = nsh++;
	note_i = 0;
	if (want_note)
		note_i = nsh++;
	strsz = 1;
	stroff = malloc((size_t)(nsyms + 1) * sizeof *stroff);
	if (!stroff)
		die("out of memory");
	for (i = 0; i < nsyms; i++) {
		stroff[i] = 0;
		if (syms[i].idx >= 0 && !(syms[i].defd && is_dotL(syms[i].name) && !syms[i].global)) {
			stroff[i] = strsz;
			strsz += (long)strlen(syms[i].name) + 1;
		}
	}
	for (i = 0; i < NSEC; i++)
		if (relsec[i])
			sh[relsec[i]].link = symtab_i;
	sh[symtab_i].name = symname;
	sh[symtab_i].type = 2;
	sh[symtab_i].size = (1 + NSEC + nlocal + nglobal) * 24;
	sh[symtab_i].link = strtab_i;
	sh[symtab_i].info = first_global;
	sh[symtab_i].align = 8;
	sh[symtab_i].entsize = 24;
	sh[strtab_i].name = strname;
	sh[strtab_i].type = 3;
	sh[strtab_i].size = strsz;
	sh[strtab_i].align = 1;
	sh[shstr_i].name = shname;
	sh[shstr_i].type = 3;
	sh[shstr_i].size = shstrn;
	sh[shstr_i].align = 1;
	if (note_i) {
		sh[note_i].name = notename;
		sh[note_i].type = 1;
		sh[note_i].align = 1;
	}
	pos = 64;
	for (i = 1; i < nsh; i++) {
		a = sh[i].align < 1 ? 1 : sh[i].align;
		pos = (pos + a - 1) / a * a;
		sh[i].off = pos;
		if (sh[i].type != 8)
			pos += sh[i].size;
	}
	pos = (pos + 7) / 8 * 8;
	fputc(0x7f, f);
	fputc('E', f);
	fputc('L', f);
	fputc('F', f);
	fputc(2, f);
	fputc(1, f);
	fputc(1, f);
	for (i = 7; i < 16; i++)
		fputc(0, f);
	w16(f, 1);
	w16(f, 62);
	w32(f, 1);
	w64(f, 0);
	w64(f, 0);
	w64(f, pos);
	w32(f, 0);
	w16(f, 64);
	w16(f, 0);
	w16(f, 0);
	w16(f, 64);
	w16(f, nsh);
	w16(f, shstr_i);
	file_pos = 64;
	for (i = 1; i < nsh; i++) {
		if (sh[i].type == 8)
			continue;
		padto(f, sh[i].align < 1 ? 1 : sh[i].align);
		if (i >= 1 && i <= NSEC) {
			put_bytes(f, secs[i - 1].d, secs[i - 1].n);
		} else if (i == symtab_i) {
			for (s = 0; s < 24; s++)
				fputc(0, f);
			file_pos += 24;
			for (sec = 0; sec < NSEC; sec++) {
				w32(f, 0);
				w8(f, 3);
				w8(f, 0);
				w16(f, 1 + sec);
				w64(f, 0);
				w64(f, 0);
				file_pos += 24;
			}
			for (s = 0; s < 2; s++) {
				for (ri = 0; ri < nsyms; ri++) {
					sy = &syms[ri];
					if (sy->idx < 0 || stroff[ri] == 0)
						continue;
					if ((s == 0) != (sy->idx < first_global))
						continue;
					w32(f, stroff[ri]);
					w8(f, ((sy->idx < first_global ? 0 : 1) << 4)
						| (sy->func ? 2 : (sy->defd && sy->sec != SEC_TEXT && !sy->common ? 1 : 0)));
					w8(f, 0);
					w16(f, sy->common ? 0xFFF2 : sy->defd ? 1 + sy->sec : 0);
					w64(f, sy->val);
					w64(f, sy->size);
					file_pos += 24;
				}
			}
		} else if (i == strtab_i) {
			fputc(0, f);
			file_pos++;
			for (ri = 0; ri < nsyms; ri++) {
				if (stroff[ri] == 0)
					continue;
				fputs(syms[ri].name, f);
				fputc(0, f);
				file_pos += (long)strlen(syms[ri].name) + 1;
			}
		} else if (i == shstr_i) {
			put_bytes(f, (unsigned char *)shstr, shstrn);
		} else {
			for (sec = 0; sec < NSEC; sec++)
				if (relsec[sec] == i)
					put_bytes(f, relbuf[sec], nrel[sec] * 24);
		}
	}
	padto(f, 8);
	for (i = 0; i < nsh; i++) {
		w32(f, sh[i].name);
		w32(f, sh[i].type);
		w64(f, sh[i].flags);
		w64(f, 0);
		w64(f, sh[i].off);
		w64(f, sh[i].size);
		w32(f, sh[i].link);
		w32(f, sh[i].info);
		w64(f, sh[i].align);
		w64(f, sh[i].entsize);
	}
	free(stroff);
}

int enc_run(char *text, const char *path)
{
	char *p;
	char *nl;
	FILE *f;

	cur = SEC_TEXT;
	p = text;
	while (*p) {
		nl = strchr(p, '\n');
		if (nl)
			*nl = '\0';
		assemble_line(p);
		if (!nl)
			break;
		p = nl + 1;
	}
	f = fopen(path, "wb");
	if (!f)
		die("cannot write '%s'", path);
	write_elf(f);
	fclose(f);
	return 0;
}
