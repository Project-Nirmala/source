#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>

#define MAXTOK  256
#define MAXCMD  8
#define MAXARGS 128
#define POOLSZ  8192
#define MAXENV  32
#define SIGINT  2

enum { T_WORD, T_PIPE, T_SEMI, T_AND, T_OR, T_IN, T_OUT, T_APPEND };

struct tok {
    int   type;
    char *s;
    int   glob;
};

struct cmd {
    char *argv[MAXARGS + 1];
    int   argc;
    char *assign[8];
    int   nassign;
    char *infile;
    char *outfile;
    int   append;
};

static struct tok toks[MAXTOK];
static int        ntok;
static char       pool[POOLSZ];
static size_t     plen;
static int        opt_e, interactive;

static char  *env[MAXENV + 1];
static int    nenv;
static char   envpool[1024];
static size_t envlen;
static int    last_status;

static void out(int fd, const char *s)
{
    write(fd, s, strlen(s));
}

static void err2(const char *a, const char *b)
{
    out(2, "sh: ");
    out(2, a);
    if (b) {
        out(2, ": ");
        out(2, b);
    }
    out(2, "\n");
}

static const char *errstr(int e)
{
    switch (e) {
    case ENOENT:  return "No such file or directory";
    case ENOTDIR: return "Not a directory";
    case EACCES:  return "Permission denied";
    case EISDIR:  return "Is a directory";
    default:      return "Error";
    }
}

static void utoa(unsigned v, char *buf)
{
    char t[12];
    int n = 0, i = 0;

    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        buf[i++] = t[--n];
    buf[i] = 0;
}

static const char *sh_getenv(const char *name)
{
    size_t n = strlen(name);
    int i;

    for (i = 0; i < nenv; i++)
        if (!strncmp(env[i], name, n) && env[i][n] == '=')
            return env[i] + n + 1;
    return 0;
}

static int sh_setenv(const char *name, const char *val)
{
    size_t nl = strlen(name), vl = strlen(val);
    char *s;
    int i;

    if (envlen + nl + vl + 2 > sizeof(envpool))
        return -1;
    s = envpool + envlen;
    memcpy(s, name, nl);
    s[nl] = '=';
    memcpy(s + nl + 1, val, vl + 1);
    envlen += nl + vl + 2;
    for (i = 0; i < nenv; i++)
        if (!strncmp(env[i], name, nl) && env[i][nl] == '=') {
            env[i] = s;
            return 0;
        }
    if (nenv >= MAXENV)
        return -1;
    env[nenv++] = s;
    env[nenv] = 0;
    return 0;
}

static int emit(char c)
{
    if (plen + 1 >= POOLSZ)
        return -1;
    pool[plen++] = c;
    return 0;
}

static int emit_str(const char *s)
{
    while (*s)
        if (emit(*s++) < 0)
            return -1;
    return 0;
}

static int is_name(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int expand(const char **pp)
{
    const char *p = *pp;
    char name[32], num[12];
    size_t n = 0;
    const char *v;

    if (*p == '?') {
        utoa((unsigned)last_status, num);
        *pp = p + 1;
        return emit_str(num);
    }
    while (is_name(*p) && n < sizeof(name) - 1)
        name[n++] = *p++;
    name[n] = 0;
    *pp = p;
    if (!n)
        return emit('$');
    v = sh_getenv(name);
    return v ? emit_str(v) : 0;
}

static int add_tok(int type, char *s)
{
    if (ntok >= MAXTOK) {
        err2("too many tokens", 0);
        return -1;
    }
    toks[ntok].type = type;
    toks[ntok].s = s;
    toks[ntok].glob = 0;
    ntok++;
    return 0;
}

static int lex(const char **pp, int *op)
{
    const char *p = *pp;

    ntok = 0;
    plen = 0;
    *op = -1;
    for (;;) {
        char *start;
        int inword = 0, globby = 0;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#')
            while (*p && *p != '\n')
                p++;
        if (*p == '\n') {
            *op = T_SEMI;
            *pp = p + 1;
            return 0;
        }
        if (!*p) {
            *pp = p;
            return 0;
        }
        if (*p == '|') {
            if (p[1] == '|') {
                *op = T_OR;
                *pp = p + 2;
                return 0;
            } else {
                p++;
                if (add_tok(T_PIPE, 0) < 0)
                    return -1;
            }
            continue;
        }
        if (*p == '&') {
            if (p[1] != '&') {
                err2("background jobs (&) are not supported", 0);
                return -1;
            }
            *op = T_AND;
            *pp = p + 2;
            return 0;
        }
        if (*p == ';') {
            *op = T_SEMI;
            *pp = p + 1;
            return 0;
        }
        if (*p == '<') {
            p++;
            if (add_tok(T_IN, 0) < 0)
                return -1;
            continue;
        }
        if (*p == '>') {
            int type = T_OUT;

            if (p[1] == '>') {
                type = T_APPEND;
                p++;
            }
            p++;
            if (add_tok(type, 0) < 0)
                return -1;
            continue;
        }

        start = pool + plen;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '|' &&
               *p != ';' && *p != '&' && *p != '<' && *p != '>') {
            inword = 1;
            if (*p == '\'') {
                p++;
                while (*p && *p != '\'')
                    if (emit(*p++) < 0)
                        goto full;
                if (!*p) {
                    err2("unterminated quote", 0);
                    return -1;
                }
                p++;
            } else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '$') {
                        p++;
                        if (expand(&p) < 0)
                            goto full;
                    } else if (*p == '\\' && (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                        if (emit(p[1]) < 0)
                            goto full;
                        p += 2;
                    } else if (emit(*p++) < 0) {
                        goto full;
                    }
                }
                if (!*p) {
                    err2("unterminated quote", 0);
                    return -1;
                }
                p++;
            } else if (*p == '\\' && p[1]) {
                if (emit(p[1]) < 0)
                    goto full;
                p += 2;
            } else if (*p == '$') {
                p++;
                if (expand(&p) < 0)
                    goto full;
            } else {
                if (*p == '*' || *p == '?' || *p == '[')
                    globby = 1;
                if (emit(*p++) < 0)
                    goto full;
            }
        }
        if (inword) {
            if (emit(0) < 0)
                goto full;
            if (add_tok(T_WORD, start) < 0)
                return -1;
            toks[ntok - 1].glob = globby;
        }
    }
full:
    err2("line too long", 0);
    return -1;
}

static int is_builtin(const char *n)
{
    return !strcmp(n, "cd") || !strcmp(n, "pwd") || !strcmp(n, "exit") ||
           !strcmp(n, "export") || !strcmp(n, "help");
}

static int builtin(char **argv, int argc)
{
    const char *n = argv[0];

    if (!strcmp(n, "cd")) {
        const char *d = argc > 1 ? argv[1] : sh_getenv("HOME");

        if (!d)
            d = "/";
        if (chdir(d) < 0) {
            out(2, "sh: cd: can't cd to ");
            out(2, d);
            out(2, ": ");
            out(2, errstr(errno));
            out(2, "\n");
            return 1;
        }
        return 0;
    }
    if (!strcmp(n, "pwd")) {
        char buf[256];

        if (!getcwd(buf, sizeof(buf)))
            return 1;
        out(1, buf);
        out(1, "\n");
        return 0;
    }
    if (!strcmp(n, "exit")) {
        int code = last_status;

        if (argc > 1) {
            const char *s = argv[1];

            code = 0;
            while (*s >= '0' && *s <= '9')
                code = code * 10 + (*s++ - '0');
        }
        _exit(code & 0xFF);
    }
    if (!strcmp(n, "export")) {
        int i;

        if (argc == 1) {
            for (i = 0; i < nenv; i++) {
                out(1, env[i]);
                out(1, "\n");
            }
            return 0;
        }
        for (i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');

            if (!eq) {
                err2("export: expected NAME=VALUE", argv[i]);
                return 1;
            }
            *eq = 0;
            if (sh_setenv(argv[i], eq + 1) < 0) {
                err2("export: environment full", 0);
                return 1;
            }
        }
        return 0;
    }
    out(1, "Builtins: cd [dir], pwd, export [NAME=VALUE], exit [n], help\n");
    out(1, "Commands: ls cat echo mkdir rmdir rm cp mv touch wc head tail sort uniq grep tee vi ...\n");
    out(1, "Syntax:   cmd1 | cmd2, cmd1 ; cmd2, cmd1 && cmd2, cmd1 || cmd2,\n");
    out(1, "          cmd < file, cmd > file, cmd >> file, 'quotes', \"quotes\", $VAR, $?\n");
    return 0;
}

static void exec_path(char **argv)
{
    char buf[256];
    const char *path = sh_getenv("PATH");
    int e = ENOENT;

    if (strchr(argv[0], '/')) {
        execve(argv[0], argv, env);
        e = errno;
    } else {
        if (!path)
            path = "/bin:/sbin";
        while (*path) {
            size_t dl = 0, cl = strlen(argv[0]);

            while (path[dl] && path[dl] != ':')
                dl++;
            if (dl + 1 + cl + 1 <= sizeof(buf)) {
                memcpy(buf, path, dl);
                buf[dl] = '/';
                memcpy(buf + dl + 1, argv[0], cl + 1);
                execve(buf, argv, env);
                if (errno != ENOENT)
                    e = errno;
            }
            path += dl;
            if (*path == ':')
                path++;
        }
    }
    if (e == ENOENT)
        err2(argv[0], "not found");
    else
        err2(argv[0], "cannot execute");
    _exit(127);
}

static void apply_assign(char *a);

static void child_run(struct cmd *c)
{
    int a;

    for (a = 0; a < c->nassign; a++)
        apply_assign(c->assign[a]);
    if (is_builtin(c->argv[0]))
        _exit(builtin(c->argv, c->argc) & 0xFF);
    exec_path(c->argv);
}

static int status_of(int st)
{
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WTERMSIG(st) != SIGINT) {
        char num[12];

        utoa((unsigned)WTERMSIG(st), num);
        out(2, "sh: process killed by signal ");
        out(2, num);
        out(2, "\n");
    }
    return 128 + WTERMSIG(st);
}

static void set_fg(int pid)
{
    if (!interactive)
        return;
    ioctl(0, TIOCSPGRP, &pid);
}

static char   gpool[8192];
static size_t glen;

struct globres {
    char *v[MAXARGS];
    int   n;
};

static int add_arg(struct cmd *c, char *s)
{
    if (c->argc >= MAXARGS) {
        err2("too many arguments", 0);
        return -1;
    }
    c->argv[c->argc++] = s;
    return 0;
}

static int has_wild(const char *s)
{
    return strchr(s, '*') || strchr(s, '?') || strchr(s, '[');
}

static void glob_keep(const char *path, struct globres *r)
{
    size_t n = strlen(path) + 1;

    if (r->n >= MAXARGS || glen + n > sizeof(gpool))
        return;
    memcpy(gpool + glen, path, n);
    r->v[r->n++] = gpool + glen;
    glen += n;
}

static void glob_walk(const char *base, char **comps, int ncomp, int k, struct globres *r)
{
    char path[512], buf[1024];
    const char *name;
    int fd;
    long len;

    if (k == ncomp) {
        glob_keep(base, r);
        return;
    }
    if (!has_wild(comps[k])) {
        struct stat st;

        if (snprintf(path, sizeof(path), base[0] ? (base[strlen(base) - 1] == '/' ? "%s%s" : "%s/%s") : "%s%s",
                     base, comps[k]) >= (int)sizeof(path))
            return;
        if (k == ncomp - 1 && stat(path, &st) < 0)
            return;
        glob_walk(path, comps, ncomp, k + 1, r);
        return;
    }
    fd = open(base[0] ? base : ".", O_RDONLY);
    if (fd < 0)
        return;
    while ((len = getdents64(fd, buf, sizeof(buf))) > 0) {
        long off = 0;

        while (off < len) {
            const struct dirent *d = (const struct dirent *)(buf + off);

            off += d->d_reclen;
            name = d->d_name;
            if (!strcmp(name, ".") || !strcmp(name, ".."))
                continue;
            if (name[0] == '.' && comps[k][0] != '.')
                continue;
            if (fnmatch(comps[k], name, 0) != 0)
                continue;
            if (snprintf(path, sizeof(path), base[0] ? (base[strlen(base) - 1] == '/' ? "%s%s" : "%s/%s") : "%s%s",
                         base, name) >= (int)sizeof(path))
                continue;
            glob_walk(path, comps, ncomp, k + 1, r);
        }
    }
    close(fd);
}

static int glob_expand(char *word, struct cmd *c)
{
    char pat[512], *comps[32];
    const char *base = "";
    struct globres r;
    int ncomp = 0, i, j;
    char *q;

    if (strlen(word) >= sizeof(pat))
        return add_arg(c, word);
    strcpy(pat, word);
    q = pat;
    if (*q == '/') {
        base = "/";
        while (*q == '/')
            q++;
    }
    while (*q && ncomp < 32) {
        comps[ncomp++] = q;
        while (*q && *q != '/')
            q++;
        if (*q) {
            *q++ = 0;
            while (*q == '/')
                q++;
        }
    }
    r.n = 0;
    glob_walk(base, comps, ncomp, 0, &r);
    for (i = 1; i < r.n; i++) {
        char *t = r.v[i];

        for (j = i - 1; j >= 0 && strcmp(r.v[j], t) > 0; j--)
            r.v[j + 1] = r.v[j];
        r.v[j + 1] = t;
    }
    if (r.n == 0)
        return add_arg(c, word);
    for (i = 0; i < r.n; i++)
        if (add_arg(c, r.v[i]) < 0)
            return -1;
    return 0;
}

static int is_assign(const char *w)
{
    const char *p = w;

    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_'))
        return 0;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
        p++;
    return *p == '=';
}

static void apply_assign(char *a)
{
    char *eq = strchr(a, '=');

    *eq = 0;
    sh_setenv(a, eq + 1);
    *eq = '=';
}

static int run_pipeline(int s, int e)
{
    struct cmd cmds[MAXCMD];
    pid_t pids[MAXCMD];
    int n = 0, i, prev = -1, st = 0, spawned = 0;

    memset(cmds, 0, sizeof(cmds));
    glen = 0;
    for (i = s; i < e; i++) {
        struct cmd *c = &cmds[n];

        if (toks[i].type == T_WORD) {
            if (toks[i].glob ? glob_expand(toks[i].s, c) < 0 : add_arg(c, toks[i].s) < 0)
                return 2;
        } else if (toks[i].type == T_IN) {
            if (i + 1 >= e || toks[i + 1].type != T_WORD) {
                err2("syntax error: expected file after <", 0);
                return 2;
            }
            c->infile = toks[++i].s;
        } else if (toks[i].type == T_OUT || toks[i].type == T_APPEND) {
            if (i + 1 >= e || toks[i + 1].type != T_WORD) {
                err2("syntax error: expected file after >", 0);
                return 2;
            }
            c->append = toks[i].type == T_APPEND;
            c->outfile = toks[++i].s;
        } else if (toks[i].type == T_PIPE) {
            if (!c->argc) {
                err2("syntax error near |", 0);
                return 2;
            }
            if (++n >= MAXCMD) {
                err2("pipeline too long", 0);
                return 2;
            }
        }
    }
    if (!cmds[n].argc) {
        if (n || cmds[n].infile || cmds[n].outfile)
            err2("syntax error", 0);
        return n || cmds[n].infile || cmds[n].outfile ? 2 : 0;
    }
    n++;

    for (i = 0; i < n; i++) {
        struct cmd *c = &cmds[i];
        int k = 0, m;

        while (k < c->argc && c->nassign < 8 && is_assign(c->argv[k]))
            c->assign[c->nassign++] = c->argv[k++];
        if (k == c->argc && c->nassign) {
            if (n == 1)
                for (m = 0; m < c->nassign; m++)
                    apply_assign(c->assign[m]);
            return 0;
        }
        for (m = 0; m < c->argc - k; m++)
            c->argv[m] = c->argv[m + k];
        c->argc -= k;
        c->argv[c->argc] = 0;
    }
    if (n == 1 && !cmds[0].infile && !cmds[0].outfile && is_builtin(cmds[0].argv[0]))
        return builtin(cmds[0].argv, cmds[0].argc);

    for (i = 0; i < n; i++) {
        int fds[2] = { -1, -1 };
        pid_t pid;

        if (i < n - 1 && pipe(fds) < 0) {
            err2("cannot create pipe", 0);
            break;
        }
        pid = fork();
        if (pid < 0) {
            err2("fork failed", 0);
            if (fds[0] >= 0) {
                close(fds[0]);
                close(fds[1]);
            }
            break;
        }
        if (pid == 0) {
            if (prev >= 0) {
                dup2(prev, 0);
                close(prev);
            }
            if (fds[1] >= 0) {
                dup2(fds[1], 1);
                close(fds[0]);
                close(fds[1]);
            }
            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);

                if (fd < 0) {
                    out(2, "sh: can't open '");
                    out(2, cmds[i].infile);
                    out(2, "': ");
                    out(2, errstr(errno));
                    out(2, "\n");
                    _exit(1);
                }
                dup2(fd, 0);
                close(fd);
            }
            if (cmds[i].outfile) {
                int fd = open(cmds[i].outfile, O_WRONLY | O_CREAT | (cmds[i].append ? O_APPEND : O_TRUNC), 0644);

                if (fd < 0) {
                    out(2, "sh: can't create ");
                    out(2, cmds[i].outfile);
                    out(2, ": ");
                    out(2, errstr(errno));
                    out(2, "\n");
                    _exit(1);
                }
                dup2(fd, 1);
                close(fd);
            }
            child_run(&cmds[i]);
        }
        pids[spawned++] = pid;
        if (prev >= 0)
            close(prev);
        if (fds[1] >= 0)
            close(fds[1]);
        prev = fds[0];
    }
    if (prev >= 0)
        close(prev);
    if (!spawned)
        return 1;

    set_fg(pids[spawned - 1]);
    for (i = 0; i < spawned; i++) {
        int w = 0;

        if (waitpid(pids[i], &w, 0) == pids[i] && i == spawned - 1)
            st = status_of(w);
    }
    set_fg(0);
    return st;
}

static void run_line(const char *line)
{
    const char *p = line;
    int st = last_status, prev = T_SEMI, op;

    for (;;) {
        int run;

        if (lex(&p, &op) < 0) {
            last_status = 2;
            return;
        }
        run = prev == T_SEMI || (prev == T_AND && st == 0) || (prev == T_OR && st != 0);
        if (!ntok && (prev == T_AND || prev == T_OR || op == T_AND || op == T_OR)) {
            err2("syntax error", 0);
            last_status = 2;
            return;
        }
        if (run && ntok) {
            st = run_pipeline(0, ntok);
            last_status = st;
            if (opt_e && st != 0 && op != T_AND && op != T_OR)
                _exit(st & 0xFF);
        }
        prev = op;
        if (op < 0)
            break;
    }
}

static int run_script(const char *path)
{
    static char text[65536];
    int fd = open(path, O_RDONLY);
    long n, len = 0;

    if (fd < 0) {
        out(2, "sh: can't open '");
        out(2, path);
        out(2, "': ");
        out(2, errstr(errno));
        out(2, "\n");
        return 127;
    }
    while (len < (long)sizeof(text) - 1 && (n = read(fd, text + len, sizeof(text) - 1 - (size_t)len)) > 0)
        len += n;
    close(fd);
    text[len] = 0;
    run_line(text);
    return last_status;
}

int main(int argc, char **argv, char **envp)
{
    char line[256];
    int i = 1, cmdmode = 0;

    for (; envp && *envp && nenv < MAXENV; envp++) {
        char *eq = strchr(*envp, '=');

        if (eq) {
            *eq = 0;
            sh_setenv(*envp, eq + 1);
            *eq = '=';
        }
    }
    if (!sh_getenv("PATH"))
        sh_setenv("PATH", "/bin:/sbin");

    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *f;

        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (f = argv[i] + 1; *f; f++) {
            if (*f == 'c')
                cmdmode = 1;
            else if (*f == 'e')
                opt_e = 1;
        }
    }
    if (cmdmode) {
        if (i >= argc) {
            err2("-c: option requires an argument", 0);
            return 2;
        }
        run_line(argv[i]);
        return last_status;
    }
    if (i < argc)
        return run_script(argv[i]);

    interactive = 1;
    set_fg(0);
    for (;;) {
        char cwd[128];
        long n;

        if (getcwd(cwd, sizeof(cwd)))
            out(1, cwd);
        out(1, " # ");
        n = read(0, line, sizeof(line) - 1);
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        line[n] = 0;
        run_line(line);
    }
    out(1, "\n");
    return last_status;
}
