/* See LICENSE file for copyright and license details. */
#include <sys/stat.h>
#include <sys/types.h>
#ifndef major
#include <sys/sysmacros.h>
#endif

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "utf.h"
#include "util.h"

struct entry {
	char   *name;
	mode_t  mode, tmode;
	nlink_t nlink;
	uid_t   uid;
	gid_t   gid;
	off_t   size;
	struct timespec t;
	dev_t   dev;
	dev_t   rdev;
	ino_t   ino, tino;
	long    blocks;
};

static struct {
	dev_t dev;
	ino_t ino;
} tree[PATH_MAX];

static int ret   = 0;
static int Aflag = 0;
static int aflag = 0;
static int cflag = 0;
static int dflag = 0;
static int Fflag = 0;
static int fflag = 0;
static int Hflag = 0;
static int hflag = 0;
static int iflag = 0;
static int Lflag = 0;
static int lflag = 0;
static int nflag = 0;
static int pflag = 0;
static int qflag = 0;
static int Rflag = 0;
static int rflag = 0;
static int Uflag = 0;
static int uflag = 0;
static int Cflag = 0;
static int oneflag = 0;
static int wn = 1, wu = 1, wg = 1, wsz = 1;
static int first = 1;
static char sort = 0;
static int showdirs;

static void ls(const char *, const struct entry *, int);

static void
mkent(struct entry *ent, char *path, int dostat, int follow)
{
	struct stat st;

	ent->name = path;
	if (!dostat)
		return;
	if ((follow ? stat : lstat)(path, &st) < 0)
		eprintf("cannot access '%s':", path);
	ent->mode  = st.st_mode;
	ent->nlink = st.st_nlink;
	ent->uid   = st.st_uid;
	ent->gid   = st.st_gid;
	ent->size  = st.st_size;
	if (cflag)
		ent->t = st.st_ctim;
	else if (uflag)
		ent->t = st.st_atim;
	else
		ent->t = st.st_mtim;
	ent->dev   = st.st_dev;
	ent->rdev  = st.st_rdev;
	ent->ino   = st.st_ino;
	ent->blocks = st.st_blocks;
	if (S_ISLNK(ent->mode)) {
		if (stat(path, &st) == 0) {
			ent->tmode = st.st_mode;
			ent->dev   = st.st_dev;
			ent->tino  = st.st_ino;
		} else {
			ent->tmode = ent->tino = 0;
		}
	}
}

static char *
indicator(mode_t mode)
{
	if (pflag || Fflag)
		if (S_ISDIR(mode))
			return "/";

	if (Fflag) {
		if (S_ISLNK(mode))
			return "@";
		else if (S_ISFIFO(mode))
			return "|";
		else if (S_ISSOCK(mode))
			return "=";
		else if (mode & S_IXUSR || mode & S_IXGRP || mode & S_IXOTH)
			return "*";
	}

	return "";
}

static void
printname(const char *name)
{
	const char *c;
	Rune r;
	size_t l;

	for (c = name; *c; c += l) {
		l = chartorune(&r, c);
		if (!qflag || isprintrune(r))
			fwrite(c, 1, l, stdout);
		else
			putchar('?');
	}
}

static int
numw(unsigned long v)
{
	int n = 1;

	while (v >= 10) {
		v /= 10;
		n++;
	}
	return n;
}

static void
setw(const struct entry *ents, size_t n)
{
	struct group *gr;
	struct passwd *pw;
	char tmp[64];
	size_t i;
	int w;

	wn = wu = wg = wsz = 1;
	for (i = 0; i < n; i++) {
		if ((w = numw(ents[i].nlink)) > wn)
			wn = w;
		if (!nflag && (pw = getpwuid(ents[i].uid)))
			w = strlen(pw->pw_name);
		else
			w = numw(ents[i].uid);
		if (w > wu)
			wu = w;
		if (!nflag && (gr = getgrgid(ents[i].gid)))
			w = strlen(gr->gr_name);
		else
			w = numw(ents[i].gid);
		if (w > wg)
			wg = w;
		if (hflag)
			w = strlen(humansize(ents[i].size));
		else
			w = numw(ents[i].size);
		if (w > wsz)
			wsz = w;
	}
	(void)tmp;
}

static void
output(const struct entry *ent)
{
	struct group *gr;
	struct passwd *pw;
	struct tm *tm;
	ssize_t len;
	char *fmt, buf[BUFSIZ], pwname[256],
	     grname[256], mode[] = "----------";

	if (iflag)
		printf("%lu ", (unsigned long)ent->ino);
	if (!lflag) {
		printname(ent->name);
		puts(indicator(ent->mode));
		return;
	}
	if (S_ISREG(ent->mode))
		mode[0] = '-';
	else if (S_ISBLK(ent->mode))
		mode[0] = 'b';
	else if (S_ISCHR(ent->mode))
		mode[0] = 'c';
	else if (S_ISDIR(ent->mode))
		mode[0] = 'd';
	else if (S_ISFIFO(ent->mode))
		mode[0] = 'p';
	else if (S_ISLNK(ent->mode))
		mode[0] = 'l';
	else if (S_ISSOCK(ent->mode))
		mode[0] = 's';
	else
		mode[0] = '?';

	if (ent->mode & S_IRUSR) mode[1] = 'r';
	if (ent->mode & S_IWUSR) mode[2] = 'w';
	if (ent->mode & S_IXUSR) mode[3] = 'x';
	if (ent->mode & S_IRGRP) mode[4] = 'r';
	if (ent->mode & S_IWGRP) mode[5] = 'w';
	if (ent->mode & S_IXGRP) mode[6] = 'x';
	if (ent->mode & S_IROTH) mode[7] = 'r';
	if (ent->mode & S_IWOTH) mode[8] = 'w';
	if (ent->mode & S_IXOTH) mode[9] = 'x';

	if (ent->mode & S_ISUID) mode[3] = (mode[3] == 'x') ? 's' : 'S';
	if (ent->mode & S_ISGID) mode[6] = (mode[6] == 'x') ? 's' : 'S';
	if (ent->mode & S_ISVTX) mode[9] = (mode[9] == 'x') ? 't' : 'T';

	if (!nflag && (pw = getpwuid(ent->uid)))
		snprintf(pwname, sizeof(pwname), "%s", pw->pw_name);
	else
		snprintf(pwname, sizeof(pwname), "%d", ent->uid);

	if (!nflag && (gr = getgrgid(ent->gid)))
		snprintf(grname, sizeof(grname), "%s", gr->gr_name);
	else
		snprintf(grname, sizeof(grname), "%d", ent->gid);

	{
		time_t now = time(NULL);

		if (now > ent->t.tv_sec + 15778476 || now < ent->t.tv_sec)
			fmt = "%b %e  %Y";
		else
			fmt = "%b %e %H:%M";
	}

	if ((tm = localtime(&ent->t.tv_sec)))
		strftime(buf, sizeof(buf), fmt, tm);
	else
		snprintf(buf, sizeof(buf), "%lld", (long long)(ent->t.tv_sec));
	printf("%s %*ld %-*s %-*s ", mode, wn, (long)ent->nlink, wu, pwname, wg, grname);

	if (S_ISBLK(ent->mode) || S_ISCHR(ent->mode))
		printf("%4u, %4u ", major(ent->rdev), minor(ent->rdev));
	else if (hflag)
		printf("%*s ", wsz, humansize(ent->size));
	else
		printf("%*lu ", wsz, (unsigned long)ent->size);
	printf("%s ", buf);
	printname(ent->name);
	fputs(indicator(ent->mode), stdout);
	if (S_ISLNK(ent->mode)) {
		if ((len = readlink(ent->name, buf, sizeof(buf) - 1)) < 0)
			eprintf("readlink %s:", ent->name);
		buf[len] = '\0';
		printf(" -> %s%s", buf, indicator(ent->tmode));
	}
	putchar('\n');
}

static int
entcmp(const void *va, const void *vb)
{
	int cmp = 0;
	const struct entry *a = va, *b = vb;

	switch (sort) {
	case 'S':
		cmp = b->size - a->size;
		break;
	case 't':
		if (!(cmp = b->t.tv_sec - a->t.tv_sec))
			cmp = b->t.tv_nsec - a->t.tv_nsec;
		break;
	}

	if (!cmp)
		cmp = strcmp(a->name, b->name);

	return rflag ? 0 - cmp : cmp;
}

static int
termwidth(void)
{
	struct winsize ws;
	char *c;

	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		return ws.ws_col;
	if ((c = getenv("COLUMNS")) && atoi(c) > 0)
		return atoi(c);
	return 80;
}

static void
outcols(const struct entry *ents, size_t n)
{
	char (*lab)[PATH_MAX + 32];
	size_t *len, *cw, i, nc, rows, r, c, tot, k, width;

	lab = emalloc(n * sizeof(*lab));
	len = emalloc(n * sizeof(*len));
	for (i = 0; i < n; i++) {
		lab[i][0] = 0;
		if (iflag)
			snprintf(lab[i], sizeof(*lab), "%lu ", (unsigned long)ents[i].ino);
		strlcat(lab[i], ents[i].name, sizeof(*lab));
		strlcat(lab[i], indicator(ents[i].mode), sizeof(*lab));
		len[i] = strlen(lab[i]);
	}
	width = termwidth();
	cw = emalloc(n * sizeof(*cw));
	for (nc = n; nc > 1; nc--) {
		rows = (n + nc - 1) / nc;
		nc = (n + rows - 1) / rows;
		for (c = 0, tot = 0; c < nc; c++) {
			cw[c] = 0;
			for (r = 0; r < rows && (k = c * rows + r) < n; r++)
				if (len[k] > cw[c])
					cw[c] = len[k];
			tot += cw[c] + (c + 1 < nc ? 2 : 0);
		}
		if (tot <= width)
			break;
	}
	if (nc <= 1) {
		nc = 1;
		rows = n;
		cw[0] = 0;
	}
	for (r = 0; r < rows; r++) {
		for (c = 0; c < nc && (k = c * rows + r) < n; c++) {
			fputs(lab[k], stdout);
			if (c + 1 < nc && k + rows < n)
				for (i = len[k]; i < cw[c] + 2; i++)
					putchar(' ');
		}
		putchar('\n');
	}
	free(lab);
	free(len);
	free(cw);
}

static void
lsdir(const char *path, const struct entry *dir)
{
	DIR *dp;
	struct entry *ent, *ents = NULL;
	struct dirent *d;
	size_t i, n = 0;
	char prefix[PATH_MAX];

	if (!(dp = opendir(dir->name))) {
		ret = 1;
		weprintf("opendir %s%s:", path, dir->name);
		return;
	}
	if (chdir(dir->name) < 0)
		eprintf("chdir %s:", dir->name);

	while ((d = readdir(dp))) {
		if (d->d_name[0] == '.' && !aflag && !Aflag)
			continue;
		else if (Aflag)
			if (strcmp(d->d_name, ".") == 0 ||
			    strcmp(d->d_name, "..") == 0)
				continue;

		ents = ereallocarray(ents, ++n, sizeof(*ents));
		mkent(&ents[n - 1], estrdup(d->d_name), Fflag || iflag ||
		    lflag || pflag || Rflag || sort, Lflag);
	}

	closedir(dp);

	if (!Uflag)
		qsort(ents, n, sizeof(*ents), entcmp);

	if (path[0] || showdirs) {
		fputs(path, stdout);
		printname(dir->name);
		puts(":");
	}
	if (lflag) {
		long tb = 0;

		for (i = 0; i < n; i++)
			tb += ents[i].blocks;
		printf("total %ld\n", (tb + 1) / 2);
		setw(ents, n);
	}
	if (Cflag && n)
		outcols(ents, n);
	else
		for (i = 0; i < n; i++)
			output(&ents[i]);

	if (Rflag) {
		if (snprintf(prefix, PATH_MAX, "%s%s/", path, dir->name) >=
		    PATH_MAX)
			eprintf("path too long: %s%s\n", path, dir->name);

		for (i = 0; i < n; i++) {
			ent = &ents[i];
			if (strcmp(ent->name, ".") == 0 ||
			    strcmp(ent->name, "..") == 0)
				continue;
			if (S_ISLNK(ent->mode) && S_ISDIR(ent->tmode) && !Lflag)
				continue;

			ls(prefix, ent, 1);
		}
	}

	for (i = 0; i < n; ++i)
		free(ents[i].name);
	free(ents);
}

static int
visit(const struct entry *ent)
{
	dev_t dev;
	ino_t ino;
	int i;

	dev = ent->dev;
	ino = S_ISLNK(ent->mode) ? ent->tino : ent->ino;

	for (i = 0; i < PATH_MAX && tree[i].ino; ++i) {
		if (ino == tree[i].ino && dev == tree[i].dev)
			return -1;
	}

	tree[i].ino = ino;
	tree[i].dev = dev;

	return i;
}

static void
ls(const char *path, const struct entry *ent, int listdir)
{
	int treeind;
	char cwd[PATH_MAX];

	if (!listdir) {
		output(ent);
	} else if (S_ISDIR(ent->mode) ||
	    (S_ISLNK(ent->mode) && S_ISDIR(ent->tmode))) {
		if ((treeind = visit(ent)) < 0) {
			ret = 1;
			weprintf("%s%s: Already visited\n", path, ent->name);
			return;
		}

		if (!getcwd(cwd, PATH_MAX))
			eprintf("getcwd:");

		if (first)
			first = 0;
		else
			putchar('\n');

		lsdir(path, ent);
		tree[treeind].ino = 0;

		if (chdir(cwd) < 0)
			eprintf("chdir %s:", cwd);
	}
}

static void
usage(void)
{
	eprintf("usage: %s [-1ACacdFfHhiLlnpqRrtUu] [file ...]\n", argv0);
}

int
main(int argc, char *argv[])
{
	struct entry ent, *dents, *fents;
	size_t i, ds, fs;

	ARGBEGIN {
	case '1':
		oneflag = 1;
		Cflag = 0;
		break;
	case 'C':
		Cflag = 1;
		oneflag = 0;
		break;
	case 'A':
		Aflag = 1;
		break;
	case 'a':
		aflag = 1;
		break;
	case 'c':
		cflag = 1;
		uflag = 0;
		break;
	case 'd':
		dflag = 1;
		break;
	case 'f':
		aflag = 1;
		fflag = 1;
		Uflag = 1;
		break;
	case 'F':
		Fflag = 1;
		break;
	case 'H':
		Hflag = 1;
		break;
	case 'h':
		hflag = 1;
		break;
	case 'i':
		iflag = 1;
		break;
	case 'L':
		Lflag = 1;
		break;
	case 'l':
		lflag = 1;
		break;
	case 'n':
		lflag = 1;
		nflag = 1;
		break;
	case 'p':
		pflag = 1;
		break;
	case 'q':
		qflag = 1;
		break;
	case 'R':
		Rflag = 1;
		break;
	case 'r':
		rflag = 1;
		break;
	case 'S':
		sort = 'S';
		break;
	case 't':
		sort = 't';
		break;
	case 'U':
		Uflag = 1;
		break;
	case 'u':
		uflag = 1;
		cflag = 0;
		break;
	default:
		usage();
	} ARGEND

	if (!oneflag && !Cflag && isatty(1))
		Cflag = 1;
	if (lflag || nflag)
		Cflag = 0;

	switch (argc) {
	case 0: /* fallthrough */
		*--argv = ".", ++argc;
	case 1:
		mkent(&ent, argv[0], 1, Hflag || Lflag);
		if (lflag)
			setw(&ent, 1);
		ls("", &ent, (!dflag && S_ISDIR(ent.mode)) ||
		    (S_ISLNK(ent.mode) && S_ISDIR(ent.tmode) &&
		     !(dflag || Fflag || lflag)));

		break;
	default:
		for (i = ds = fs = 0, fents = dents = NULL; i < argc; ++i) {
			mkent(&ent, argv[i], 1, Hflag || Lflag);

			if ((!dflag && S_ISDIR(ent.mode)) ||
			    (S_ISLNK(ent.mode) && S_ISDIR(ent.tmode) &&
			     !(dflag || Fflag || lflag))) {
				dents = ereallocarray(dents, ++ds, sizeof(*dents));
				memcpy(&dents[ds - 1], &ent, sizeof(ent));
			} else {
				fents = ereallocarray(fents, ++fs, sizeof(*fents));
				memcpy(&fents[fs - 1], &ent, sizeof(ent));
			}
		}

		showdirs = ds > 1 || (ds && fs);

		qsort(fents, fs, sizeof(ent), entcmp);
		if (lflag)
			setw(fents, fs);
		qsort(dents, ds, sizeof(ent), entcmp);

		for (i = 0; i < fs; ++i)
			ls("", &fents[i], 0);
		free(fents);
		if (fs && ds)
			putchar('\n');
		for (i = 0; i < ds; ++i)
			ls("", &dents[i], 1);
		free(dents);
	}

	return (fshut(stdout, "<stdout>") | ret);
}
