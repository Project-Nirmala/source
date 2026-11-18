#include "ld.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#define HAVE_CHMOD 1
#endif

static int cls;

static void put_sym(struct buf *b, u64 name, u64 value, u64 size, int info, u32 shndx)
{
	unsigned char t[24];

	memset(t, 0, sizeof(t));
	if (cls == ELFCLASS32) {
		wr(t, 4, name);
		wr(t + 4, 4, value);
		wr(t + 8, 4, size);
		t[12] = (unsigned char)info;
		wr(t + 14, 2, shndx);
		buf_add(b, t, 16);
	} else {
		wr(t, 4, name);
		t[4] = (unsigned char)info;
		wr(t + 6, 2, shndx);
		wr(t + 8, 8, value);
		wr(t + 16, 8, size);
		buf_add(b, t, 24);
	}
}

static void put_phdr(unsigned char *p, u32 type, u64 flags, u64 off, u64 vaddr,
		     u64 filesz, u64 memsz, u64 align)
{
	if (cls == ELFCLASS32) {
		wr(p, 4, type);
		wr(p + 4, 4, off);
		wr(p + 8, 4, vaddr);
		wr(p + 12, 4, vaddr);
		wr(p + 16, 4, filesz);
		wr(p + 20, 4, memsz);
		wr(p + 24, 4, flags);
		wr(p + 28, 4, align);
	} else {
		wr(p, 4, type);
		wr(p + 4, 4, flags);
		wr(p + 8, 8, off);
		wr(p + 16, 8, vaddr);
		wr(p + 24, 8, vaddr);
		wr(p + 32, 8, filesz);
		wr(p + 40, 8, memsz);
		wr(p + 48, 8, align);
	}
}

static void put_shdr(unsigned char *p, u64 name, u32 type, u64 flags, u64 addr, u64 off,
		     u64 size, u32 link, u32 info, u64 align, u64 entsize)
{
	if (cls == ELFCLASS32) {
		wr(p, 4, name);
		wr(p + 4, 4, type);
		wr(p + 8, 4, flags);
		wr(p + 12, 4, addr);
		wr(p + 16, 4, off);
		wr(p + 20, 4, size);
		wr(p + 24, 4, link);
		wr(p + 28, 4, info);
		wr(p + 32, 4, align);
		wr(p + 36, 4, entsize);
	} else {
		wr(p, 4, name);
		wr(p + 4, 4, type);
		wr(p + 8, 8, flags);
		wr(p + 16, 8, addr);
		wr(p + 24, 8, off);
		wr(p + 32, 8, size);
		wr(p + 40, 4, link);
		wr(p + 44, 4, info);
		wr(p + 48, 8, align);
		wr(p + 56, 8, entsize);
	}
}

static struct osec *sec_at(int i)
{
	return i < O_COUNT ? &osecs[i] : dbgs[i - O_COUNT];
}

static u32 shndx_of(const struct osec *o)
{
	return o && o->shidx ? (u32)o->shidx : SHN_ABS;
}

static u32 build_symtab(struct buf *sym, struct buf *str)
{
	const struct input *in;
	const struct isym *s;
	const struct gsym *g;
	const struct isec *sec;
	u64 v;
	u32 nloc;
	u32 shndx;
	int i;
	int k;

	put_sym(sym, 0, 0, 0, 0, 0);
	nloc = 1;
	for (k = 0; k < ninputs; k++) {
		in = inputs[k];
		for (i = 1; i < in->firstglobal; i++) {
			s = &in->syms[i];
			if (s->type == STT_SECTION || s->type == STT_FILE || s->name[0] == 0)
				continue;
			if (!sym_value(in, s, &v))
				continue;
			if (s->shndx == SHN_ABS) {
				shndx = SHN_ABS;
			} else if (s->shndx < SHN_LORESERVE) {
				sec = &in->secs[s->shndx];
				if (!sec->out || !sec->out->shidx)
					continue;
				shndx = (u32)sec->out->shidx;
			} else {
				continue;
			}
			if (s->type == STT_TLS)
				v -= tls_addr;
			put_sym(sym, buf_str(str, s->name), v, s->size,
				(STB_LOCAL << 4) | s->type, shndx);
			nloc++;
		}
	}
	for (i = 0; i < ngsyms; i++) {
		g = gsyms[i];
		if (g->state == G_UNDEF)
			continue;
		put_sym(sym, buf_str(str, g->name),
			g->type == STT_TLS ? g->addr - tls_addr : g->addr, g->size,
			(g->bind << 4) | (g->type & 15), shndx_of(g->osec));
	}
	return nloc;
}

static void finish_file(const char *path, const unsigned char *img, u64 total)
{
	FILE *f;
#ifdef HAVE_CHMOD
	mode_t m;
#endif

	f = fopen(path, "wb");
	if (!f)
		die("cannot create %s", path);
	if (fwrite(img, 1, (size_t)total, f) != (size_t)total)
		die("write error on %s", path);
	if (fclose(f) != 0)
		die("write error on %s", path);
#ifdef HAVE_CHMOD
	m = umask(0);
	umask(m);
	chmod(path, 0777 & ~m);
#endif
}

static void write_binary(const char *path)
{
	unsigned char *img;
	struct osec *o;
	int i;

	img = xalloc((size_t)file_end);
	for (i = 0; i < O_COUNT; i++) {
		o = &osecs[i];
		if (o->size && o->buf)
			memcpy(img + o->off, o->buf, (size_t)o->size);
	}
	finish_file(path, img, file_end);
}

static void put_ehdr(unsigned char *img, u64 shoff, int nsh, int shstridx)
{
	int ehsz;
	int phsz;
	int shsz;

	ehsz = cls == ELFCLASS32 ? 52 : 64;
	phsz = cls == ELFCLASS32 ? 32 : 56;
	shsz = cls == ELFCLASS32 ? 40 : 64;
	memcpy(img, "\177ELF", 4);
	img[4] = (unsigned char)cls;
	img[5] = ELFDATA2LSB;
	img[6] = 1;
	wr(img + 16, 2, ET_EXEC);
	wr(img + 18, 2, (u64)mach->em);
	wr(img + 20, 4, 1);
	if (cls == ELFCLASS32) {
		wr(img + 24, 4, entry_addr);
		wr(img + 28, 4, (u64)ehsz);
		wr(img + 32, 4, shoff);
		wr(img + 40, 2, (u64)ehsz);
		wr(img + 42, 2, (u64)phsz);
		wr(img + 44, 2, (u64)nphdr);
		wr(img + 46, 2, (u64)shsz);
		wr(img + 48, 2, (u64)nsh);
		wr(img + 50, 2, (u64)shstridx);
	} else {
		wr(img + 24, 8, entry_addr);
		wr(img + 32, 8, (u64)ehsz);
		wr(img + 40, 8, shoff);
		wr(img + 52, 2, (u64)ehsz);
		wr(img + 54, 2, (u64)phsz);
		wr(img + 56, 2, (u64)nphdr);
		wr(img + 58, 2, (u64)shsz);
		wr(img + 60, 2, (u64)nsh);
		wr(img + 62, 2, (u64)shstridx);
	}
}

void write_output(const char *path)
{
	struct buf sym;
	struct buf str;
	struct buf shstr;
	unsigned char *img;
	unsigned char *p;
	struct osec *o;
	u64 total;
	u64 pos;
	u64 symoff;
	u64 stroff;
	u64 shstroff;
	u64 shoff;
	u64 nloc;
	u64 n_sym;
	u64 n_str;
	u64 n_shs;
	int ehsz;
	int phsz;
	int shsz;
	int symsz;
	int nsh;
	int stridx;
	int shstridx;
	int i;

	if (opt_binary) {
		write_binary(path);
		return;
	}
	cls = mach->cls;
	ehsz = cls == ELFCLASS32 ? 52 : 64;
	phsz = cls == ELFCLASS32 ? 32 : 56;
	shsz = cls == ELFCLASS32 ? 40 : 64;
	symsz = cls == ELFCLASS32 ? 16 : 24;
	memset(&sym, 0, sizeof(sym));
	memset(&str, 0, sizeof(str));
	memset(&shstr, 0, sizeof(shstr));
	buf_add(&str, "", 1);
	buf_add(&shstr, "", 1);

	pos = file_end;
	for (i = O_COUNT; i < O_COUNT + ndbgs; i++) {
		o = sec_at(i);
		if (o->size == 0)
			continue;
		o->off = align_up(pos, o->align);
		pos = o->off + o->size;
	}
	nsh = 1;
	for (i = 0; i < O_COUNT + ndbgs; i++) {
		o = sec_at(i);
		o->shidx = 0;
		if (o->size == 0)
			continue;
		o->shidx = nsh++;
		o->nameoff = buf_str(&shstr, o->name);
	}
	stridx = 0;
	nloc = 0;
	if (!opt_strip) {
		nloc = build_symtab(&sym, &str);
		nsh++;
		stridx = nsh++;
	}
	shstridx = nsh++;

	symoff = stroff = 0;
	if (!opt_strip) {
		symoff = align_up(pos, 8);
		stroff = symoff + sym.len;
		pos = stroff + str.len;
	}
	n_sym = n_str = 0;
	if (!opt_strip) {
		n_sym = buf_str(&shstr, ".symtab");
		n_str = buf_str(&shstr, ".strtab");
	}
	n_shs = buf_str(&shstr, ".shstrtab");
	shstroff = pos;
	pos = shstroff + shstr.len;
	shoff = align_up(pos, 8);
	total = shoff + (u64)nsh * (u64)shsz;
	img = xalloc((size_t)total);

	put_ehdr(img, shoff, nsh, shstridx);
	p = img + ehsz;
	for (i = 0; i < 3; i++) {
		if (!segs[i].used)
			continue;
		put_phdr(p, PT_LOAD, segs[i].flags, segs[i].off, segs[i].addr,
			 segs[i].filesz, segs[i].memsz, pagesize);
		p += phsz;
	}
	if (have_tls) {
		put_phdr(p, PT_TLS, PF_R, tls_off, tls_addr, tls_filesz, tls_memsz, tls_align);
		p += phsz;
	}
	put_phdr(p, PT_GNU_STACK, opt_execstack ? (PF_R | PF_W | PF_X) : (PF_R | PF_W),
		 0, 0, 0, 0, 16);

	for (i = 0; i < O_COUNT + ndbgs; i++) {
		o = sec_at(i);
		if (o->size && o->buf)
			memcpy(img + o->off, o->buf, (size_t)o->size);
	}
	if (!opt_strip) {
		memcpy(img + symoff, sym.p, sym.len);
		memcpy(img + stroff, str.p, str.len);
	}
	memcpy(img + shstroff, shstr.p, shstr.len);

	p = img + shoff + shsz;
	for (i = 0; i < O_COUNT + ndbgs; i++) {
		o = sec_at(i);
		if (!o->size)
			continue;
		put_shdr(p, o->nameoff, o->type, o->flags, o->addr, o->off, o->size,
			 0, 0, o->align, 0);
		p += shsz;
	}
	if (!opt_strip) {
		put_shdr(p, n_sym, SHT_SYMTAB, 0, 0, symoff, sym.len, (u32)stridx,
			 (u32)nloc, (u64)(cls == ELFCLASS32 ? 4 : 8), (u64)symsz);
		p += shsz;
		put_shdr(p, n_str, SHT_STRTAB, 0, 0, stroff, str.len, 0, 0, 1, 0);
		p += shsz;
	}
	put_shdr(p, n_shs, SHT_STRTAB, 0, 0, shstroff, shstr.len, 0, 0, 1, 0);
	finish_file(path, img, total);
}
