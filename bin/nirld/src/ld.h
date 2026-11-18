#ifndef LD_H
#define LD_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

#if ULONG_MAX <= 0xffffffffUL
#error nirld needs a host where unsigned long has at least 64 bits
#endif

#include "nelf.h"

typedef unsigned long u64;
typedef long s64;
typedef unsigned int u32;

enum { G_UNDEF, G_DEF, G_COMMON };

enum {
	O_INIT, O_TEXT, O_FINI, O_RODATA, O_TDATA, O_TBSS, O_PREINIT, O_INITARR,
	O_FINIARR, O_DATA, O_GOT, O_BSS, O_COUNT
};

enum {
	C_NONE, C_ABS, C_PCREL, C_GOTSLOTPC, C_GOTSLOT, C_GOTOFF, C_GOTPC,
	C_TPOFF, C_TPOFFPOS, C_TPSLOTPC, C_TPSLOT, C_TPSLOTABS
};
enum { R_WRAP, R_UNSIGNED, R_SIGNED };
enum { IT_FILE, IT_LIB, IT_GROUP_START, IT_GROUP_END };

struct buf {
	unsigned char *p;
	size_t len;
	size_t cap;
};

struct input;
struct gsym;
struct osec;

struct isec {
	struct input *obj;
	char *name;
	u32 nameoff;
	u32 type;
	u64 flags;
	u64 offset;
	u64 size;
	u64 align;
	u32 link;
	u32 info;
	int live;
	int mark;
	int gcdone;
	struct isec *alias;
	struct osec *out;
	u64 outoff;
};

struct comdat {
	char *key;
	struct input *obj;
	int grp;
	struct comdat *next;
};

struct isym {
	char *name;
	u32 shndx;
	u64 value;
	u64 size;
	int bind;
	int type;
	struct gsym *g;
	long got;
	long gottp;
};

struct input {
	char *name;
	const unsigned char *buf;
	size_t len;
	int cls;
	int machine;
	int nsec;
	struct isec *secs;
	int nsym;
	struct isym *syms;
	int symsec;
	int firstglobal;
	int loaded;
};

struct gsym {
	char *name;
	struct gsym *next;
	int state;
	int bind;
	int type;
	int pull;
	int linker;
	struct input *obj;
	struct input *ref;
	u32 shndx;
	u64 value;
	u64 size;
	u64 align;
	u64 commonoff;
	u64 addr;
	long got;
	long gottp;
	struct osec *osec;
};

struct osec {
	const char *name;
	u32 type;
	u64 flags;
	int group;
	u64 size;
	u64 align;
	u64 addr;
	u64 off;
	unsigned char *buf;
	struct isec **secs;
	long nsecs;
	long capsecs;
	int shidx;
	u64 nameoff;
};

struct seg {
	int used;
	u64 flags;
	u64 off;
	u64 addr;
	u64 fileend;
	u64 memend;
	u64 filesz;
	u64 memsz;
};

struct rtype {
	u32 type;
	int calc;
	int size;
	int chk;
};

struct machine {
	int em;
	int cls;
	const char *emul;
	u64 base;
	const struct rtype *rt;
	int nrt;
};

struct member {
	char *name;
	size_t off;
	size_t size;
	struct input *obj;
	int loaded;
};

struct archive {
	char *name;
	const unsigned char *buf;
	size_t len;
	int nmem;
	int capmem;
	struct member *mem;
};

struct item {
	int kind;
	char *arg;
};

extern const struct machine *mach;
extern struct osec osecs[O_COUNT];
extern struct seg segs[3];
extern struct input **inputs;
extern int ninputs;
extern struct gsym **gsyms;
extern long ngsyms;
extern struct item *items;
extern int nitems;
extern char **libdirs;
extern int nlibdirs;
extern const char *opt_out;
extern const char *opt_entry;
extern int opt_strip;
extern int have_ttext;
extern u64 opt_ttext;
extern u64 pagesize;
extern long ngot;
extern struct osec **dbgs;
extern int ndbgs;
extern int opt_gc;
extern int opt_debug;
extern int opt_binary;
extern int opt_noseparate;
extern int opt_execstack;
extern int opt_ignore_undef;
extern const char *opt_map;
extern char **defsyms;
extern int ndefsyms;
extern int have_tls;
extern u64 tls_addr;
extern u64 tls_off;
extern u64 tls_filesz;
extern u64 tls_memsz;
extern u64 tls_align;
extern int nphdr;
extern u64 image_base;
extern u64 file_end;
extern u64 etext_addr;
extern u64 edata_addr;
extern u64 bss_start_addr;
extern u64 end_addr;
extern u64 entry_addr;
extern int nerrors;

void die(const char *fmt, ...);
void err(const char *fmt, ...);
void warn(const char *fmt, ...);
void *xalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
unsigned char *read_file(const char *path, size_t *len);
u64 rd(const unsigned char *p, int n);
void wr(unsigned char *p, int n, u64 v);
u64 align_up(u64 v, u64 a);
s64 sext(u64 v, int bytes);
void buf_add(struct buf *b, const void *d, size_t n);
size_t buf_str(struct buf *b, const char *s);

struct input *obj_parse(const char *name, const unsigned char *buf, size_t len);
void obj_load(struct input *in);
struct archive *archive_open(const char *path, const unsigned char *buf, size_t len);
int ar_scan(struct archive *a);

struct gsym *gsym_find(const char *name);
struct gsym *gsym_get(const char *name);
struct comdat *comdat_seen(const char *sig, struct input *in, int grp);
void sym_resolve(struct input *in, struct isym *s);

void osec_init(void);
void place_sections(void);
void gc_sections(void);
void apply_defsyms(void);
void write_map(const char *path);
u64 tls_tpoff(u64 s);
void osec_add(struct input *in, struct isec *s);
void alloc_commons(void);
void layout(void);
void define_linker_syms(void);
void finalize_syms(void);
void report_undefined(void);
void build_sections(void);
int sym_value(const struct input *in, const struct isym *s, u64 *out);

const struct machine *machine_by_em(int em);
const struct machine *machine_by_emul(const char *emul);
void scan_relocs(void);
void fill_got(void);
void apply_relocs(void);

void link_inputs(void);
void write_output(const char *path);

#endif
