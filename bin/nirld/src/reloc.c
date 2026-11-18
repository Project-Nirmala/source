#include "ld.h"

struct rel {
	u64 off;
	u32 sym;
	u32 type;
	s64 addend;
};

typedef void (*rel_fn)(struct input *, struct isec *, const struct isec *, const struct rel *);

long ngot;

static const struct rtype rt_x86_64[] = {
	{ 0, C_NONE, 0, R_WRAP },
	{ 1, C_ABS, 8, R_WRAP },
	{ 2, C_PCREL, 4, R_SIGNED },
	{ 3, C_GOTSLOT, 4, R_UNSIGNED },
	{ 4, C_PCREL, 4, R_SIGNED },
	{ 9, C_GOTSLOTPC, 4, R_SIGNED },
	{ 10, C_ABS, 4, R_UNSIGNED },
	{ 11, C_ABS, 4, R_SIGNED },
	{ 12, C_ABS, 2, R_UNSIGNED },
	{ 13, C_PCREL, 2, R_SIGNED },
	{ 14, C_ABS, 1, R_UNSIGNED },
	{ 15, C_PCREL, 1, R_SIGNED },
	{ 24, C_PCREL, 8, R_WRAP },
	{ 25, C_GOTOFF, 8, R_WRAP },
	{ 26, C_GOTPC, 4, R_SIGNED },
	{ 18, C_TPOFF, 8, R_WRAP },
	{ 22, C_TPSLOTPC, 4, R_SIGNED },
	{ 23, C_TPOFF, 4, R_SIGNED },
	{ 27, C_GOTSLOT, 8, R_WRAP },
	{ 41, C_GOTSLOTPC, 4, R_SIGNED },
	{ 42, C_GOTSLOTPC, 4, R_SIGNED }
};

static const struct rtype rt_i386[] = {
	{ 0, C_NONE, 0, R_WRAP },
	{ 1, C_ABS, 4, R_WRAP },
	{ 2, C_PCREL, 4, R_WRAP },
	{ 3, C_GOTSLOT, 4, R_WRAP },
	{ 4, C_PCREL, 4, R_WRAP },
	{ 9, C_GOTOFF, 4, R_WRAP },
	{ 10, C_GOTPC, 4, R_WRAP },
	{ 15, C_TPSLOTABS, 4, R_WRAP },
	{ 16, C_TPSLOT, 4, R_WRAP },
	{ 17, C_TPOFF, 4, R_WRAP },
	{ 20, C_ABS, 2, R_UNSIGNED },
	{ 21, C_PCREL, 2, R_SIGNED },
	{ 22, C_ABS, 1, R_UNSIGNED },
	{ 23, C_PCREL, 1, R_SIGNED },
	{ 34, C_TPOFFPOS, 4, R_WRAP },
	{ 43, C_GOTSLOT, 4, R_WRAP }
};

static const struct machine machines[] = {
	{ EM_X86_64, ELFCLASS64, "elf_x86_64", 0x400000UL,
	  rt_x86_64, (int)(sizeof(rt_x86_64) / sizeof(rt_x86_64[0])) },
	{ EM_386, ELFCLASS32, "elf_i386", 0x08048000UL,
	  rt_i386, (int)(sizeof(rt_i386) / sizeof(rt_i386[0])) }
};

const struct machine *machine_by_em(int em)
{
	size_t i;

	for (i = 0; i < sizeof(machines) / sizeof(machines[0]); i++)
		if (machines[i].em == em)
			return &machines[i];
	return NULL;
}

const struct machine *machine_by_emul(const char *emul)
{
	size_t i;

	for (i = 0; i < sizeof(machines) / sizeof(machines[0]); i++)
		if (strcmp(machines[i].emul, emul) == 0)
			return &machines[i];
	return NULL;
}

static const struct rtype *find_rtype(u32 type)
{
	int i;

	for (i = 0; i < mach->nrt; i++)
		if (mach->rt[i].type == type)
			return &mach->rt[i];
	return NULL;
}

static int word_size(void)
{
	return mach->cls == ELFCLASS32 ? 4 : 8;
}

static int rel_entsize(int cls, int rela)
{
	if (cls == ELFCLASS32)
		return rela ? 12 : 8;
	return rela ? 24 : 16;
}

static void get_rel(const struct input *in, const struct isec *rs, long i, struct rel *r)
{
	const unsigned char *p;
	u64 info;
	int rela;

	rela = rs->type == SHT_RELA;
	p = in->buf + rs->offset + (size_t)i * (size_t)rel_entsize(in->cls, rela);
	if (in->cls == ELFCLASS32) {
		r->off = rd(p, 4);
		info = rd(p + 4, 4);
		r->sym = (u32)(info >> 8);
		r->type = (u32)(info & 0xff);
		r->addend = rela ? sext(rd(p + 8, 4), 4) : 0;
	} else {
		r->off = rd(p, 8);
		info = rd(p + 8, 8);
		r->sym = (u32)(info >> 32);
		r->type = (u32)(info & 0xffffffffUL);
		r->addend = rela ? (s64)rd(p + 16, 8) : 0;
	}
}

static void walk_relocs(rel_fn fn)
{
	struct input *in;
	struct isec *rs;
	struct isec *t;
	struct rel r;
	long i;
	long n;
	int ii;
	int k;

	for (ii = 0; ii < ninputs; ii++) {
		in = inputs[ii];
		for (k = 0; k < in->nsec; k++) {
			rs = &in->secs[k];
			if (rs->type != SHT_REL && rs->type != SHT_RELA)
				continue;
			if (rs->info >= (u32)in->nsec)
				continue;
			t = &in->secs[rs->info];
			if (!t->live)
				continue;
			n = (long)(rs->size / (u64)rel_entsize(in->cls, rs->type == SHT_RELA));
			for (i = 0; i < n; i++) {
				get_rel(in, rs, i, &r);
				if (r.sym >= (u32)in->nsym) {
					err("%s: relocation with bad symbol index in %s", in->name, t->name);
					continue;
				}
				fn(in, t, rs, &r);
			}
		}
	}
}

static long *slot_ref(struct isym *s)
{
	return s->g ? &s->g->got : &s->got;
}

static long *tp_ref(struct isym *s)
{
	return s->g ? &s->g->gottp : &s->gottp;
}

static int is_tp_slot(int calc)
{
	return calc == C_TPSLOTPC || calc == C_TPSLOT || calc == C_TPSLOTABS;
}

static int is_got_slot(int calc)
{
	return calc == C_GOTSLOT || calc == C_GOTSLOTPC;
}

static void scan_one(struct input *in, struct isec *t, const struct isec *rs, const struct rel *r)
{
	const struct rtype *rt;
	long *slot;

	(void)t;
	(void)rs;
	rt = find_rtype(r->type);
	if (!rt) {
		err("%s: unsupported relocation type %u", in->name, r->type);
		return;
	}
	if (is_tp_slot(rt->calc))
		slot = tp_ref(&in->syms[r->sym]);
	else if (is_got_slot(rt->calc))
		slot = slot_ref(&in->syms[r->sym]);
	else
		return;
	if (*slot < 0)
		*slot = ngot++;
}

void scan_relocs(void)
{
	walk_relocs(scan_one);
	osecs[O_GOT].size = (u64)ngot * (u64)word_size();
	if (ngot)
		osecs[O_GOT].align = (u64)word_size();
}

void fill_got(void)
{
	struct osec *o;
	struct input *in;
	struct isym *s;
	struct gsym *g;
	u64 v;
	int w;
	int i;
	int k;

	o = &osecs[O_GOT];
	w = word_size();
	for (i = 0; i < ngsyms; i++) {
		g = gsyms[i];
		if (g->got >= 0)
			wr(o->buf + g->got * w, w, g->addr);
		if (g->gottp >= 0)
			wr(o->buf + g->gottp * w, w, tls_tpoff(g->addr));
	}
	for (k = 0; k < ninputs; k++) {
		in = inputs[k];
		for (i = 0; i < in->nsym; i++) {
			s = &in->syms[i];
			if (s->g || (s->got < 0 && s->gottp < 0))
				continue;
			if (!sym_value(in, s, &v))
				continue;
			if (s->got >= 0)
				wr(o->buf + s->got * w, w, v);
			if (s->gottp >= 0)
				wr(o->buf + s->gottp * w, w, tls_tpoff(v));
		}
	}
}

static int fits(s64 v, int size, int chk)
{
	int bits;

	if (chk == R_WRAP || size >= 8)
		return 1;
	bits = size * 8;
	if (chk == R_UNSIGNED)
		return v >= 0 && (u64)v < ((u64)1 << bits);
	return v >= -((s64)1 << (bits - 1)) && v < ((s64)1 << (bits - 1));
}

static void apply_one(struct input *in, struct isec *t, const struct isec *rs, const struct rel *r)
{
	const struct rtype *rt;
	struct isym *sym;
	struct osec *o;
	unsigned char *loc;
	u64 S;
	u64 P;
	u64 A;
	u64 v;
	u64 got;
	u64 slot;

	rt = find_rtype(r->type);
	if (!rt || rt->calc == C_NONE)
		return;
	o = t->out;
	if (!o->buf || r->off > t->size || (u64)rt->size > t->size - r->off) {
		err("%s: relocation offset 0x%lx out of range in %s", in->name, r->off, t->name);
		return;
	}
	sym = &in->syms[r->sym];
	if (!sym_value(in, sym, &S)) {
		if (o->group >= 0 && strcmp(t->name, ".eh_frame") != 0) {
			err("%s: relocation in %s refers to a discarded section", in->name, t->name);
			return;
		}
		S = 0;
	}
	loc = o->buf + t->outoff + r->off;
	P = o->addr + t->outoff + r->off;
	if (rs->type == SHT_RELA)
		A = (u64)r->addend;
	else
		A = (u64)sext(rd(loc, rt->size), rt->size);
	got = osecs[O_GOT].addr;
	slot = 0;
	if (is_got_slot(rt->calc))
		slot = (u64)*slot_ref(sym) * (u64)word_size();
	else if (is_tp_slot(rt->calc))
		slot = (u64)*tp_ref(sym) * (u64)word_size();
	switch (rt->calc) {
	case C_ABS:
		v = S + A;
		break;
	case C_PCREL:
		v = S + A - P;
		break;
	case C_GOTSLOTPC:
		v = got + slot + A - P;
		break;
	case C_GOTSLOT:
		v = slot + A;
		break;
	case C_GOTOFF:
		v = S + A - got;
		break;
	case C_GOTPC:
		v = got + A - P;
		break;
	case C_TPOFF:
		v = tls_tpoff(S) + A;
		break;
	case C_TPOFFPOS:
		v = (u64)0 - tls_tpoff(S) + A;
		break;
	case C_TPSLOTPC:
		v = got + slot + A - P;
		break;
	case C_TPSLOT:
		v = slot + A;
		break;
	case C_TPSLOTABS:
		v = got + slot + A;
		break;
	default:
		return;
	}
	if (!fits((s64)v, rt->size, rt->chk)) {
		err("%s: relocation type %u out of range in %s+0x%lx", in->name, r->type, t->name, r->off);
		return;
	}
	wr(loc, rt->size, v);
}

void apply_relocs(void)
{
	walk_relocs(apply_one);
}

static int mark_sec(struct isec *s)
{
	if (!s->live && s->alias)
		s = s->alias;
	if (!s->live || s->mark)
		return 0;
	s->mark = 1;
	return 1;
}

static int mark_sym(const struct input *in, const struct isym *s)
{
	if (s->g) {
		if (s->g->state != G_DEF || s->g->shndx >= SHN_LORESERVE)
			return 0;
		return mark_sec(&s->g->obj->secs[s->g->shndx]);
	}
	if (s->shndx == SHN_UNDEF || s->shndx >= SHN_LORESERVE)
		return 0;
	return mark_sec(&in->secs[s->shndx]);
}

static int is_root(const struct isec *s)
{
	if (!(s->flags & SHF_ALLOC))
		return 1;
	if (s->flags & SHF_GNU_RETAIN)
		return 1;
	if (s->type == SHT_INIT_ARRAY || s->type == SHT_FINI_ARRAY || s->type == SHT_PREINIT_ARRAY)
		return 1;
	return strcmp(s->name, ".init") == 0 || strcmp(s->name, ".fini") == 0 ||
	       strcmp(s->name, ".eh_frame") == 0 || strncmp(s->name, ".ctors", 6) == 0 ||
	       strncmp(s->name, ".dtors", 6) == 0;
}

void gc_sections(void)
{
	struct input *in;
	struct isec *s;
	struct isec *rs;
	struct isec *t;
	struct gsym *g;
	struct rel r;
	long i;
	long n;
	int ii;
	int k;
	int changed;

	for (ii = 0; ii < ninputs; ii++) {
		in = inputs[ii];
		for (k = 0; k < in->nsec; k++) {
			s = &in->secs[k];
			s->mark = s->live && is_root(s);
			s->gcdone = 0;
		}
	}
	for (i = 0; i < ngsyms; i++) {
		g = gsyms[i];
		if (g->pull && !g->ref && g->state == G_DEF && g->shndx < SHN_LORESERVE)
			mark_sec(&g->obj->secs[g->shndx]);
	}
	do {
		changed = 0;
		for (ii = 0; ii < ninputs; ii++) {
			in = inputs[ii];
			for (k = 0; k < in->nsec; k++) {
				rs = &in->secs[k];
				if ((rs->type != SHT_REL && rs->type != SHT_RELA) || rs->gcdone)
					continue;
				if (rs->info >= (u32)in->nsec)
					continue;
				t = &in->secs[rs->info];
				if (!t->live || !t->mark || !(t->flags & SHF_ALLOC))
					continue;
				rs->gcdone = 1;
				if (strcmp(t->name, ".eh_frame") == 0)
					continue;
				n = (long)(rs->size / (u64)rel_entsize(in->cls, rs->type == SHT_RELA));
				for (i = 0; i < n; i++) {
					get_rel(in, rs, i, &r);
					if (r.sym < (u32)in->nsym && mark_sym(in, &in->syms[r.sym]))
						changed = 1;
				}
			}
		}
	} while (changed);
	for (ii = 0; ii < ninputs; ii++) {
		in = inputs[ii];
		for (k = 0; k < in->nsec; k++) {
			s = &in->secs[k];
			if (s->live && (s->flags & SHF_ALLOC) && !s->mark)
				s->live = 0;
		}
	}
}
