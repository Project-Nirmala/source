#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "nirmake.h"

#define F_SILENT 1
#define F_IGNORE 2
#define F_ALWAYS 4

typedef struct Job {
    Node *n;
    pid_t pid;
    char **cmds;
    int *flags;
    int ncmd;
    int idx;
    int ign;
    struct Job *next;
} Job;

static Node **list = NULL;
static int nlist = 0;
static int caplist = 0;
static Job *jobs = NULL;
static int running = 0;
static int aborting = 0;
static int any_failed = 0;
static int unknown_stop = 0;

static void advance(Job *j);

static void list_add(Node *n)
{
    if (nlist + 1 > caplist) {
        caplist = caplist ? caplist * 2 : 64;
        list = (Node **)nm_realloc(list, (size_t)caplist * sizeof(Node *));
    }
    list[nlist++] = n;
}

static void visit(Node *n)
{
    Rule *r;
    int i;
    if (n->state != S_NEW)
        return;
    n->state = S_VISIT;
    node_stat(n);
    if (!node_has_cmds(n) && !n->phony && (n->is_target || !n->exists))
        node_infer(n);
    for (r = n->rules; r != NULL; r = r->next) {
        for (i = 0; i < r->deps.n; i++) {
            Node *d = r->deps.v[i];
            if (d->state == S_VISIT) {
                int k;
                fflush(stdout);
                fprintf(stderr, "%s: Graph cycles through `%s'\n", nm_progname, d->name);
                n->failed = 1;
                any_failed = 1;
                for (k = i; k + 1 < r->deps.n; k++)
                    r->deps.v[k] = r->deps.v[k + 1];
                r->deps.n--;
                i--;
                continue;
            }
            visit(d);
        }
    }
    n->state = S_WAIT;
    list_add(n);
}

static int deps_done(Node *n)
{
    Rule *r;
    int i;
    for (r = n->rules; r != NULL; r = r->next)
        for (i = 0; i < r->deps.n; i++)
            if (r->deps.v[i]->state != S_DONE)
                return 0;
    return 1;
}

static int dep_newer(Node *n, Node *d)
{
    if (opt.always_make || d->phony || d->updated || !n->exists)
        return 1;
    node_stat(d);
    return nm_time_cmp(d->mtime, n->mtime) > 0;
}

static char *nlist_join(const NList *l)
{
    Buf b;
    int i;
    buf_init(&b);
    for (i = 0; i < l->n; i++) {
        if (i > 0)
            buf_addc(&b, ' ');
        buf_adds(&b, l->v[i]->name);
    }
    return buf_take(&b);
}

static void set_locals(Node *n, const NList *all, const NList *newer)
{
    char *s;
    char *pfx = NULL;
    const char *p;
    const char *imp;
    int i;

    var_setx("@", n->name, ORG_LOCAL);
    var_setx(".TARGET", n->name, ORG_LOCAL);
    p = n->prefix;
    if (p == NULL) {
        size_t nl = strlen(n->name);
        for (i = 0; i < suffixes.n; i++) {
            size_t sl = strlen(suffixes.v[i]);
            if (nl > sl && strcmp(n->name + nl - sl, suffixes.v[i]) == 0) {
                pfx = nm_strndup(n->name, nl - sl);
                break;
            }
        }
        p = pfx ? pfx : n->name;
    }
    var_setx("*", p, ORG_LOCAL);
    var_setx(".PREFIX", p, ORG_LOCAL);
    free(pfx);
    imp = n->impsrc ? n->impsrc : (all->n > 0 ? all->v[0]->name : "");
    var_setx("<", imp, ORG_LOCAL);
    var_setx(".IMPSRC", imp, ORG_LOCAL);
    s = nlist_join(newer);
    var_setx("?", s, ORG_LOCAL);
    var_setx(".OODATE", s, ORG_LOCAL);
    free(s);
    s = nlist_join(all);
    var_setx(">", s, ORG_LOCAL);
    var_setx(".ALLSRC", s, ORG_LOCAL);
    var_setx("^", s, ORG_LOCAL);
    free(s);
    var_setx("%", "", ORG_LOCAL);
    var_setx(".MEMBER", "", ORG_LOCAL);
}

static void add_cmds(Job *j, const StrList *raw)
{
    int i;
    for (i = 0; i < raw->n; i++) {
        const char *rc = raw->v[i];
        int fl = 0;
        char *e;
        const char *p;
        if (strstr(rc, "${MAKE}") || strstr(rc, "$(MAKE)") || strstr(rc, "${.MAKE}") || strstr(rc, "$(.MAKE)"))
            fl |= F_ALWAYS;
        e = var_expand(rc);
        p = e;
        for (;;) {
            if (*p == '@')
                fl |= F_SILENT;
            else if (*p == '-')
                fl |= F_IGNORE;
            else if (*p == '+')
                fl |= F_ALWAYS;
            else if (*p != ' ' && *p != '\t')
                break;
            p++;
        }
        if (*p == '\0') {
            free(e);
            continue;
        }
        j->cmds = (char **)nm_realloc(j->cmds, (size_t)(j->ncmd + 1) * sizeof(char *));
        j->flags = (int *)nm_realloc(j->flags, (size_t)(j->ncmd + 1) * sizeof(int));
        j->cmds[j->ncmd] = nm_strdup(p);
        j->flags[j->ncmd] = fl;
        j->ncmd++;
        free(e);
    }
}

static void fail_node(Node *n)
{
    n->failed = 1;
    n->state = S_DONE;
    any_failed = 1;
    if (!opt.keep_going)
        aborting = 1;
}

static void free_job(Job *j)
{
    int i;
    for (i = 0; i < j->ncmd; i++)
        free(j->cmds[i]);
    free(j->cmds);
    free(j->flags);
    free(j);
}

static void finish_job(Job *j, int ok)
{
    Node *n = j->n;
    if (ok) {
        n->updated = 1;
        n->state = S_DONE;
        if (!n->phony) {
            node_restat(n);
            if (!n->exists)
                nm_time_now(&n->mtime);
        }
    } else {
        fail_node(n);
    }
    free_job(j);
}

static void jobs_remove(Job *j)
{
    Job **pp;
    for (pp = &jobs; *pp != NULL; pp = &(*pp)->next) {
        if (*pp == j) {
            *pp = j->next;
            return;
        }
    }
}

static void advance(Job *j)
{
    Node *n = j->n;
    while (j->idx < j->ncmd) {
        const char *cmd = j->cmds[j->idx];
        int fl = j->flags[j->idx];
        int silent = (fl & F_SILENT) || n->silent || global_silent || opt.silent;
        int ign = (fl & F_IGNORE) || n->ignore || global_ignore || opt.ignore_err;
        pid_t pid;
        if (!silent || opt.no_exec) {
            printf("%s\n", cmd);
            fflush(stdout);
        }
        if (opt.no_exec && !(fl & F_ALWAYS)) {
            j->idx++;
            continue;
        }
        var_export_all();
        pid = job_spawn(cmd, ign);
        if (pid < 0) {
            fprintf(stderr, "%s: cannot fork: %s\n", nm_progname, strerror(errno));
            finish_job(j, 0);
            return;
        }
        j->pid = pid;
        j->ign = ign;
        j->next = jobs;
        jobs = j;
        running++;
        job_track(pid, (n->phony || n->precious || global_precious) ? NULL : n->name, n->exists, n->mtime);
        return;
    }
    finish_job(j, 1);
}

static void remove_partial(Node *n)
{
    struct stat st;
    if (n->phony || n->precious || global_precious)
        return;
    if (stat(n->name, &st) != 0 || !S_ISREG(st.st_mode))
        return;
    if (!n->exists || st.st_mtime != n->mtime.s) {
        printf("%s: *** `%s' removed\n", nm_progname, n->name);
        fflush(stdout);
        unlink(n->name);
    }
}

static void reap(void)
{
    int st = 0;
    pid_t pid = job_wait(&st);
    Job *j;
    int ok, code = 0, sig = 0;

    if (pid < 0) {
        nm_fatal("wait failed: %s", strerror(errno));
    }
    for (j = jobs; j != NULL; j = j->next)
        if (j->pid == pid)
            break;
    if (j == NULL)
        return;
    jobs_remove(j);
    job_untrack(pid);
    running--;
    if (WIFEXITED(st)) {
        code = WEXITSTATUS(st);
        ok = (code == 0);
    } else {
        sig = WIFSIGNALED(st) ? WTERMSIG(st) : 1;
        ok = 0;
    }
    if (ok) {
        j->idx++;
        advance(j);
        return;
    }
    if (sig)
        printf("*** Signal %d%s\n", sig, j->ign ? " (ignored)" : (opt.keep_going ? " (continuing)" : ""));
    else
        printf("*** Error code %d%s\n", code, j->ign ? " (ignored)" : (opt.keep_going ? " (continuing)" : ""));
    fflush(stdout);
    if (j->ign) {
        j->idx++;
        advance(j);
        return;
    }
    remove_partial(j->n);
    finish_job(j, 0);
}

static void touch_node(Node *n)
{
    if (!opt.silent)
        printf("touch %s\n", n->name);
    fflush(stdout);
    if (!opt.no_exec) {
        int fd = open(n->name, O_WRONLY | O_CREAT, 0666);
        if (fd >= 0)
            close(fd);
        utime(n->name, NULL);
    }
}

static int start_node(Node *n)
{
    Rule *r, *cr = NULL;
    NList all, newer;
    Job *j;
    int i, has_deps = 0, dep_failed = 0, has_single = 0, single_need = 0;
    int has_cmds, need_job = 0;

    memset(&all, 0, sizeof(all));
    memset(&newer, 0, sizeof(newer));
    for (r = n->rules; r != NULL; r = r->next)
        for (i = 0; i < r->deps.n; i++) {
            has_deps = 1;
            if (r->deps.v[i]->failed)
                dep_failed = 1;
        }
    if (n->failed) {
        n->state = S_DONE;
        return 1;
    }
    if (dep_failed) {
        printf("`%s' not remade because of errors.\n", n->name);
        fflush(stdout);
        n->failed = 1;
        n->state = S_DONE;
        any_failed = 1;
        return 1;
    }
    node_stat(n);
    has_cmds = node_has_cmds(n);
    for (r = n->rules; r != NULL; r = r->next) {
        if (r->dcolon)
            continue;
        has_single = 1;
        for (i = 0; i < r->deps.n; i++) {
            Node *d = r->deps.v[i];
            nlist_add(&all, d);
            if (dep_newer(n, d)) {
                nlist_add(&newer, d);
                single_need = 1;
            }
        }
        if (r->bang)
            single_need = 1;
        if (cr == NULL && r->cmds.n > 0)
            cr = r;
    }
    if (has_single && (!n->exists || n->phony || opt.always_make))
        single_need = 1;

    if (has_single && cr != NULL && single_need)
        need_job = 1;
    for (r = n->rules; r != NULL && !need_job; r = r->next) {
        if (r->dcolon && r->cmds.n > 0) {
            int need = (!n->exists || n->phony || opt.always_make || r->deps.n == 0);
            for (i = 0; i < r->deps.n && !need; i++)
                if (dep_newer(n, r->deps.v[i]))
                    need = 1;
            if (need)
                need_job = 1;
        }
    }

    if (!has_cmds && !n->exists && !n->phony && !has_deps) {
        Node *def = node_lookup(".DEFAULT");
        if (def != NULL && node_has_cmds(def)) {
            if (n->impsrc == NULL)
                n->impsrc = nm_strdup(n->name);
            if (opt.question) {
                fflush(stdout);
                exit(1);
            }
            if (running >= opt.jobs && !opt.no_exec)
                return 0;
            j = (Job *)nm_malloc(sizeof(Job));
            memset(j, 0, sizeof(Job));
            j->n = n;
            n->state = S_RUN;
            set_locals(n, &all, &newer);
            for (r = def->rules; r != NULL; r = r->next)
                add_cmds(j, &r->cmds);
            advance(j);
            return 1;
        }
        if (n->rules == NULL) {
            fflush(stdout);
            fprintf(stderr, "%s: don't know how to make %s%s\n", nm_progname, n->name,
                    opt.keep_going ? " (continuing)" : ". Stop");
            if (!opt.keep_going)
                unknown_stop = 1;
            fail_node(n);
            return 1;
        }
    }

    if (!need_job) {
        n->updated = has_single ? single_need : 0;
        n->state = S_DONE;
        return 1;
    }
    if (opt.question) {
        fflush(stdout);
        exit(1);
    }
    if (opt.touch) {
        touch_node(n);
        n->updated = 1;
        n->state = S_DONE;
        node_restat(n);
        return 1;
    }
    if (running >= opt.jobs && !opt.no_exec)
        return 0;

    j = (Job *)nm_malloc(sizeof(Job));
    memset(j, 0, sizeof(Job));
    j->n = n;
    n->state = S_RUN;
    if (has_single && cr != NULL && single_need) {
        set_locals(n, &all, &newer);
        add_cmds(j, &cr->cmds);
    }
    for (r = n->rules; r != NULL; r = r->next) {
        NList rall, rnew;
        int need;
        if (!r->dcolon || r->cmds.n == 0)
            continue;
        memset(&rall, 0, sizeof(rall));
        memset(&rnew, 0, sizeof(rnew));
        need = (!n->exists || n->phony || opt.always_make || r->deps.n == 0);
        for (i = 0; i < r->deps.n; i++) {
            nlist_add(&rall, r->deps.v[i]);
            if (dep_newer(n, r->deps.v[i])) {
                nlist_add(&rnew, r->deps.v[i]);
                need = 1;
            }
        }
        if (need) {
            set_locals(n, &rall, &rnew);
            add_cmds(j, &r->cmds);
        }
        free(rall.v);
        free(rnew.v);
    }
    free(all.v);
    free(newer.v);
    advance(j);
    return 1;
}

static int run_nodes(Node **gl, int ng)
{
    int i;
    nlist = 0;
    for (i = 0; i < ng; i++)
        visit(gl[i]);
    for (;;) {
        int started = 0;
        if (!aborting) {
            for (i = 0; i < nlist; i++) {
                Node *n = list[i];
                if (n->state != S_WAIT || !deps_done(n))
                    continue;
                if (start_node(n))
                    started = 1;
                else
                    break;
                if (aborting)
                    break;
            }
        }
        if (running == 0) {
            if (started && !aborting)
                continue;
            break;
        }
        reap();
    }
    for (i = 0; i < ng; i++) {
        Node *g = gl[i];
        if (!g->updated && !g->failed && !aborting && !opt.silent && g->state == S_DONE && !opt.question)
            printf("%s: `%s' is up to date.\n", nm_progname, g->name);
    }
    fflush(stdout);
    return any_failed;
}

int make_run(void)
{
    Node *begin, *end;
    Node **gl;
    int ng, i;
    char cwd[4096];

    job_install_signals();
    if (goals.n == 0) {
        if (default_goal == NULL) {
            fprintf(stderr, "%s: no target to make.\n", nm_progname);
            return 1;
        }
        gl = (Node **)nm_malloc(sizeof(Node *));
        gl[0] = default_goal;
        ng = 1;
    } else {
        gl = (Node **)nm_malloc((size_t)goals.n * sizeof(Node *));
        for (i = 0; i < goals.n; i++)
            gl[i] = node_find(goals.v[i]);
        ng = goals.n;
    }
    begin = node_lookup(".BEGIN");
    if (begin != NULL && node_has_cmds(begin))
        run_nodes(&begin, 1);
    if (!any_failed)
        run_nodes(gl, ng);
    end = node_lookup(".END");
    if (!any_failed && end != NULL && node_has_cmds(end))
        run_nodes(&end, 1);
    free(gl);
    if (any_failed) {
        if (aborting) {
            printf(unknown_stop ? "\n" : "\nStop.\n");
            if (getcwd(cwd, sizeof(cwd)) != NULL)
                printf("%s: stopped in %s\n", nm_progname, cwd);
            fflush(stdout);
        }
        return unknown_stop ? 2 : 1;
    }
    return 0;
}
