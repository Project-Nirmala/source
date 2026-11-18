#include "ld.h"

#define HASHSZ 8192
#define COMDATSZ 1024

static struct gsym *htab[HASHSZ];
static struct comdat *comdats[COMDATSZ];
static long capgsyms;
struct gsym **gsyms;
long ngsyms;

static unsigned hash_str(const char *s)
{
	unsigned h;

	h = 5381;
	while (*s)
		h = h * 33 + (unsigned char)*s++;
	return h;
}

struct gsym *gsym_find(const char *name)
{
	struct gsym *g;

	for (g = htab[hash_str(name) % HASHSZ]; g; g = g->next)
		if (strcmp(g->name, name) == 0)
			return g;
	return NULL;
}

struct gsym *gsym_get(const char *name)
{
	struct gsym *g;
	unsigned h;

	g = gsym_find(name);
	if (g)
		return g;
	g = xalloc(sizeof(*g));
	g->name = xstrdup(name);
	g->got = -1;
	g->gottp = -1;
	h = hash_str(name) % HASHSZ;
	g->next = htab[h];
	htab[h] = g;
	if (ngsyms == capgsyms) {
		capgsyms = capgsyms ? capgsyms * 2 : 256;
		gsyms = xrealloc(gsyms, sizeof(*gsyms) * (size_t)capgsyms);
	}
	gsyms[ngsyms++] = g;
	return g;
}

struct comdat *comdat_seen(const char *sig, struct input *in, int grp)
{
	struct comdat *e;
	unsigned h;

	h = hash_str(sig) % COMDATSZ;
	for (e = comdats[h]; e; e = e->next)
		if (strcmp(e->key, sig) == 0)
			return e;
	e = xalloc(sizeof(*e));
	e->key = xstrdup(sig);
	e->obj = in;
	e->grp = grp;
	e->next = comdats[h];
	comdats[h] = e;
	return NULL;
}

static void define(struct gsym *g, struct input *in, const struct isym *s)
{
	g->state = G_DEF;
	g->obj = in;
	g->shndx = s->shndx;
	g->value = s->value;
	g->size = s->size;
	g->bind = s->bind;
	g->type = s->type;
}

void sym_resolve(struct input *in, struct isym *s)
{
	struct gsym *g;

	if (s->name[0] == 0)
		return;
	g = gsym_get(s->name);
	s->g = g;
	if (s->shndx == SHN_UNDEF) {
		if (s->bind != STB_WEAK && !g->pull) {
			g->pull = 1;
			g->ref = in;
		}
		return;
	}
	if (s->shndx == SHN_COMMON) {
		if (g->state == G_UNDEF) {
			g->state = G_COMMON;
			g->size = s->size;
			g->align = s->value ? s->value : 1;
			g->bind = s->bind;
			g->type = STT_OBJECT;
			g->obj = in;
		} else if (g->state == G_COMMON) {
			if (s->size > g->size)
				g->size = s->size;
			if (s->value > g->align)
				g->align = s->value;
		}
		return;
	}
	if (s->shndx < SHN_LORESERVE && !in->secs[s->shndx].live)
		return;
	if (s->shndx >= SHN_LORESERVE && s->shndx != SHN_ABS)
		die("%s: symbol '%s' has unsupported section index", in->name, s->name);
	if (g->state == G_DEF) {
		if (s->bind == STB_WEAK)
			return;
		if (g->bind == STB_WEAK) {
			define(g, in, s);
			return;
		}
		err("multiple definition of `%s'; first defined in %s, again in %s",
		    s->name, g->obj->name, in->name);
		return;
	}
	define(g, in, s);
}
