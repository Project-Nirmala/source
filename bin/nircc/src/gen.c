#include "cc.h"

#define MAX_LOOP 64
#define MAX_ARGS 64

static const char *argreg[6] = {
	"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
};

struct ulabel {
	char *name;
	int id;
	struct ulabel *next;
};

static FILE *out;

static int nlabel;
static struct ulabel *ulabels;
static int depth;
static int brk_top;
static int cont_top;
static int brk_stack[MAX_LOOP];
static int cont_stack[MAX_LOOP];

static char **fbuf;
static int fcount;
static int fcap;
static int buffering;
static int opt_level = 1;

void gen_set_opt(int level)
{
	opt_level = level;
}

int gen_opt(void)
{
	return opt_level;
}

static void emit(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	if (!buffering) {
		fputs(buf, out);
		fputc('\n', out);
		return;
	}
	if (fcount == fcap) {
		fcap = fcap ? fcap * 2 : 256;
		fbuf = realloc(fbuf, (size_t)fcap * sizeof *fbuf);
		if (!fbuf)
			die("out of memory");
	}
	fbuf[fcount++] = xstrdup(buf);
}

static void flush_func(void)
{
	int i;

	buffering = 0;
	if (opt_level > 0) {
		peep_run(fbuf, fcount, out, opt_level);
	} else {
		for (i = 0; i < fcount; i++) {
			fputs(fbuf[i], out);
			fputc('\n', out);
		}
	}
	fcount = 0;
}

static void push_ax(void)
{
	emit("\tpush%c %s", tg->sfx, tg->ax);
	depth++;
}

static void pop_to(const char *reg)
{
	emit("\tpop%c %s", tg->sfx, reg);
	depth--;
}

static int new_label(void)
{
	return nlabel++;
}

static void emit_label(int l)
{
	emit(".L%d:", l);
}

static void emit_test(void)
{
	emit("\tcmp%c $0, %s", tg->sfx, tg->ax);
}

static void load_ax(struct type *ty, const char *mem)
{
    int size;
    int u;

    size = ty->size;
    u = ty->is_unsigned;
    if (size == 1)
        emit("\tmov%cb%c %s, %s", u ? 'z' : 's', tg->sfx, mem, tg->ax);
    else if (size == 2)
        emit("\tmov%cw%c %s, %s", u ? 'z' : 's', tg->sfx, mem, tg->ax);
    else if (size == 4 && tg->bits == 64 && u)
        emit("\tmovl %s, %%eax", mem);
    else if (size == 4 && tg->bits == 64)
        emit("\tmovslq %s, %%rax", mem);
    else if (size == 4)
        emit("\tmovl %s, %%eax", mem);
    else
        emit("\tmov%c %s, %s", tg->sfx, mem, tg->ax);
}

static void store_ax(int size, const char *mem)
{
    if (size == 1)
        emit("\tmovb %%al, %s", mem);
    else if (size == 2)
        emit("\tmovw %%ax, %s", mem);
    else if (size == 4)
        emit("\tmovl %%eax, %s", mem);
    else
        emit("\tmov%c %s, %s", tg->sfx, tg->ax, mem);
}

static void normalize(struct type *t)
{
    if (t->kind == TY_CHAR)
        emit("\tmov%cb%c %%al, %s", t->is_unsigned ? 'z' : 's', tg->sfx,
             tg->ax);
    else if (t->kind == TY_SHORT)
        emit("\tmov%cw%c %%ax, %s", t->is_unsigned ? 'z' : 's', tg->sfx,
             tg->ax);
    else if (tg->bits == 64 && t->kind == TY_INT && t->is_unsigned)
        emit("\tmovl %%eax, %%eax");
    else if (tg->bits == 64 && t->kind == TY_INT)
        emit("\tmovslq %%eax, %%rax");
}

static void gen_binop(int op, struct type *ty, int uns)
{
    const char *set;

    switch (op) {
    case T_PLUS:
        emit("\tadd%c %s, %s", tg->sfx, tg->di, tg->ax);
        normalize(ty);
        return;
    case T_MINUS:
        emit("\tsub%c %s, %s", tg->sfx, tg->di, tg->ax);
        normalize(ty);
        return;
    case T_STAR:
        emit("\timul%c %s, %s", tg->sfx, tg->di, tg->ax);
        normalize(ty);
        return;
    case T_SLASH:
    case T_PERCENT:
        if (uns) {
            emit("\txorl %%edx, %%edx");
            emit("\tdiv%c %s", tg->sfx, tg->di);
        } else {
            emit(tg->bits == 64 ? "\tcqto" : "\tcltd");
            emit("\tidiv%c %s", tg->sfx, tg->di);
        }
        if (op == T_PERCENT)
            emit("\tmov%c %s, %s", tg->sfx, tg->dx, tg->ax);
        return;
    case T_AMP:
        emit("\tand%c %s, %s", tg->sfx, tg->di, tg->ax);
        return;
    case T_PIPE:
        emit("\tor%c %s, %s", tg->sfx, tg->di, tg->ax);
        return;
    case T_CARET:
        emit("\txor%c %s, %s", tg->sfx, tg->di, tg->ax);
        return;
    case T_SHL:
    case T_SHR:
        if (strcmp(tg->di, tg->cx) != 0)
            emit("\tmov%c %s, %s", tg->sfx, tg->di, tg->cx);
        emit("\t%s%c %%cl, %s", op == T_SHL ? "shl" : uns ? "shr" : "sar", tg->sfx,
             tg->ax);
        if (op == T_SHL)
            normalize(ty);
        return;
    case T_LT: set = uns ? "setb" : "setl"; break;
    case T_GT: set = uns ? "seta" : "setg"; break;
    case T_LE: set = uns ? "setbe" : "setle"; break;
    case T_GE: set = uns ? "setae" : "setge"; break;
    case T_EQ: set = "sete"; break;
    default: set = "setne"; break;
    }
    emit("\tcmp%c %s, %s", tg->sfx, tg->di, tg->ax);
    emit("\t%s %%al", set);
    emit("\tmovzb%c %%al, %s", tg->sfx, tg->ax);
}

static void gen_expr(struct node *n);

static void peek_to(const char *reg)
{
	emit("\tmov%c (%s), %s", tg->sfx, tg->sp, reg);
}

static void copy_struct(int size)
{
	int i;

	for (i = 0; i < size; i++) {
		emit("\tmovb %d(%s), %%dl", i, tg->ax);
		emit("\tmovb %%dl, %d(%s)", i, tg->di);
	}
}

static void gen_addr(struct node *n)
{
	switch (n->kind) {
	case N_VAR:
		emit("\tlea%c %d(%s), %s", tg->sfx, n->offset, tg->bp, tg->ax);
		return;
	case N_GVAR:
	case N_STR:
		if (tg->bits == 64)
			emit("\tleaq %s(%%rip), %%rax", n->name);
		else
			emit("\tmovl $%s, %%eax", n->name);
		return;
	case N_DEREF:
		gen_expr(n->lhs);
		return;
	case N_MEMBER:
		gen_addr(n->lhs);
		if (n->offset)
			emit("\tadd%c $%d, %s", tg->sfx, n->offset, tg->ax);
		return;
	default:
		die_at(n->line, "cannot get address");
	}
}

static void gen_load_value(struct node *n)
{
	char buf[64];

	if (n->kind == N_VAR) {
		sprintf(buf, "%d(%s)", n->offset, tg->bp);
		load_ax(n->ty, buf);
		return;
	}
	if (n->kind == N_GVAR) {
		sprintf(buf, tg->bits == 64 ? "%s(%%rip)" : "%s", n->name);
		load_ax(n->ty, buf);
		return;
	}
	gen_addr(n);
	sprintf(buf, "(%s)", tg->ax);
	load_ax(n->ty, buf);
}

static void gen_opassign(struct node *n)
{
	char buf[32];

	gen_addr(n->lhs);
	push_ax();
	gen_expr(n->rhs);
	emit("\tmov%c %s, %s", tg->sfx, tg->ax, tg->di);
	peek_to(tg->ax);
	sprintf(buf, "(%s)", tg->ax);
	load_ax(n->lhs->ty, buf);
	gen_binop(n->op, n->ty, n->lhs->ty->is_unsigned);
	peek_to(tg->di);
	sprintf(buf, "(%s)", tg->di);
	store_ax(n->lhs->ty->size, buf);
	emit("\tadd%c $%d, %s", tg->sfx, tg->word, tg->sp);
	depth--;
}

static void gen_call(struct node *n)
{
    struct node *args[MAX_ARGS];
    struct node *a;
    int nargs;
    int nreg;
    int nstack;
    int pad;
    int i;

    nargs = 0;
    for (a = n->body; a; a = a->next) {
        if (nargs == MAX_ARGS)
            die_at(n->line, "too many arguments");
        args[nargs++] = a;
    }
    if (n->lhs && tg->bits == 64) {
        nreg = nargs < 6 ? nargs : 6;
        nstack = nargs - nreg;
        pad = (depth + nstack) & 1;
        if (pad) {
            emit("\tsubq $8, %%rsp");
            depth++;
        }
        for (i = nargs - 1; i >= 0; i--) {
            gen_expr(args[i]);
            push_ax();
        }
        gen_expr(n->lhs);
        emit("\tmovq %%rax, %%r11");
        for (i = 0; i < nreg; i++)
            pop_to(argreg[i]);
        emit("\tmovl $0, %%eax");
        emit("\tcall *%%r11");
        if (nstack + pad) {
            emit("\taddq $%d, %%rsp", 8 * (nstack + pad));
            depth -= nstack + pad;
        }
        normalize(n->ty);
        return;
    }
    if (tg->bits == 64) {
        nreg = nargs < 6 ? nargs : 6;
        nstack = nargs - nreg;
        pad = (depth + nstack) & 1;
        if (pad) {
            emit("\tsubq $8, %%rsp");
            depth++;
        }
        for (i = nargs - 1; i >= 0; i--) {
            gen_expr(args[i]);
            push_ax();
        }
        for (i = 0; i < nreg; i++)
            pop_to(argreg[i]);
        emit("\tmovl $0, %%eax");
        emit("\tcall %s", n->name);
        if (nstack + pad) {
            emit("\taddq $%d, %%rsp", 8 * (nstack + pad));
            depth -= nstack + pad;
        }
    } else {
        pad = (4 - (depth + nargs) % 4) % 4;
        if (pad) {
            emit("\tsubl $%d, %%esp", 4 * pad);
            depth += pad;
        }
        for (i = nargs - 1; i >= 0; i--) {
            gen_expr(args[i]);
            push_ax();
        }
        if (n->lhs) {
            gen_expr(n->lhs);
            emit("\tcall *%%eax");
        } else {
            emit("\tcall %s", n->name);
        }
        emit("\taddl $%d, %%esp", 4 * (nargs + pad));
        depth -= nargs + pad;
    }
    normalize(n->ty);
}

static void gen_expr(struct node *n)
{
	char buf[32];
	int lf;
	int le;

	switch (n->kind) {
	case N_NUM:
		if (tg->bits == 64 && (n->ival > 2147483647L
		    || n->ival < -2147483647L - 1))
			emit("\tmovabsq $%ld, %s", n->ival, tg->ax);
		else
			emit("\tmov%c $%ld, %s", tg->sfx, n->ival, tg->ax);
		return;
	case N_NEG:
		gen_expr(n->lhs);
		emit("\tneg%c %s", tg->sfx, tg->ax);
		return;
	case N_NOT:
		gen_expr(n->lhs);
		emit_test();
		emit("\tsete %%al");
		emit("\tmovzb%c %%al, %s", tg->sfx, tg->ax);
		return;
	case N_BNOT:
		gen_expr(n->lhs);
		emit("\tnot%c %s", tg->sfx, tg->ax);
		return;
	case N_COMMA:
		gen_expr(n->lhs);
		gen_expr(n->rhs);
		return;
	case N_COND:
		lf = new_label();
		le = new_label();
		gen_expr(n->lhs);
		emit_test();
		emit("\tje .L%d", lf);
		gen_expr(n->rhs);
		emit("\tjmp .L%d", le);
		emit_label(lf);
		gen_expr(n->body);
		emit_label(le);
		return;
	case N_OPASSIGN:
		gen_opassign(n);
		return;
	case N_POSTINC:
		gen_opassign(n);
		emit("\t%s%c $%ld, %s", n->op == T_PLUS ? "sub" : "add",
		     tg->sfx, n->rhs->ival, tg->ax);
		return;
	case N_AND:
	case N_OR:
		lf = new_label();
		le = new_label();
		gen_expr(n->lhs);
		emit_test();
		emit("\t%s .L%d", n->kind == N_AND ? "je" : "jne", lf);
		gen_expr(n->rhs);
		emit_test();
		emit("\t%s .L%d", n->kind == N_AND ? "je" : "jne", lf);
		emit("\tmov%c $%d, %s", tg->sfx, n->kind == N_AND, tg->ax);
		emit("\tjmp .L%d", le);
		emit_label(lf);
		emit("\tmov%c $%d, %s", tg->sfx, n->kind == N_OR, tg->ax);
		emit_label(le);
		return;
	case N_CALL:
		gen_call(n);
		return;
	case N_ADDR:
		gen_addr(n->lhs);
		return;
	case N_CAST:
		gen_expr(n->lhs);
		normalize(n->ty);
		return;
	case N_VASTART:
		gen_expr(n->lhs);
		emit("\tmovl $%ld, (%%rax)", n->ival);
		emit("\tmovl $48, 4(%%rax)");
		emit("\tleaq %d(%%rbp), %%rdx", n->offset);
		emit("\tmovq %%rdx, 8(%%rax)");
		emit("\tleaq %d(%%rbp), %%rdx", n->label);
		emit("\tmovq %%rdx, 16(%%rax)");
		return;
	case N_VAARG:
		lf = new_label();
		le = new_label();
		gen_expr(n->lhs);
		emit("\tmovl (%%rax), %%ecx");
		emit("\tcmpl $48, %%ecx");
		emit("\tjae .L%d", lf);
		emit("\tmovq 16(%%rax), %%rdx");
		emit("\tmovslq %%ecx, %%rcx");
		emit("\taddq %%rcx, %%rdx");
		emit("\taddl $8, (%%rax)");
		emit("\tjmp .L%d", le);
		emit_label(lf);
		emit("\tmovq 8(%%rax), %%rdx");
		emit("\tleaq 8(%%rdx), %%rcx");
		emit("\tmovq %%rcx, 8(%%rax)");
		emit_label(le);
		load_ax(n->ty, "(%rdx)");
		return;
	case N_BINOP:
		gen_expr(n->lhs);
		push_ax();
		gen_expr(n->rhs);
		emit("\tmov%c %s, %s", tg->sfx, tg->ax, tg->di);
		pop_to(tg->ax);
		gen_binop(n->op, n->ty, (int)n->ival);
		return;
	case N_VAR:
	case N_GVAR:
	case N_DEREF:
	case N_MEMBER:
	case N_STR:
		if (n->ty->kind == TY_ARRAY || n->ty->kind == TY_STRUCT)
			gen_addr(n);
		else
			gen_load_value(n);
		return;
    case N_FUNCADDR:
        if (tg->bits == 64)
            emit("\tleaq %s(%%rip), %%rax", n->name);
        else
            emit("\tmovl $%s, %%eax", n->name);
        return;
	case N_ASSIGN:
		if (n->ty->kind == TY_STRUCT) {
			gen_expr(n->rhs);
			push_ax();
			gen_addr(n->lhs);
			emit("\tmov%c %s, %s", tg->sfx, tg->ax, tg->di);
			pop_to(tg->ax);
			copy_struct(n->ty->size);
			emit("\tmov%c %s, %s", tg->sfx, tg->di, tg->ax);
			return;
		}
		gen_expr(n->rhs);
		if (n->lhs->kind == N_VAR) {
			sprintf(buf, "%d(%s)", n->lhs->offset, tg->bp);
			store_ax(n->ty->size, buf);
			return;
		}
		push_ax();
		gen_addr(n->lhs);
		emit("\tmov%c %s, %s", tg->sfx, tg->ax, tg->di);
		pop_to(tg->ax);
		sprintf(buf, "(%s)", tg->di);
		store_ax(n->ty->size, buf);
		return;
	default:
		die_at(n->line, "cannot generate expression");
	}
}

static void epilogue(void)
{
	emit("\tmov%c %s, %s", tg->sfx, tg->bp, tg->sp);
	emit("\tpop%c %s", tg->sfx, tg->bp);
	emit("\tret");
}

static int user_label(const char *name)
{
	struct ulabel *u;

	for (u = ulabels; u; u = u->next)
		if (strcmp(u->name, name) == 0)
			return u->id;
	u = xalloc(sizeof *u);
	u->name = (char *)name;
	u->id = new_label();
	u->next = ulabels;
	ulabels = u;
	return u->id;
}

static void gen_stmt(struct node *n)
{
	struct node *s;
	int a;
	int b;
	int c;
	char buf[32];

	switch (n->kind) {
	case N_RETURN:
		if (n->lhs)
			gen_expr(n->lhs);
		epilogue();
		return;
	case N_DECL:
		if (n->rhs) {
			gen_expr(n->rhs);
			sprintf(buf, "%d(%s)", n->offset, tg->bp);
			store_ax(n->ty->size, buf);
		}
		return;
	case N_EXPR_STMT:
		gen_expr(n->lhs);
		return;
	case N_BLOCK:
		for (s = n->body; s; s = s->next)
			gen_stmt(s);
		return;
	case N_IF:
		a = new_label();
		b = new_label();
		gen_expr(n->lhs);
		emit_test();
		emit("\tje .L%d", a);
		gen_stmt(n->rhs);
		emit("\tjmp .L%d", b);
		emit_label(a);
		if (n->body)
			gen_stmt(n->body);
		emit_label(b);
		return;
	case N_WHILE:
	case N_FOR:
	case N_DO:
		if (brk_top >= MAX_LOOP || cont_top >= MAX_LOOP)
			die_at(n->line, "loops nested too deeply");
		a = new_label();
		b = new_label();
		c = new_label();
		if (n->kind == N_FOR && n->init)
			gen_expr(n->init);
		emit_label(a);
		if (n->kind != N_DO && n->lhs) {
			gen_expr(n->lhs);
			emit_test();
			emit("\tje .L%d", b);
		}
		brk_stack[brk_top++] = b;
		cont_stack[cont_top++] = c;
		gen_stmt(n->rhs);
		brk_top--;
		cont_top--;
		emit_label(c);
		if (n->kind == N_FOR && n->step)
			gen_expr(n->step);
		if (n->kind == N_DO) {
			gen_expr(n->lhs);
			emit_test();
			emit("\tjne .L%d", a);
		} else {
			emit("\tjmp .L%d", a);
		}
		emit_label(b);
		return;
	case N_SWITCH:
		if (brk_top >= MAX_LOOP)
			die_at(n->line, "loops nested too deeply");
		b = new_label();
		gen_expr(n->lhs);
		for (s = n->cases; s; s = s->cnext) {
			s->label = new_label();
			emit("\tcmp%c $%ld, %s", tg->sfx, s->ival, tg->ax);
			emit("\tje .L%d", s->label);
		}
		if (n->init) {
			n->init->label = new_label();
			emit("\tjmp .L%d", n->init->label);
		} else {
			emit("\tjmp .L%d", b);
		}
		brk_stack[brk_top++] = b;
		gen_stmt(n->rhs);
		brk_top--;
		emit_label(b);
		return;
	case N_CASE:
		emit_label(n->label);
		gen_stmt(n->lhs);
		return;
	case N_GOTO:
		emit("\tjmp .L%d", user_label(n->name));
		return;
	case N_LABEL:
		emit_label(user_label(n->name));
		gen_stmt(n->lhs);
		return;
	case N_BREAK:
		emit("\tjmp .L%d", brk_stack[brk_top - 1]);
		return;
	case N_CONTINUE:
		emit("\tjmp .L%d", cont_stack[cont_top - 1]);
		return;
	default:
		die_at(n->line, "cannot generate statement");
	}
}

static void gen_func(struct node *fn)
{
	struct node *p;
	int i;

	buffering = 1;
	ulabels = NULL;
	depth = 0;
	brk_top = 0;
	cont_top = 0;
	if (!fn->op)
		emit("\t.globl %s", fn->name);
	emit("\t.type %s, @function", fn->name);
	emit("%s:", fn->name);
	emit("\tpush%c %s", tg->sfx, tg->bp);
	emit("\tmov%c %s, %s", tg->sfx, tg->sp, tg->bp);
	if (fn->offset)
		emit("\tsub%c $%d, %s", tg->sfx, fn->offset, tg->sp);
	if (tg->bits == 64 && fn->label) {
		for (i = 0; i < 6; i++)
			emit("\tmovq %s, %d(%%rbp)", argreg[i],
			     fn->label + 8 * i);
	}
	if (tg->bits == 64) {
		i = 0;
		for (p = fn->lhs; p && i < 6; p = p->next, i++)
			emit("\tmovq %s, %d(%%rbp)", argreg[i], p->offset);
	}
	for (p = fn->body; p; p = p->next)
		gen_stmt(p);
	emit("\tmov%c $0, %s", tg->sfx, tg->ax);
	epilogue();
	flush_func();
}

static void emit_item(struct node *it){
	const char *d;
	if (it->kind == N_STR) {
		emit("\t.%s %s", tg->bits == 64 ? "quad" : "long", it->name);
		return;
	}
	d = it->ty->size == 1 ? "byte" : it->ty->size == 4 ? "long" : "quad";
	emit("\t.%s %ld", d, it->ival);
}

static int item_size(struct node *it){
	return it->kind == N_STR ? tg->word : it->ty->size;
}

static void gen_global(struct node *g)
{
    struct node *it;
    int written;

    if (!g->op) {
        if (g->label)
            emit("\t.local %s", g->name);
        emit("\t.comm %s,%d,%d", g->name, g->ty->size ? g->ty->size : 1,
             g->ty->align);
        return;
    }
    emit("\t.data");
    emit("\t.align %d", g->ty->align);
    if (!g->label)
        emit("\t.globl %s", g->name);
    emit("%s:", g->name);
    written = 0;
    for (it = g->init; it; it = it->next) {
        if (it->offset > written) {
            emit("\t.zero %d", it->offset - written);
            written = it->offset;
        }
        emit_item(it);
        written += item_size(it);
    }
    if (g->ty->size > written)
        emit("\t.zero %d", g->ty->size - written);
    emit("\t.text");
}

static void gen_string(struct node *s){
	long i;
	unsigned char c;
	emit("%s:", s->name);
	fprintf(out, "\t.string \"");
	for (i = 0; i < s->ival; i++) {
		c = (unsigned char)s->text[i];
		if (c >= 32 && c < 127 && c != '"' && c != '\\')
			fputc(c, out);
		else
			fprintf(out, "\\%03o", c);
	}
	fprintf(out, "\"\n");
}

void gen_unit(struct node *unit, FILE *f)
{
	struct node *fn;
	struct node *g;
	struct node *s;

	out = f;
	emit("\t.text");
	for (fn = unit; fn; fn = fn->next)
		gen_func(fn);
	for (g = g_globals; g; g = g->next)
		gen_global(g);
	if (g_strings) {
		emit("\t.section .rodata");
		for (s = g_strings; s; s = s->next)
			gen_string(s);
	}
	emit("\t.section .note.GNU-stack,\"\",@progbits");
}
