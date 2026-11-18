#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include "nirmake.h"

extern char **environ;

Opts opt;
StrList goals;

static void usage(FILE *out)
{
    fprintf(out,
            "usage: %s [-BeiknqrSst] [-C directory] [-D variable] [-f makefile]\n"
            "       [-I directory] [-j max_jobs] [-V variable] [variable=value] [target ...]\n",
            nm_progname);
}

static int takes_arg(int c)
{
    return strchr("fCjIVDdm", c) != NULL;
}

static void add_cmdvar(const char *assign)
{
    const char *eq = strchr(assign, '=');
    char *name;
    if (eq == NULL || eq == assign)
        return;
    name = nm_strndup(assign, (size_t)(eq - assign));
    var_set(name, eq + 1, ORG_CMD);
    setenv(name, eq + 1, 1);
    free(name);
    if (!sl_has(&opt.cmdvars, assign))
        sl_add(&opt.cmdvars, assign);
}

static int apply_opt(int c, const char *arg)
{
    switch (c) {
    case 'n':
        opt.no_exec = 1;
        break;
    case 's':
        opt.silent = 1;
        break;
    case 'k':
        opt.keep_going = 1;
        break;
    case 'S':
        opt.keep_going = 0;
        break;
    case 'i':
        opt.ignore_err = 1;
        break;
    case 'e':
        opt.env_override = 1;
        break;
    case 'q':
        opt.question = 1;
        break;
    case 't':
        opt.touch = 1;
        break;
    case 'r':
        opt.no_builtin = 1;
        break;
    case 'B':
        opt.always_make = 1;
        break;
    case 'j':
        opt.jobs = atoi(arg);
        if (opt.jobs < 1)
            opt.jobs = 1;
        if (opt.jobs > 200)
            opt.jobs = 200;
        break;
    case 'f':
        sl_add(&opt.files, arg);
        break;
    case 'C':
        if (chdir(arg) != 0)
            nm_fatal("cannot change directory to %s", arg);
        break;
    case 'I':
        sl_add(&opt.incdirs, arg);
        break;
    case 'V':
        sl_add(&opt.queryvars, arg);
        break;
    case 'D': {
        Buf b;
        buf_init(&b);
        buf_adds(&b, arg);
        buf_adds(&b, "=1");
        add_cmdvar(b.p);
        buf_free(&b);
        break;
    }
    case 'd':
    case 'm':
    case 'w':
    case 'p':
    case 'b':
    case 'a':
    case 'u':
    case 'x':
    case 'X':
        break;
    case 'h':
        usage(stdout);
        exit(0);
    default:
        return -1;
    }
    return 0;
}

static void parse_args(int argc, char **argv, int start, int from_env)
{
    int i;
    int only_vars = 0;
    for (i = start; i < argc; i++) {
        const char *a = argv[i];
        if (!only_vars && strcmp(a, "--") == 0) {
            only_vars = 1;
            continue;
        }
        if (!only_vars && a[0] == '-' && a[1] != '\0') {
            const char *c;
            for (c = a + 1; *c; c++) {
                if (takes_arg(*c)) {
                    const char *arg;
                    if (c[1] != '\0') {
                        arg = c + 1;
                    } else if (i + 1 < argc) {
                        arg = argv[++i];
                    } else {
                        fprintf(stderr, "%s: option requires an argument -- %c\n", nm_progname, *c);
                        usage(stderr);
                        exit(2);
                    }
                    apply_opt(*c, arg);
                    break;
                }
                if (apply_opt(*c, NULL) < 0) {
                    fprintf(stderr, "%s: unknown option -- %c\n", nm_progname, *c);
                    usage(stderr);
                    exit(2);
                }
            }
            continue;
        }
        if (strchr(a, '=') != NULL)
            add_cmdvar(a);
        else if (!from_env)
            sl_add(&goals, a);
    }
}

static char *unescape(const char *s)
{
    Buf b;
    buf_init(&b);
    for (; *s; s++) {
        if (*s == '\\' && s[1] != '\0')
            s++;
        buf_addc(&b, *s);
    }
    return buf_take(&b);
}

int opt_apply(const char *flags)
{
    StrList w;
    char **av;
    int i, n;
    sl_init(&w);
    split_words(flags, &w);
    if (w.n == 0) {
        sl_free(&w);
        return 0;
    }
    av = (char **)nm_malloc((size_t)(w.n + 1) * sizeof(char *));
    for (i = 0; i < w.n; i++) {
        char *u = unescape(w.v[i]);
        if (i == 0 && u[0] != '-' && strchr(u, '=') == NULL) {
            Buf b;
            buf_init(&b);
            buf_addc(&b, '-');
            buf_adds(&b, u);
            free(u);
            u = buf_take(&b);
        }
        av[i] = u;
    }
    n = w.n;
    parse_args(n, av, 0, 1);
    for (i = 0; i < n; i++)
        free(av[i]);
    free(av);
    sl_free(&w);
    return 0;
}

void opt_setup_makeflags(void)
{
    Buf b;
    int i;
    buf_init(&b);
    if (opt.no_exec)
        buf_adds(&b, " -n");
    if (opt.silent)
        buf_adds(&b, " -s");
    if (opt.keep_going)
        buf_adds(&b, " -k");
    if (opt.ignore_err)
        buf_adds(&b, " -i");
    if (opt.env_override)
        buf_adds(&b, " -e");
    if (opt.no_builtin)
        buf_adds(&b, " -r");
    if (opt.always_make)
        buf_adds(&b, " -B");
    if (opt.jobs > 1) {
        char tmp[32];
        sprintf(tmp, " -j%d", opt.jobs);
        buf_adds(&b, tmp);
    }
    for (i = 0; i < opt.incdirs.n; i++) {
        const char *s;
        buf_adds(&b, " -I");
        for (s = opt.incdirs.v[i]; *s; s++) {
            if (*s == ' ' || *s == '\\')
                buf_addc(&b, '\\');
            buf_addc(&b, *s);
        }
    }
    if (opt.cmdvars.n > 0) {
        buf_adds(&b, " --");
        for (i = 0; i < opt.cmdvars.n; i++) {
            const char *s;
            buf_addc(&b, ' ');
            for (s = opt.cmdvars.v[i]; *s; s++) {
                if (*s == ' ' || *s == '\\')
                    buf_addc(&b, '\\');
                buf_addc(&b, *s);
            }
        }
    }
    setenv("MAKEFLAGS", b.p, 1);
    var_setx("MAKEFLAGS", b.p, ORG_BUILTIN);
    buf_free(&b);
}

static void setup_vars(const char *argv0)
{
    char cwd[4096];
    struct utsname u;
    Buf mk;

    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, ".");
    var_setx(".CURDIR", cwd, ORG_FILE);
    var_setx(".OBJDIR", cwd, ORG_FILE);
    buf_init(&mk);
    if (argv0[0] != '/' && strchr(argv0, '/') != NULL) {
        buf_adds(&mk, cwd);
        buf_addc(&mk, '/');
    }
    buf_adds(&mk, argv0);
    var_setx("MAKE", mk.p, ORG_FILE);
    var_setx(".MAKE", mk.p, ORG_FILE);
    buf_free(&mk);
    var_setx(".MAKE.VERSION", NM_VERSION, ORG_FILE);
    if (uname(&u) == 0) {
        var_setx("MACHINE", u.machine, ORG_BUILTIN);
        var_setx("MACHINE_ARCH", u.machine, ORG_BUILTIN);
        var_setx(".MAKE.OS", u.sysname, ORG_BUILTIN);
    }
}

static char *read_stdin(void)
{
    Buf b;
    char chunk[4096];
    size_t n;
    buf_init(&b);
    while ((n = fread(chunk, 1, sizeof(chunk), stdin)) > 0)
        buf_addn(&b, chunk, n);
    return buf_take(&b);
}

static void load_makefiles(void)
{
    static const char *dflt[] = {"makefile", "Makefile", NULL};
    int i;
    if (opt.files.n > 0) {
        for (i = 0; i < opt.files.n; i++) {
            if (strcmp(opt.files.v[i], "-") == 0) {
                char *t = read_stdin();
                parse_string(t, "(stdin)", 0);
                free(t);
            } else {
                parse_file(opt.files.v[i], 1);
            }
        }
        return;
    }
    for (i = 0; dflt[i] != NULL; i++) {
        if (file_exists(dflt[i])) {
            parse_file(dflt[i], 1);
            return;
        }
    }
}

int main(int argc, char **argv)
{
    const char *env;
    int i;

    memset(&opt, 0, sizeof(opt));
    opt.jobs = 1;
    sl_init(&opt.files);
    sl_init(&opt.incdirs);
    sl_init(&opt.cmdvars);
    sl_init(&opt.queryvars);
    sl_init(&goals);
    sl_init(&suffixes);

    var_init(environ);
    env = getenv("MAKEFLAGS");
    if (env != NULL)
        opt_apply(env);
    parse_args(argc, argv, 1, 0);
    setup_vars(argv[0]);
    builtin_load();
    opt_setup_makeflags();
    load_makefiles();
    parse_finish();
    if (nm_errors > 0)
        return 2;
    opt_setup_makeflags();

    if (opt.queryvars.n > 0) {
        for (i = 0; i < opt.queryvars.n; i++) {
            char *v = var_get(opt.queryvars.v[i]);
            printf("%s\n", v ? v : "");
            free(v);
        }
        return 0;
    }
    return make_run();
}
