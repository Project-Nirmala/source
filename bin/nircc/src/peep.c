#include "cc.h"

static char **ln;
static int n;
static int level;

static int eq(int i, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	if (i >= n)
		return 0;
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	return strcmp(ln[i], buf) == 0;
}

static int has(int i, const char *prefix)
{
	return i < n && strncmp(ln[i], prefix, strlen(prefix)) == 0;
}

static int ends(int i, const char *suffix)
{
	size_t a;
	size_t b;

	a = strlen(ln[i]);
	b = strlen(suffix);
	return a >= b && strcmp(ln[i] + a - b, suffix) == 0;
}

static void del(int i, int k)
{
	memmove(ln + i, ln + i + k, (size_t)(n - i - k) * sizeof *ln);
	n -= k;
}

static void put(int i, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	ln[i] = xstrdup(buf);
}

struct jmap {
	const char *set;
	const char *yes;
	const char *no;
};

static const struct jmap jmaps[] = {
	{"setl", "jl", "jge"}, {"setg", "jg", "jle"},
	{"setle", "jle", "jg"}, {"setge", "jge", "jl"},
	{"sete", "je", "jne"}, {"setne", "jne", "je"},
	{"setb", "jb", "jae"}, {"seta", "ja", "jbe"},
	{"setbe", "jbe", "ja"}, {"setae", "jae", "jb"},
	{NULL, NULL, NULL}
};

static const char *invert(const char *set, int jump_if_true)
{
	int i;

	for (i = 0; jmaps[i].set; i++)
		if (strcmp(jmaps[i].set, set) == 0)
			return jump_if_true ? jmaps[i].yes : jmaps[i].no;
	return NULL;
}

static int fuse_branch(int i)
{
	char set[16];
	char jcc[16];
	const char *j;

	if (!has(i, "\tset") || sscanf(ln[i], "\t%15s %%al", set) != 1)
		return 0;
	if (!eq(i + 1, "\tmovzb%c %%al, %s", tg->sfx, tg->ax))
		return 0;
	if (!eq(i + 2, "\tcmp%c $0, %s", tg->sfx, tg->ax))
		return 0;
	if (!has(i + 3, "\tje ") && !has(i + 3, "\tjne "))
		return 0;
	sscanf(ln[i + 3], "\t%15s", jcc);
	j = invert(set, strcmp(jcc, "jne") == 0);
	if (!j)
		return 0;
	put(i + 3, "\t%s %s", j, strchr(ln[i + 3] + 1, ' ') + 1);
	del(i, 3);
	return 1;
}

static const char *fold_ops[] = {"add", "sub", "and", "or", "xor", "cmp",
				  "imul", NULL};

static int fold_operand(int i)
{
	char *dollar;
	char imm[40];
	char *end;
	long v;
	int k;

	if (!has(i, "\tmov") || has(i, "\tmovabs"))
		return 0;
	if (!ends(i, tg->di) || !(dollar = strchr(ln[i], '$')))
		return 0;
	sscanf(dollar, "$%39[^,]", imm);
	v = strtol(imm, &end, 0);
	if (*end || v < -2147483647L - 1 || v > 2147483647L)
		return 0;
	for (k = 0; fold_ops[k]; k++)
		if (eq(i + 1, "\t%s%c %s, %s", fold_ops[k], tg->sfx, tg->di,
		       tg->ax)) {
			put(i, "\t%s%c $%s, %s", fold_ops[k], tg->sfx, imm, tg->ax);
			del(i + 1, 1);
			return 1;
		}
	return 0;
}

static int direct_load(int i)
{
	size_t keep;

	if (!eq(i, "\tpush%c %s", tg->sfx, tg->ax))
		return 0;
	if (!has(i + 1, "\tmov") && !has(i + 1, "\tlea"))
		return 0;
	if (!ends(i + 1, tg->ax) || strstr(ln[i + 1], tg->sp))
		return 0;
	if (!eq(i + 2, "\tmov%c %s, %s", tg->sfx, tg->ax, tg->di))
		return 0;
	if (!eq(i + 3, "\tpop%c %s", tg->sfx, tg->ax))
		return 0;
	keep = strlen(ln[i + 1]) - strlen(tg->ax);
	ln[i + 1][keep] = '\0';
	put(i, "%s%s", ln[i + 1], tg->di);
	del(i + 1, 3);
	return 1;
}

static int is_label(int i)
{
	return i < n && ln[i][0] == '.' && ln[i][strlen(ln[i]) - 1] == ':';
}

static const char *target(int i)
{
	const char *sp;

	if (i >= n || ln[i][0] != '\t' || ln[i][1] != 'j')
		return NULL;
	sp = strchr(ln[i], ' ');
	if (!sp || strncmp(sp + 1, ".L", 2) != 0)
		return NULL;
	return sp + 1;
}

static int find_label(const char *name)
{
	size_t len;
	int k;

	len = strlen(name);
	for (k = 0; k < n; k++)
		if (is_label(k) && strlen(ln[k]) == len + 1
		    && strncmp(ln[k], name, len) == 0)
			return k;
	return -1;
}

static int thread_jumps(void)
{
	char op[16];
	const char *cur;
	const char *next;
	int hops;
	int p;
	int q;
	int i;

	for (i = 0; i < n; i++) {
		cur = target(i);
		if (!cur)
			continue;
		hops = 0;
		for (;;) {
			p = find_label(cur);
			if (p < 0)
				break;
			for (q = p + 1; q < n && is_label(q); q++)
				;
			next = target(q);
			if (!next || !has(q, "\tjmp ") || strcmp(next, cur) == 0)
				break;
			if (++hops > 8) {
				hops = -1;
				break;
			}
			cur = next;
		}
		if (hops > 0 && strcmp(cur, target(i)) != 0) {
			sscanf(ln[i], "\t%15s", op);
			put(i, "\t%s %s", op, cur);
			return 1;
		}
	}
	return 0;
}

static const char *invert_jcc(const char *j)
{
	int k;

	for (k = 0; jmaps[k].set; k++) {
		if (strcmp(jmaps[k].yes, j) == 0)
			return jmaps[k].no;
		if (strcmp(jmaps[k].no, j) == 0)
			return jmaps[k].yes;
	}
	return NULL;
}

static int skip_jump(int i)
{
	char jcc[16];
	const char *inv;
	const char *a;
	const char *b;

	a = target(i);
	b = target(i + 1);
	if (!a || !b || !has(i + 1, "\tjmp ") || has(i, "\tjmp "))
		return 0;
	if (!is_label(i + 2) || strlen(ln[i + 2]) != strlen(a) + 1
	    || strncmp(ln[i + 2], a, strlen(a)) != 0)
		return 0;
	sscanf(ln[i], "\t%15s", jcc);
	inv = invert_jcc(jcc);
	if (!inv)
		return 0;
	put(i, "\t%s %s", inv, b);
	del(i + 1, 1);
	return 1;
}

static int referenced(int self, const char *name)
{
	size_t len;
	const char *s;
	int k;

	len = strlen(name);
	for (k = 0; k < n; k++) {
		if (k == self || is_label(k))
			continue;
		for (s = ln[k]; (s = strstr(s, name)) != NULL; s++)
			if (!isdigit((unsigned char)s[len]))
				return 1;
	}
	return 0;
}

static int drop_labels(void)
{
	char name[64];
	size_t len;
	int i;

	for (i = 0; i < n; i++) {
		if (!is_label(i) || strncmp(ln[i], ".L", 2) != 0)
			continue;
		len = strlen(ln[i]) - 1;
		if (len >= sizeof name)
			continue;
		memcpy(name, ln[i], len);
		name[len] = '\0';
		if (!referenced(i, name)) {
			del(i, 1);
			return 1;
		}
	}
	return 0;
}

static int strength(int i)
{
	char sfx;
	char reg[16];
	long v;
	int k;

	if (sscanf(ln[i], "\timul%c $%ld, %15s", &sfx, &v, reg) != 3)
		return 0;
	if (sfx != tg->sfx || strcmp(reg, tg->ax) != 0 || v < 1
	    || (v & (v - 1)) != 0)
		return 0;
	if (v == 1) {
		del(i, 1);
		return 1;
	}
	for (k = 0; (1L << k) < v; k++)
		;
	put(i, "\tshl%c $%d, %s", tg->sfx, k, tg->ax);
	return 1;
}

static int pass_o2(void)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strength(i) || skip_jump(i))
			return 1;
		if (eq(i, "\tmov%c %s, %s", tg->sfx, tg->ax, tg->ax)) {
			del(i, 1);
			return 1;
		}
	}
	return thread_jumps() || drop_labels();
}

static int pass(void)
{
	int i;
	int j;
	char mem[128];

	for (i = 0; i < n; i++) {
		if (eq(i, "\tpush%c %s", tg->sfx, tg->ax)
		    && eq(i + 1, "\tpop%c %s", tg->sfx, tg->ax)) {
			del(i, 2);
			return 1;
		}
		if (direct_load(i) || fold_operand(i) || fuse_branch(i))
			return 1;
		if (has(i, "\tjmp ") && i + 1 < n
		    && strncmp(ln[i + 1], ln[i] + 5, strlen(ln[i] + 5)) == 0
		    && ln[i + 1][strlen(ln[i] + 5)] == ':'
		    && !ln[i + 1][strlen(ln[i] + 5) + 1]) {
			del(i, 1);
			return 1;
		}
		if ((has(i, "\tjmp ") || eq(i, "\tret")) && i + 1 < n
		    && ln[i + 1][0] == '\t') {
			for (j = i + 1; j < n && ln[j][0] == '\t'; j++)
				;
			del(i + 1, j - i - 1);
			return 1;
		}
		if (eq(i, "\tmovslq %%eax, %%rax") && i + 1 < n
		    && (eq(i + 1, "\tmovslq %%eax, %%rax"))) {
			del(i + 1, 1);
			return 1;
		}
		if (tg->bits == 64 && has(i, "\tmovslq ") && ends(i, ", %rax")
		    && eq(i + 1, "\tmovslq %%eax, %%rax")) {
			del(i + 1, 1);
			return 1;
		}
		if (sscanf(ln[i], "\tmovl %%eax, %127[^\n]", mem) == 1
		    && mem[0] != '%' && i + 1 < n) {
			if (tg->bits == 64 && eq(i + 1, "\tmovslq %s, %%rax", mem)) {
				put(i + 1, "\tmovslq %%eax, %%rax");
				return 1;
			}
			if (tg->bits == 32 && eq(i + 1, "\tmovl %s, %%eax", mem)) {
				del(i + 1, 1);
				return 1;
			}
		}
		if (tg->bits == 64
		    && sscanf(ln[i], "\tmovq %%rax, %127[^\n]", mem) == 1
		    && mem[0] != '%' && eq(i + 1, "\tmovq %s, %%rax", mem)) {
			del(i + 1, 1);
			return 1;
		}
	}
	return 0;
}

void peep_run(char **lines, int count, FILE *out, int opt)
{
	int i;

	ln = lines;
	n = count;
	level = opt;
	while (pass() || (level >= 2 && pass_o2()))
		;
	for (i = 0; i < n; i++) {
		fputs(ln[i], out);
		fputc('\n', out);
	}
}
