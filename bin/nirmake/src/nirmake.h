#ifndef NIRMAKE_H
#define NIRMAKE_H

#include <stddef.h>
#include <time.h>
#include <sys/types.h>

#define NM_VERSION "1.0"

typedef struct Buf {
    char *p;
    size_t len;
    size_t cap;
} Buf;

typedef struct StrList {
    char **v;
    int n;
    int cap;
} StrList;

typedef struct NmTime {
    time_t s;
    long ns;
} NmTime;

enum { ORG_BUILTIN, ORG_ENV, ORG_FILE, ORG_CMD, ORG_LOCAL };

typedef struct Opts {
    int no_exec;
    int silent;
    int keep_going;
    int ignore_err;
    int env_override;
    int question;
    int touch;
    int no_builtin;
    int always_make;
    int jobs;
    StrList files;
    StrList incdirs;
    StrList cmdvars;
    StrList queryvars;
} Opts;

extern Opts opt;
extern StrList goals;
extern const char *nm_progname;
extern const char *nm_file;
extern int nm_line;
extern int nm_errors;

void *nm_malloc(size_t n);
void *nm_realloc(void *p, size_t n);
char *nm_strdup(const char *s);
char *nm_strndup(const char *s, size_t n);
void nm_fatal(const char *fmt, ...);
void nm_error(const char *fmt, ...);
void nm_warn(const char *fmt, ...);
void nm_info(const char *fmt, ...);

void buf_init(Buf *b);
void buf_addc(Buf *b, int c);
void buf_addn(Buf *b, const char *s, size_t n);
void buf_adds(Buf *b, const char *s);
char *buf_take(Buf *b);
void buf_free(Buf *b);

void sl_init(StrList *l);
void sl_add(StrList *l, const char *s);
void sl_addown(StrList *l, char *s);
void sl_free(StrList *l);
int sl_has(const StrList *l, const char *s);
char *sl_join(const StrList *l, const char *sep);

void split_words(const char *s, StrList *out);
char *str_trim(char *s);
char *nm_dirname(const char *path);
const char *nm_basename(const char *path);
char *run_capture(const char *cmd, int *status);
int file_exists(const char *path);
const char *skip_ws(const char *s);

void var_init(char **envp);
void var_set(const char *name, const char *val, int origin);
void var_setx(const char *name, const char *val, int origin);
void var_append(const char *name, const char *val, int origin);
void var_unset(const char *name);
int var_defined(const char *name);
char *var_get(const char *name);
char *var_expand(const char *s);
void var_export(const char *name);
void var_export_all(void);
const char *find_close(const char *s, int open, int close);

int cond_eval(const char *expr, int *result);

typedef struct Node Node;

typedef struct NList {
    Node **v;
    int n;
    int cap;
} NList;

typedef struct Rule {
    NList deps;
    StrList cmds;
    struct Rule *next;
    int dcolon;
    int bang;
    int builtin;
    int cmds_set;
    int inferred;
    int line;
    char *file;
} Rule;

enum { S_NEW, S_VISIT, S_WAIT, S_RUN, S_DONE };

struct Node {
    char *name;
    Rule *rules;
    Rule *rules_tail;
    int is_target;
    int is_xform;
    int phony;
    int silent;
    int ignore;
    int precious;
    int state;
    int failed;
    int updated;
    int exists;
    int stat_done;
    NmTime mtime;
    char *impsrc;
    char *prefix;
    Node *next;
    Node *hnext;
};

extern Node *node_first;
extern Node *default_goal;
extern int global_silent;
extern int global_ignore;
extern int global_precious;
extern StrList suffixes;

Node *node_find(const char *name);
Node *node_lookup(const char *name);
Rule *node_new_rule(Node *n, int dcolon);
void nlist_add(NList *l, Node *n);
void node_stat(Node *n);
void node_restat(Node *n);
int node_has_cmds(Node *n);
int nm_time_cmp(NmTime a, NmTime b);
void nm_time_now(NmTime *t);
void suff_add(const char *s);
void suff_clear(void);
int suff_known(const char *s);
int xform_name(const char *name);
void meta_add(const char *tgt, const StrList *srcs, const StrList *cmds);
int node_infer(Node *n);

void parse_file(const char *path, int must_exist);
void parse_string(const char *text, const char *name, int origin_builtin);
void parse_finish(void);

void builtin_load(void);

int opt_apply(const char *flags);
void opt_setup_makeflags(void);

int make_run(void);
pid_t job_spawn(const char *cmd, int ignore_err);
pid_t job_wait(int *status);
void job_track(pid_t pid, const char *target, int existed, NmTime before);
void job_untrack(pid_t pid);
void job_install_signals(void);

#endif
