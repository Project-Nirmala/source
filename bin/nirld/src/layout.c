#include "ld.h"

struct osec osecs[O_COUNT];
struct osec **dbgs;
int ndbgs;
static int capdbgs;
struct seg segs[3];
int nphdr;
int have_tls;
u64 tls_addr;
u64 tls_off;
u64 tls_filesz;
u64 tls_memsz;
u64 tls_align = 1;
u64 image_base;
u64 file_end;
u64 etext_addr;
u64 edata_addr;
u64 bss_start_addr;
u64 end_addr;
u64 entry_addr;

static const struct {
	const char *name;
	u32 type;
	u64 flags;
	int group;
} odefs[O_COUNT] = {
	{ ".init", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0 },
	{ ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0 },
	{ ".fini", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0 },
	{ ".rodata", SHT_PROGBITS, SHF_ALLOC, 1 },
	{ ".tdata", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS, 2 },
	{ ".tbss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS, 2 },
	{ ".preinit_array", SHT_PREINIT_ARRAY, SHF_ALLOC | SHF_WRITE, 2 },
	{ ".init_array", SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE, 2 },
	{ ".fini_array", SHT_FINI_ARRAY, SHF_ALLOC | SHF_WRITE, 2 },
	{ ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 2 },
	{ ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 2 },
	{ ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 2 }
};

void osec_init(void)
{
	int i;

	for (i = 0; i < O_COUNT; i++) {
		osecs[i].name = odefs[i].name;
		osecs[i].type = odefs[i].type;
		osecs[i].flags = odefs[i].flags;
		osecs[i].group = odefs[i].group;
		osecs[i].align = 1;
	}
}

static int classify(const struct isec *s)
{
	if (s->flags & SHF_TLS)
		return s->type == SHT_NOBITS ? O_TBSS : O_TDATA;
	if (s->type == SHT_NOBITS)
		return O_BSS;
	if (s->type == SHT_PREINIT_ARRAY)
		return O_PREINIT;
	if (s->type == SHT_INIT_ARRAY)
		return O_INITARR;
	if (s->type == SHT_FINI_ARRAY)
		return O_FINIARR;
	if (s->flags & SHF_EXECINSTR) {
		if (strcmp(s->name, ".init") == 0)
			return O_INIT;
		if (strcmp(s->name, ".fini") == 0)
			return O_FINI;
		return O_TEXT;
	}
	if (s->flags & SHF_WRITE)
		return O_DATA;
	return O_RODATA;
}

static struct osec *dbg_get(const char *name, u32 type)
{
	struct osec *o;
	int i;

	for (i = 0; i < ndbgs; i++)
		if (strcmp(dbgs[i]->name, name) == 0)
			return dbgs[i];
	o = xalloc(sizeof(*o));
	o->name = xstrdup(name);
	o->type = type;
	o->group = -1;
	o->align = 1;
	if (ndbgs == capdbgs) {
		capdbgs = capdbgs ? capdbgs * 2 : 16;
		dbgs = xrealloc(dbgs, sizeof(*dbgs) * (size_t)capdbgs);
	}
	dbgs[ndbgs++] = o;
	return o;
}

void osec_add(struct input *in, struct isec *s)
{
	struct osec *o;

	(void)in;
	if (s->flags & SHF_ALLOC)
		o = &osecs[classify(s)];
	else
		o = dbg_get(s->name, s->type);
	if (o->nsecs == o->capsecs) {
		o->capsecs = o->capsecs ? o->capsecs * 2 : 16;
		o->secs = xrealloc(o->secs, sizeof(*o->secs) * (size_t)o->capsecs);
	}
	s->out = o;
	o->secs[o->nsecs++] = s;
}

static void place_one(struct osec *o)
{
	struct isec *s;
	long j;

	o->size = 0;
	o->align = 1;
	for (j = 0; j < o->nsecs; j++) {
		s = o->secs[j];
		if (!s->live)
			continue;
		s->outoff = align_up(o->size, s->align);
		o->size = s->outoff + s->size;
		if (s->align > o->align)
			o->align = s->align;
		if (o->size > ((u64)1 << 40))
			die("output section %s is too large", o->name);
	}
}

void place_sections(void)
{
	int i;

	for (i = 0; i < O_COUNT; i++)
		place_one(&osecs[i]);
	for (i = 0; i < ndbgs; i++)
		place_one(dbgs[i]);
}

void alloc_commons(void)
{
	struct osec *o;
	struct gsym *g;
	long i;

	o = &osecs[O_BSS];
	for (i = 0; i < ngsyms; i++) {
		g = gsyms[i];
		if (g->state != G_COMMON)
			continue;
		g->commonoff = align_up(o->size, g->align);
		o->size = g->commonoff + g->size;
		if (g->align > o->align)
			o->align = g->align;
	}
}

u64 tls_tpoff(u64 s)
{
	return (s - tls_addr) - align_up(tls_memsz, tls_align);
}

void layout(void)
{
	struct osec *o;
	u64 grpsize[3];
	u64 hdr;
	u64 a0;
	u64 off0;
	u64 base;
	u64 cur_off;
	u64 cur_addr;
	u64 d;
	int ehsz;
	int phsz;
	int i;
	int g;
	int lastg;

	if (opt_noseparate)
		osecs[O_RODATA].group = 0;
	if (opt_binary)
		for (i = 0; i < O_COUNT; i++)
			osecs[i].group = 0;
	for (g = 0; g < 3; g++)
		grpsize[g] = 0;
	a0 = 1;
	for (i = 0; i < O_COUNT; i++) {
		o = &osecs[i];
		if (o->align > pagesize)
			die("section %s alignment %lu exceeds the page size", o->name, o->align);
		grpsize[o->group] += o->size;
		if (o->group == 0 && o->size && o->align > a0)
			a0 = o->align;
	}
	have_tls = osecs[O_TDATA].size || osecs[O_TBSS].size;
	segs[0].used = 1;
	segs[1].used = grpsize[1] > 0;
	segs[2].used = grpsize[2] > 0;
	segs[0].flags = PF_R | PF_X;
	segs[1].flags = PF_R;
	segs[2].flags = PF_R | PF_W;
	ehsz = mach->cls == ELFCLASS32 ? 52 : 64;
	phsz = mach->cls == ELFCLASS32 ? 32 : 56;
	if (opt_binary) {
		nphdr = 0;
		hdr = 0;
	} else {
		nphdr = 1 + segs[1].used + segs[2].used + (have_tls ? 1 : 0) + 1;
		hdr = (u64)ehsz + (u64)nphdr * (u64)phsz;
	}

	if (opt_binary) {
		base = have_ttext ? opt_ttext : mach->base;
		off0 = 0;
	} else if (have_ttext) {
		if (opt_ttext % a0)
			die("-Ttext address 0x%lx is not aligned to %lu", opt_ttext, a0);
		off0 = opt_ttext % pagesize;
		if (off0 < hdr)
			off0 += pagesize;
		if (off0 > opt_ttext)
			die("-Ttext address 0x%lx is too low", opt_ttext);
		base = opt_ttext - off0;
	} else {
		base = mach->base;
		off0 = hdr;
	}
	image_base = base;
	cur_off = off0;
	cur_addr = base + off0;
	segs[0].off = 0;
	segs[0].addr = base;
	segs[0].fileend = hdr;
	segs[0].memend = base + hdr;
	edata_addr = base + hdr;
	lastg = 0;

	for (i = 0; i < O_COUNT; i++) {
		o = &osecs[i];
		g = o->group;
		if (g != lastg) {
			lastg = g;
			if (segs[g].used) {
				cur_addr = align_up(cur_addr, pagesize) + cur_off % pagesize;
				segs[g].off = cur_off;
				segs[g].addr = cur_addr;
				segs[g].fileend = cur_off;
				segs[g].memend = cur_addr;
			}
		}
		if (o->size == 0) {
			o->off = cur_off;
			o->addr = cur_addr;
			continue;
		}
		if (i == O_TBSS) {
			o->off = cur_off;
			o->addr = align_up(cur_addr, o->align);
			continue;
		}
		if (o->type == SHT_NOBITS) {
			cur_addr = align_up(cur_addr, o->align);
			o->off = cur_off;
			o->addr = cur_addr;
			cur_addr += o->size;
		} else {
			d = align_up(cur_addr, o->align) - cur_addr;
			cur_off += d;
			cur_addr += d;
			o->off = cur_off;
			o->addr = cur_addr;
			cur_off += o->size;
			cur_addr += o->size;
			segs[g].fileend = cur_off;
			edata_addr = cur_addr;
		}
		segs[g].memend = cur_addr;
	}
	for (g = 0; g < 3; g++) {
		segs[g].filesz = segs[g].fileend - segs[g].off;
		segs[g].memsz = segs[g].memend - segs[g].addr;
	}
	if (have_tls) {
		o = &osecs[O_TDATA];
		tls_addr = o->size ? o->addr : osecs[O_TBSS].addr;
		tls_off = o->size ? o->off : osecs[O_TBSS].off;
		tls_filesz = o->size;
		tls_memsz = (o->size ? o->addr + o->size : tls_addr) - tls_addr;
		if (osecs[O_TBSS].size)
			tls_memsz = osecs[O_TBSS].addr + osecs[O_TBSS].size - tls_addr;
		tls_align = osecs[O_TDATA].align > osecs[O_TBSS].align ?
			    osecs[O_TDATA].align : osecs[O_TBSS].align;
	}
	file_end = cur_off;
	etext_addr = segs[0].memend;
	end_addr = cur_addr;
	bss_start_addr = osecs[O_BSS].size ? osecs[O_BSS].addr : edata_addr;
	if (mach->cls == ELFCLASS32 && end_addr > 0xffffffffUL)
		die("image does not fit in a 32-bit address space");
}

static void def_abs(const char *name, u64 v)
{
	struct gsym *g;

	g = gsym_find(name);
	if (!g || g->state != G_UNDEF)
		return;
	g->state = G_DEF;
	g->linker = 1;
	g->bind = STB_GLOBAL;
	g->type = STT_NOTYPE;
	g->shndx = SHN_ABS;
	g->addr = v;
}

void define_linker_syms(void)
{
	def_abs("__executable_start", image_base);
	def_abs("__ehdr_start", image_base);
	def_abs("etext", etext_addr);
	def_abs("_etext", etext_addr);
	def_abs("__etext", etext_addr);
	def_abs("edata", edata_addr);
	def_abs("_edata", edata_addr);
	def_abs("__bss_start", bss_start_addr);
	def_abs("end", end_addr);
	def_abs("_end", end_addr);
	def_abs("__end", end_addr);
	def_abs("__preinit_array_start", osecs[O_PREINIT].addr);
	def_abs("__preinit_array_end", osecs[O_PREINIT].addr + osecs[O_PREINIT].size);
	def_abs("__init_array_start", osecs[O_INITARR].addr);
	def_abs("__init_array_end", osecs[O_INITARR].addr + osecs[O_INITARR].size);
	def_abs("__fini_array_start", osecs[O_FINIARR].addr);
	def_abs("__fini_array_end", osecs[O_FINIARR].addr + osecs[O_FINIARR].size);
	def_abs("_GLOBAL_OFFSET_TABLE_", osecs[O_GOT].addr);
}

void finalize_syms(void)
{
	struct gsym *g;
	struct isec *s;
	long i;

	for (i = 0; i < ngsyms; i++) {
		g = gsyms[i];
		if (g->linker)
			continue;
		if (g->state == G_DEF) {
			if (g->shndx == SHN_ABS) {
				g->addr = g->value;
				continue;
			}
			s = &g->obj->secs[g->shndx];
			if (!s->live) {
				g->state = G_UNDEF;
				g->pull = 0;
				g->addr = 0;
				continue;
			}
			g->osec = s->out;
			g->addr = s->out->addr + s->outoff + g->value;
		} else if (g->state == G_COMMON) {
			g->osec = &osecs[O_BSS];
			g->addr = osecs[O_BSS].addr + g->commonoff;
		} else {
			g->addr = 0;
		}
	}
}

static int defsym_value(const char *expr, u64 *out)
{
	struct gsym *g;
	char *end;
	char *name;
	size_t n;
	u64 v;
	u64 add;

	v = strtoul(expr, &end, 0);
	if (*expr && !*end) {
		*out = v;
		return 1;
	}
	n = strcspn(expr, "+-");
	name = xalloc(n + 1);
	memcpy(name, expr, n);
	g = gsym_find(name);
	if (!g || g->state == G_UNDEF)
		return 0;
	v = g->addr;
	if (expr[n]) {
		if (!expr[n + 1])
			return 0;
		add = strtoul(expr + n + 1, &end, 0);
		if (*end)
			return 0;
		v = expr[n] == '-' ? v - add : v + add;
	}
	*out = v;
	return 1;
}

void apply_defsyms(void)
{
	struct gsym *g;
	char *name;
	char *eq;
	u64 v;
	int i;

	for (i = 0; i < ndefsyms; i++) {
		eq = strchr(defsyms[i], '=');
		if (!eq || eq == defsyms[i]) {
			err("--defsym expects SYMBOL=EXPRESSION, got '%s'", defsyms[i]);
			continue;
		}
		name = xalloc((size_t)(eq - defsyms[i]) + 1);
		memcpy(name, defsyms[i], (size_t)(eq - defsyms[i]));
		if (!defsym_value(eq + 1, &v)) {
			err("--defsym %s: cannot evaluate '%s'", name, eq + 1);
			continue;
		}
		g = gsym_get(name);
		g->state = G_DEF;
		g->linker = 1;
		g->pull = 0;
		g->bind = STB_GLOBAL;
		g->type = STT_NOTYPE;
		g->shndx = SHN_ABS;
		g->addr = v;
	}
}

void report_undefined(void)
{
	struct gsym *g;
	long i;

	if (opt_ignore_undef)
		return;
	for (i = 0; i < ngsyms; i++) {
		g = gsyms[i];
		if (g->state != G_UNDEF || !g->pull)
			continue;
		if (strcmp(g->name, opt_entry) == 0)
			continue;
		if (g->ref)
			err("%s: undefined reference to `%s'", g->ref->name, g->name);
		else
			err("undefined symbol `%s'", g->name);
	}
}

int sym_value(const struct input *in, const struct isym *s, u64 *out)
{
	const struct isec *sec;

	if (s->g) {
		*out = s->g->addr;
		return 1;
	}
	if (s->shndx == SHN_UNDEF) {
		*out = 0;
		return 1;
	}
	if (s->shndx == SHN_ABS) {
		*out = s->value;
		return 1;
	}
	if (s->shndx >= SHN_LORESERVE)
		return 0;
	sec = &in->secs[s->shndx];
	if (!sec->live && sec->alias)
		sec = sec->alias;
	if (!sec->live || !sec->out)
		return 0;
	*out = sec->out->addr + sec->outoff + s->value;
	return 1;
}

void build_sections(void)
{
	struct osec *o;
	struct isec *s;
	long j;
	int i;

	for (i = 0; i < O_COUNT + ndbgs; i++) {
		o = i < O_COUNT ? &osecs[i] : dbgs[i - O_COUNT];
		if (o->size == 0 || o->type == SHT_NOBITS)
			continue;
		o->buf = xalloc((size_t)o->size);
		for (j = 0; j < o->nsecs; j++) {
			s = o->secs[j];
			if (s->live && s->size)
				memcpy(o->buf + s->outoff, s->obj->buf + s->offset, (size_t)s->size);
		}
	}
}

static int cmp_gsym(const void *a, const void *b)
{
	const struct gsym *x;
	const struct gsym *y;

	x = *(struct gsym * const *)a;
	y = *(struct gsym * const *)b;
	if (x->addr != y->addr)
		return x->addr < y->addr ? -1 : 1;
	return strcmp(x->name, y->name);
}

void write_map(const char *path)
{
	struct gsym **list;
	struct osec *o;
	struct isec *s;
	struct input *in;
	FILE *f;
	long n;
	long j;
	int i;
	int k;

	f = fopen(path, "w");
	if (!f)
		die("cannot create %s", path);
	fprintf(f, "Linker map (nirld, %s)\n\nOutput sections\n", mach->emul);
	for (i = 0; i < O_COUNT + ndbgs; i++) {
		o = i < O_COUNT ? &osecs[i] : dbgs[i - O_COUNT];
		if (o->size == 0)
			continue;
		fprintf(f, "%-16s 0x%016lx 0x%08lx\n", o->name, o->addr, o->size);
		for (j = 0; j < o->nsecs; j++) {
			s = o->secs[j];
			if (!s->live || s->size == 0)
				continue;
			fprintf(f, "  %-24s 0x%016lx 0x%08lx %s\n", s->name,
				o->addr + s->outoff, s->size, s->obj->name);
		}
	}
	fprintf(f, "\nDiscarded input sections\n");
	for (k = 0; k < ninputs; k++) {
		in = inputs[k];
		for (i = 0; i < in->nsec; i++) {
			s = &in->secs[i];
			if (s->live || !(s->flags & SHF_ALLOC) || s->type == SHT_NOTE || s->size == 0)
				continue;
			fprintf(f, "  %-24s 0x%08lx %s\n", s->name, s->size, in->name);
		}
	}
	list = xalloc(sizeof(*list) * (size_t)(ngsyms ? ngsyms : 1));
	n = 0;
	for (j = 0; j < ngsyms; j++)
		if (gsyms[j]->state != G_UNDEF)
			list[n++] = gsyms[j];
	qsort(list, (size_t)n, sizeof(*list), cmp_gsym);
	fprintf(f, "\nSymbols\n");
	for (j = 0; j < n; j++)
		fprintf(f, "0x%016lx %s\n", list[j]->addr, list[j]->name);
	fclose(f);
}
