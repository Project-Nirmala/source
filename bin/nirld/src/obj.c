#include "ld.h"

struct input **inputs;
int ninputs;
static int capinputs;

static char *get_str(const char *who, const unsigned char *tab, u64 tabsize, u64 off)
{
	if (off >= tabsize)
		die("%s: string offset out of range", who);
	if (!memchr(tab + off, 0, (size_t)(tabsize - off)))
		die("%s: unterminated string", who);
	return xstrdup((const char *)(tab + off));
}

static void read_shdr(const struct input *in, const unsigned char *h, struct isec *s)
{
	if (in->cls == ELFCLASS32) {
		s->nameoff = (u32)rd(h, 4);
		s->type = (u32)rd(h + 4, 4);
		s->flags = rd(h + 8, 4);
		s->offset = rd(h + 16, 4);
		s->size = rd(h + 20, 4);
		s->link = (u32)rd(h + 24, 4);
		s->info = (u32)rd(h + 28, 4);
		s->align = rd(h + 32, 4);
	} else {
		s->nameoff = (u32)rd(h, 4);
		s->type = (u32)rd(h + 4, 4);
		s->flags = rd(h + 8, 8);
		s->offset = rd(h + 24, 8);
		s->size = rd(h + 32, 8);
		s->link = (u32)rd(h + 40, 4);
		s->info = (u32)rd(h + 44, 4);
		s->align = rd(h + 48, 8);
	}
	if (s->align == 0)
		s->align = 1;
	if ((s->align & (s->align - 1)) != 0 || s->align > 0x100000UL)
		die("%s: invalid section alignment %lu", in->name, s->align);
}

static void read_syms(struct input *in)
{
	struct isec *st;
	struct isec *str;
	struct isym *s;
	const unsigned char *p;
	u64 nameoff;
	u64 shndx;
	int symsz;
	int info;
	int i;

	for (i = 0; i < in->nsec; i++)
		if (in->secs[i].type == SHT_SYMTAB)
			break;
	if (i == in->nsec)
		return;
	st = &in->secs[i];
	in->symsec = i;
	if (st->link >= (u32)in->nsec || in->secs[st->link].type != SHT_STRTAB)
		die("%s: bad symbol string table", in->name);
	str = &in->secs[st->link];
	symsz = in->cls == ELFCLASS32 ? 16 : 24;
	in->nsym = (int)(st->size / (u64)symsz);
	in->syms = xalloc(sizeof(struct isym) * (size_t)in->nsym);
	if (st->info > (u32)in->nsym)
		die("%s: bad symbol table", in->name);
	in->firstglobal = (int)st->info;
	for (i = 0; i < in->nsym; i++) {
		p = in->buf + st->offset + (size_t)i * (size_t)symsz;
		s = &in->syms[i];
		if (in->cls == ELFCLASS32) {
			nameoff = rd(p, 4);
			s->value = rd(p + 4, 4);
			s->size = rd(p + 8, 4);
			info = p[12];
			shndx = rd(p + 14, 2);
		} else {
			nameoff = rd(p, 4);
			info = p[4];
			shndx = rd(p + 6, 2);
			s->value = rd(p + 8, 8);
			s->size = rd(p + 16, 8);
		}
		if (shndx == SHN_XINDEX)
			die("%s: extended section indices are not supported", in->name);
		if (shndx < SHN_LORESERVE && shndx >= (u64)in->nsec)
			die("%s: symbol with bad section index", in->name);
		s->name = get_str(in->name, in->buf + str->offset, str->size, nameoff);
		s->bind = info >> 4;
		s->type = info & 15;
		s->shndx = (u32)shndx;
		s->got = -1;
		s->gottp = -1;
	}
}

struct input *obj_parse(const char *name, const unsigned char *buf, size_t len)
{
	struct input *in;
	const unsigned char *strtab;
	u64 shoff;
	u64 shnum;
	u64 shentsize;
	u64 shstrndx;
	int ehsz;
	int shsz;
	int i;

	if (len < 16 || memcmp(buf, "\177ELF", 4) != 0)
		die("%s: file format not recognized", name);
	in = xalloc(sizeof(*in));
	in->name = xstrdup(name);
	in->buf = buf;
	in->len = len;
	in->cls = buf[4];
	if (in->cls != ELFCLASS32 && in->cls != ELFCLASS64)
		die("%s: invalid ELF class", name);
	if (buf[5] != ELFDATA2LSB)
		die("%s: only little-endian objects are supported", name);
	ehsz = in->cls == ELFCLASS32 ? 52 : 64;
	shsz = in->cls == ELFCLASS32 ? 40 : 64;
	if (len < (size_t)ehsz)
		die("%s: truncated ELF header", name);
	if (rd(buf + 16, 2) != ET_REL)
		die("%s: not a relocatable object", name);
	in->machine = (int)rd(buf + 18, 2);
	if (in->cls == ELFCLASS32) {
		shoff = rd(buf + 32, 4);
		shentsize = rd(buf + 46, 2);
		shnum = rd(buf + 48, 2);
		shstrndx = rd(buf + 50, 2);
	} else {
		shoff = rd(buf + 40, 8);
		shentsize = rd(buf + 58, 2);
		shnum = rd(buf + 60, 2);
		shstrndx = rd(buf + 62, 2);
	}
	if (shoff == 0 || shnum == 0)
		die("%s: object has no section headers", name);
	if (shentsize < (u64)shsz || shoff > len || shnum * shentsize > len - shoff)
		die("%s: truncated section header table", name);
	if (shstrndx >= shnum)
		die("%s: bad section name table index", name);
	in->nsec = (int)shnum;
	in->secs = xalloc(sizeof(struct isec) * (size_t)in->nsec);
	for (i = 0; i < in->nsec; i++) {
		read_shdr(in, buf + shoff + (size_t)i * (size_t)shentsize, &in->secs[i]);
		in->secs[i].obj = in;
		if (in->secs[i].type != SHT_NOBITS && in->secs[i].type != SHT_NULL &&
		    (in->secs[i].offset > len || in->secs[i].size > len - in->secs[i].offset))
			die("%s: section %d extends past end of file", name, i);
	}
	if (in->secs[shstrndx].type != SHT_STRTAB)
		die("%s: bad section name table", name);
	strtab = buf + in->secs[shstrndx].offset;
	for (i = 0; i < in->nsec; i++)
		in->secs[i].name = get_str(name, strtab, in->secs[shstrndx].size,
					   in->secs[i].nameoff);
	read_syms(in);
	return in;
}

static int is_data_sec(const struct isec *s)
{
	switch (s->type) {
	case SHT_NULL:
	case SHT_SYMTAB:
	case SHT_STRTAB:
	case SHT_RELA:
	case SHT_REL:
	case SHT_GROUP:
	case SHT_NOTE:
		return 0;
	}
	if (s->flags & SHF_COMPRESSED)
		return 0;
	if (s->flags & SHF_ALLOC)
		return 1;
	if (!opt_debug || s->type != SHT_PROGBITS)
		return 0;
	return strncmp(s->name, ".debug_", 7) == 0 || strcmp(s->name, ".comment") == 0;
}

static struct isec *find_alias(const struct comdat *c, const char *name)
{
	const struct isec *g;
	struct isec *m;
	const unsigned char *p;
	u64 n;
	u64 j;
	u64 idx;

	g = &c->obj->secs[c->grp];
	p = c->obj->buf + g->offset;
	n = g->size / 4 - 1;
	for (j = 0; j < n; j++) {
		idx = rd(p + 4 + 4 * j, 4);
		if (idx >= (u64)c->obj->nsec)
			continue;
		m = &c->obj->secs[idx];
		if (m->live && strcmp(m->name, name) == 0)
			return m;
	}
	return NULL;
}

static void drop_duplicate_groups(struct input *in)
{
	struct isec *g;
	struct comdat *kept;
	const unsigned char *p;
	u64 n;
	u64 j;
	u64 idx;
	int i;

	for (i = 0; i < in->nsec; i++) {
		g = &in->secs[i];
		if (g->type != SHT_GROUP)
			continue;
		if (g->size < 4 || g->info >= (u32)in->nsym)
			die("%s: bad section group", in->name);
		p = in->buf + g->offset;
		if (!(rd(p, 4) & 1))
			continue;
		kept = comdat_seen(in->syms[g->info].name, in, i);
		if (!kept)
			continue;
		n = g->size / 4 - 1;
		for (j = 0; j < n; j++) {
			idx = rd(p + 4 + 4 * j, 4);
			if (idx >= (u64)in->nsec)
				continue;
			in->secs[idx].live = 0;
			in->secs[idx].alias = find_alias(kept, in->secs[idx].name);
		}
	}
}

void obj_load(struct input *in)
{
	struct isec *s;
	int i;

	if (in->loaded)
		return;
	in->loaded = 1;
	if (!mach) {
		mach = machine_by_em(in->machine);
		if (!mach)
			die("%s: unsupported machine type %d", in->name, in->machine);
	}
	if (in->machine != mach->em || in->cls != mach->cls)
		die("%s: incompatible with target %s", in->name, mach->emul);
	if (ninputs == capinputs) {
		capinputs = capinputs ? capinputs * 2 : 16;
		inputs = xrealloc(inputs, sizeof(*inputs) * (size_t)capinputs);
	}
	inputs[ninputs++] = in;
	for (i = 0; i < in->nsec; i++)
		in->secs[i].live = is_data_sec(&in->secs[i]);
	drop_duplicate_groups(in);
	for (i = 0; i < in->nsec; i++) {
		s = &in->secs[i];
		if (!s->live)
			continue;
		osec_add(in, s);
	}
	for (i = in->firstglobal; i < in->nsym; i++)
		sym_resolve(in, &in->syms[i]);
}

static u64 ar_num(const unsigned char *p, int n)
{
	u64 v;
	int i;

	v = 0;
	for (i = 0; i < n && p[i] >= '0' && p[i] <= '9'; i++)
		v = v * 10 + (u64)(p[i] - '0');
	return v;
}

static void ar_add(struct archive *a, const char *name, size_t off, size_t size)
{
	struct member *m;

	if (a->nmem == a->capmem) {
		a->capmem = a->capmem ? a->capmem * 2 : 32;
		a->mem = xrealloc(a->mem, sizeof(*a->mem) * (size_t)a->capmem);
	}
	m = &a->mem[a->nmem++];
	memset(m, 0, sizeof(*m));
	m->name = xstrdup(name);
	m->off = off;
	m->size = size;
}

struct archive *archive_open(const char *path, const unsigned char *buf, size_t len)
{
	struct archive *a;
	const unsigned char *h;
	const char *longnames;
	char field[17];
	char *name;
	size_t nlong;
	size_t pos;
	size_t size;
	size_t dstart;
	size_t n;
	size_t k;

	a = xalloc(sizeof(*a));
	a->name = xstrdup(path);
	a->buf = buf;
	a->len = len;
	longnames = NULL;
	nlong = 0;
	pos = 8;
	while (pos + 60 <= len) {
		h = buf + pos;
		if (h[58] != '`' || h[59] != '\n')
			die("%s: corrupt archive header", path);
		size = (size_t)ar_num(h + 48, 10);
		dstart = pos + 60;
		if (size > len - dstart)
			die("%s: truncated archive", path);
		memcpy(field, h, 16);
		field[16] = 0;
		pos = dstart + size;
		if (pos & 1)
			pos++;
		if (field[0] == '/' && (field[1] == ' ' || field[1] == 0))
			continue;
		if (field[0] == '/' && field[1] == 'S')
			continue;
		if (field[0] == '/' && field[1] == '/') {
			longnames = (const char *)(buf + dstart);
			nlong = size;
			continue;
		}
		if (strncmp(field, "__.SYMDEF", 9) == 0)
			continue;
		name = NULL;
		if (field[0] == '/' && field[1] >= '0' && field[1] <= '9') {
			n = (size_t)strtoul(field + 1, NULL, 10);
			if (!longnames || n >= nlong)
				die("%s: bad long member name", path);
			for (k = n; k < nlong && longnames[k] != '/' && longnames[k] != '\n'; k++)
				;
			name = xalloc(k - n + 1);
			memcpy(name, longnames + n, k - n);
		} else if (strncmp(field, "#1/", 3) == 0) {
			n = (size_t)strtoul(field + 3, NULL, 10);
			if (n > size)
				die("%s: bad member name", path);
			name = xalloc(n + 1);
			memcpy(name, buf + dstart, n);
			dstart += n;
			size -= n;
		} else {
			for (k = 0; k < 16 && field[k] != '/' && field[k] != ' ' && field[k]; k++)
				;
			field[k] = 0;
			name = xstrdup(field);
		}
		ar_add(a, name, dstart, size);
	}
	return a;
}

static int defines_needed(const struct input *in)
{
	const struct isym *s;
	const struct gsym *g;
	int i;

	for (i = in->firstglobal; i < in->nsym; i++) {
		s = &in->syms[i];
		if (s->shndx == SHN_UNDEF || s->name[0] == 0)
			continue;
		g = gsym_find(s->name);
		if (g && g->state == G_UNDEF && g->pull)
			return 1;
	}
	return 0;
}

int ar_scan(struct archive *a)
{
	struct member *m;
	char *full;
	size_t n1;
	size_t n2;
	int i;
	int loaded;

	loaded = 0;
	for (i = 0; i < a->nmem; i++) {
		m = &a->mem[i];
		if (m->loaded)
			continue;
		if (!m->obj) {
			n1 = strlen(a->name);
			n2 = strlen(m->name);
			full = xalloc(n1 + n2 + 3);
			memcpy(full, a->name, n1);
			full[n1] = '(';
			memcpy(full + n1 + 1, m->name, n2);
			full[n1 + n2 + 1] = ')';
			m->obj = obj_parse(full, a->buf + m->off, m->size);
		}
		if (defines_needed(m->obj)) {
			m->loaded = 1;
			obj_load(m->obj);
			loaded++;
		}
	}
	return loaded;
}
