#include "ld.h"

const struct machine *mach;
struct item *items;
int nitems;
char **libdirs;
int nlibdirs;
const char *opt_out = "a.out";
const char *opt_entry = "_start";
int opt_strip;
int have_ttext;
u64 opt_ttext;
u64 pagesize = 0x1000;
int opt_gc;
int opt_debug = 1;
int opt_binary;
int opt_noseparate;
int opt_execstack;
int opt_ignore_undef;
const char *opt_map;
char **defsyms;
int ndefsyms;

static int capitems;

static void usage(FILE *f)
{
	fputs("usage: nirld [options] file...\n"
	      "  -o FILE               output file (default a.out)\n"
	      "  -e SYMBOL|ADDR        entry point (default _start)\n"
	      "  -m EMULATION          elf_x86_64 or elf_i386\n"
	      "  -L DIR                add library search directory\n"
	      "  -l NAME               link against libNAME.a\n", f);
	fputs("  -Ttext ADDR           address of the first code byte\n"
	      "  -z max-page-size=N    page size used for segment layout\n"
	      "  -z noseparate-code    put .rodata in the code segment\n"
	      "  -z execstack          request an executable stack\n"
	      "  -s, --strip-all       omit symbol table and debug sections\n"
	      "  -S, --strip-debug     omit debug sections\n", f);
	fputs("  --gc-sections         remove unreferenced sections\n"
	      "  -u SYMBOL             treat SYMBOL as undefined (pulls archives)\n"
	      "  --defsym SYM=EXPR     define SYM as a number, symbol or symbol+N\n"
	      "  -Map FILE             write a link map\n"
	      "  --oformat binary      emit a flat binary image\n"
	      "  --unresolved-symbols=ignore-all\n", f);
	fputs("  --start-group, -(     begin an archive group\n"
	      "  --end-group, -)       end an archive group\n"
	      "  -v, --version         print version\n"
	      "  -h, --help            print this help\n", f);
}

static void add_item(int kind, const char *arg)
{
	if (nitems == capitems) {
		capitems = capitems ? capitems * 2 : 16;
		items = xrealloc(items, sizeof(*items) * (size_t)capitems);
	}
	items[nitems].kind = kind;
	items[nitems].arg = arg ? xstrdup(arg) : NULL;
	nitems++;
}

static const char *optval(int argc, char **argv, int *i, const char *name)
{
	const char *a;
	size_t n;

	a = argv[*i];
	n = strlen(name);
	if (strncmp(a, name, n) != 0)
		return NULL;
	if (a[n] == '=')
		return a + n + 1;
	if (a[n] != 0)
		return a + n;
	if (*i + 1 >= argc)
		die("option '%s' needs an argument", name);
	*i += 1;
	return argv[*i];
}

static u64 parse_num(const char *s, const char *what)
{
	char *end;
	u64 v;

	v = strtoul(s, &end, 0);
	if (*s == 0 || *end != 0)
		die("invalid %s '%s'", what, s);
	return v;
}

static void zopt(const char *s)
{
	if (strncmp(s, "max-page-size=", 14) == 0 || strncmp(s, "common-page-size=", 17) == 0) {
		pagesize = parse_num(strchr(s, '=') + 1, "page size");
		if (pagesize < 16 || (pagesize & (pagesize - 1)) != 0)
			die("page size must be a power of two");
	} else if (!strcmp(s, "noseparate-code")) {
		opt_noseparate = 1;
	} else if (!strcmp(s, "separate-code")) {
		opt_noseparate = 0;
	} else if (!strcmp(s, "execstack")) {
		opt_execstack = 1;
	} else if (!strcmp(s, "noexecstack")) {
		opt_execstack = 0;
	} else if (!strcmp(s, "undefs")) {
		opt_ignore_undef = 1;
	}
}

int main(int argc, char **argv)
{
	const char *a;
	const char *v;
	struct gsym *g;
	char *end;
	int i;

	for (i = 1; i < argc; i++) {
		a = argv[i];
		if (a[0] != '-' || a[1] == 0) {
			add_item(IT_FILE, a);
		} else if (!strcmp(a, "-s") || !strcmp(a, "--strip-all")) {
			opt_strip = 1;
			opt_debug = 0;
		} else if (!strcmp(a, "-S") || !strcmp(a, "--strip-debug")) {
			opt_debug = 0;
		} else if (!strcmp(a, "--gc-sections")) {
			opt_gc = 1;
		} else if (!strcmp(a, "--no-gc-sections")) {
			opt_gc = 0;
		} else if (!strcmp(a, "--unresolved-symbols=ignore-all")) {
			opt_ignore_undef = 1;
		} else if (!strncmp(a, "--build-id", 10) || !strncmp(a, "--hash-style", 12) ||
			   !strncmp(a, "-O", 2)) {
			continue;
		} else if (!strcmp(a, "-static") || !strcmp(a, "-Bstatic") ||
			   !strcmp(a, "-nostdlib") || !strcmp(a, "--no-dynamic-linker")) {
			continue;
		} else if (!strcmp(a, "--start-group") || !strcmp(a, "-(")) {
			add_item(IT_GROUP_START, NULL);
		} else if (!strcmp(a, "--end-group") || !strcmp(a, "-)")) {
			add_item(IT_GROUP_END, NULL);
		} else if (!strcmp(a, "-v") || !strcmp(a, "--version")) {
			puts("nirld 0.1 (Nirmala binutils)");
			return 0;
		} else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-z")) {
			if (i + 1 >= argc)
				die("option '-z' needs an argument");
			zopt(argv[++i]);
		} else if ((v = optval(argc, argv, &i, "-Map")) != NULL ||
			   (v = optval(argc, argv, &i, "--Map")) != NULL) {
			opt_map = v;
		} else if ((v = optval(argc, argv, &i, "--defsym")) != NULL) {
			defsyms = xrealloc(defsyms, sizeof(*defsyms) * (size_t)(ndefsyms + 1));
			defsyms[ndefsyms++] = xstrdup(v);
		} else if ((v = optval(argc, argv, &i, "--oformat")) != NULL) {
			opt_binary = strcmp(v, "binary") == 0;
		} else if ((v = optval(argc, argv, &i, "--undefined")) != NULL ||
			   (v = optval(argc, argv, &i, "-u")) != NULL) {
			gsym_get(v)->pull = 1;
		} else if ((v = optval(argc, argv, &i, "-Ttext")) != NULL) {
			have_ttext = 1;
			opt_ttext = parse_num(v, "address");
		} else if ((v = optval(argc, argv, &i, "--entry")) != NULL ||
			   (v = optval(argc, argv, &i, "-e")) != NULL) {
			opt_entry = v;
		} else if ((v = optval(argc, argv, &i, "-o")) != NULL) {
			opt_out = v;
		} else if ((v = optval(argc, argv, &i, "-m")) != NULL) {
			mach = machine_by_emul(v);
			if (!mach)
				die("unknown emulation '%s'", v);
		} else if ((v = optval(argc, argv, &i, "-L")) != NULL) {
			libdirs = xrealloc(libdirs, sizeof(*libdirs) * (size_t)(nlibdirs + 1));
			libdirs[nlibdirs++] = xstrdup(v);
		} else if ((v = optval(argc, argv, &i, "-l")) != NULL) {
			add_item(IT_LIB, v);
		} else {
			usage(stderr);
			die("unrecognized option '%s'", a);
		}
	}
	if (nitems == 0) {
		usage(stderr);
		return 1;
	}

	osec_init();
	strtoul(opt_entry, &end, 0);
	if (*opt_entry == 0 || *end != 0) {
		g = gsym_get(opt_entry);
		g->pull = 1;
	}
	link_inputs();
	if (opt_gc)
		gc_sections();
	place_sections();
	scan_relocs();
	alloc_commons();
	layout();
	define_linker_syms();
	finalize_syms();
	apply_defsyms();
	report_undefined();
	if (nerrors)
		return 1;

	strtoul(opt_entry, &end, 0);
	if (*opt_entry != 0 && *end == 0) {
		entry_addr = parse_num(opt_entry, "entry address");
	} else {
		g = gsym_find(opt_entry);
		if (g && g->state != G_UNDEF) {
			entry_addr = g->addr;
		} else {
			entry_addr = osecs[O_TEXT].addr;
			warn("cannot find entry symbol %s; defaulting to 0x%lx", opt_entry, entry_addr);
		}
	}

	if (opt_map)
		write_map(opt_map);
	build_sections();
	fill_got();
	apply_relocs();
	if (nerrors)
		return 1;
	write_output(opt_out);
	return 0;
}
