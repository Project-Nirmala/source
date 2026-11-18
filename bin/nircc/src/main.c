#include "cc.h"

int main(int argc, char **argv)
{
	const char *in;
	const char *outpath;
	struct token *toks;
	struct node *unit;
	char *src;
	FILE *out;
	int bits;
	int i;
	int only_pp;
	int obj;
	char *tmp;
	char *eq;
	char *pre;

	in = NULL;
	outpath = NULL;
	bits = host_bits();
	only_pp = 0;
	obj = 0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			outpath = argv[++i];
        else if (strcmp(argv[i], "-c") == 0)
            obj = 1;
        else if (strcmp(argv[i], "-E") == 0)
            only_pp = 1;
        else if (strncmp(argv[i], "-D", 2) == 0) {
            eq = strchr(argv[i] + 2, '=');
            if (eq) {
                *eq = '\0';
                pp_define(argv[i] + 2, eq + 1);
            } else {
                pp_define(argv[i] + 2, "1");
            }
        }
		else if (strcmp(argv[i], "-O0") == 0)
			gen_set_opt(0);
		else if (strcmp(argv[i], "-O1") == 0 || strcmp(argv[i], "-O") == 0)
			gen_set_opt(1);
		else if (strcmp(argv[i], "-O2") == 0 || strcmp(argv[i], "-O3") == 0)
			gen_set_opt(2);
		else if (strncmp(argv[i], "-I", 2) == 0) {
			if (argv[i][2])
				pp_add_include(argv[i] + 2);
			else if (i + 1 < argc)
				pp_add_include(argv[++i]);
		}
		else if (strcmp(argv[i], "-m32") == 0)
			bits = 32;
		else if (strcmp(argv[i], "-m64") == 0)
			bits = 64;
		else
			in = argv[i];
	}
	if (!in) {
		fprintf(stderr, "usage: nircc [-m32|-m64] [-o out.s] file.c\n");
		return 1;
	}
	target_set(bits);
	types_init();
	cur_file = in;
	src = read_file(in);
	pre = preprocess(src, in);
    if (only_pp) {
        out = outpath ? fopen(outpath, "w") : stdout;
        if (!out)
            die("cannot write '%s'", outpath);
        fputs(pre, out);
        if (out != stdout)
            fclose(out);
        return 0;
    }
    toks = lex_all(pre);
	unit = parse_unit(toks);
	if (obj) {
		if (bits != 64)
			die("-c supports only 64-bit targets");
		if (!outpath)
			die("-c needs -o");
		tmp = malloc(strlen(outpath) + 8);
		if (!tmp)
			die("out of memory");
		sprintf(tmp, "%s.tmp.s", outpath);
		out = fopen(tmp, "w");
		if (!out)
			die("cannot write '%s'", tmp);
		gen_unit(unit, out);
		fclose(out);
		pre = read_file(tmp);
		remove(tmp);
		enc_run(pre, outpath);
		free(src);
		return 0;
	}
	out = stdout;
	if (outpath) {
		out = fopen(outpath, "w");
		if (!out)
			die("cannot write '%s'", outpath);
	}
	gen_unit(unit, out);
	if (out != stdout)
		fclose(out);
	free(src);
	return 0;
}
