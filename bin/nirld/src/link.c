#include "ld.h"

static struct archive **grp;
static int ngrp;
static int capgrp;

static char *find_lib(const char *name)
{
	char *p;
	FILE *f;
	int i;

	for (i = 0; i < nlibdirs; i++) {
		p = xalloc(strlen(libdirs[i]) + strlen(name) + 8);
		sprintf(p, "%s/lib%s.a", libdirs[i], name);
		f = fopen(p, "rb");
		if (f) {
			fclose(f);
			return p;
		}
	}
	die("cannot find -l%s", name);
	return NULL;
}

static void link_path(const char *path, int ingroup)
{
	unsigned char *buf;
	size_t len;
	struct archive *a;

	buf = read_file(path, &len);
	if (!buf)
		die("cannot open %s", path);
	if (len >= 8 && memcmp(buf, "!<arch>\n", 8) == 0) {
		a = archive_open(path, buf, len);
		if (ingroup) {
			if (ngrp == capgrp) {
				capgrp = capgrp ? capgrp * 2 : 8;
				grp = xrealloc(grp, sizeof(*grp) * (size_t)capgrp);
			}
			grp[ngrp++] = a;
		} else {
			while (ar_scan(a))
				;
		}
		return;
	}
	obj_load(obj_parse(path, buf, len));
}

static void close_group(void)
{
	int i;
	int n;

	do {
		n = 0;
		for (i = 0; i < ngrp; i++)
			n += ar_scan(grp[i]);
	} while (n);
	ngrp = 0;
}

void link_inputs(void)
{
	int ingroup;
	int i;

	ingroup = 0;
	for (i = 0; i < nitems; i++) {
		switch (items[i].kind) {
		case IT_FILE:
			link_path(items[i].arg, ingroup);
			break;
		case IT_LIB:
			link_path(find_lib(items[i].arg), ingroup);
			break;
		case IT_GROUP_START:
			ingroup = 1;
			break;
		case IT_GROUP_END:
			ingroup = 0;
			close_group();
			break;
		}
	}
	if (ingroup)
		close_group();
	if (ninputs == 0)
		die("no input objects were linked");
}
