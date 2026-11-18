#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#ifndef R_OK
#define R_OK 4
#endif

#define MAX_ARGS 256
#define MAX_FILES 64

struct opts {
    char *inputs[MAX_FILES];
    int ninputs;
    char *cc1_flags[MAX_ARGS];
    int ncc1;
    char *ld_flags[MAX_ARGS];
    int nld;
    const char *out;
    int bits;
    int stop_s;
    int stop_c;
    int only_pp;
    int save_temps;
};

static char *self_dir;
static char *temps[MAX_FILES * 2];
static int ntemps;

static void cleanup(void)
{
    int i;

    for (i = 0; i < ntemps; i++)
        unlink(temps[i]);
}

static void fail(const char *msg, const char *arg)
{
    fprintf(stderr, "cc: error: %s '%s'\n", msg, arg);
    cleanup();
    exit(1);
}

static int run(char **argv)
{
    int status;
    int pid;

    pid = fork();
    if (pid < 0)
        fail("cannot fork", argv[0]);
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "cc: error: cannot run '%s'\n", argv[0]);
        _exit(127);
    }
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static char *dir_of(const char *path)
{
    char *d;
    char *slash;

    d = strdup(path);
    slash = strrchr(d, '/');
    if (slash)
        *slash = '\0';
    else
        strcpy(d, ".");
    return d;
}

static char *join(const char *a, const char *b)
{
    char *r;

    r = malloc(strlen(a) + strlen(b) + 2);
    sprintf(r, "%s/%s", a, b);
    return r;
}

static const char *ext_of(const char *path)
{
    const char *dot;

    dot = strrchr(path, '.');
    return dot ? dot : "";
}

static char *swap_ext(const char *path, const char *ext)
{
    char *r;
    char *slash;
    char *dot;

    slash = strrchr(path, '/');
    r = malloc(strlen(path) + strlen(ext) + 1);
    strcpy(r, slash ? slash + 1 : path);
    dot = strrchr(r, '.');
    if (dot)
        *dot = '\0';
    strcat(r, ext);
    return r;
}

static char *make_temp(const char *suffix, int save)
{
    char *name;
    int fd;
    int tries;
    static int counter;

    if (save)
        return swap_ext("a", suffix);
    name = malloc(64 + strlen(suffix));
    for (tries = 0; tries < 100; tries++) {
        sprintf(name, "/tmp/cc%d_%d%s", (int)getpid(), counter++, suffix);
        fd = open(name, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd >= 0) {
            close(fd);
            temps[ntemps++] = name;
            return name;
        }
    }
    fail("cannot create temporary file", name);
    return name;
}

static void parse_args(int argc, char **argv, struct opts *o)
{
    int i;

    o->bits = (int)sizeof(void *) * 8;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            o->out = argv[++i];
        else if (strcmp(argv[i], "-S") == 0)
            o->stop_s = 1;
        else if (strcmp(argv[i], "-c") == 0)
            o->stop_c = 1;
        else if (strcmp(argv[i], "-E") == 0)
            o->only_pp = 1;
        else if (strcmp(argv[i], "-save-temps") == 0)
            o->save_temps = 1;
        else if (strcmp(argv[i], "-m32") == 0)
            o->bits = 32;
        else if (strcmp(argv[i], "-m64") == 0)
            o->bits = 64;
        else if (strncmp(argv[i], "-D", 2) == 0
             || strncmp(argv[i], "-I", 2) == 0
             || strncmp(argv[i], "-O", 2) == 0)
            o->cc1_flags[o->ncc1++] = argv[i];
        else if (argv[i][0] == '-')
            o->ld_flags[o->nld++] = argv[i];
        else if (o->ninputs < MAX_FILES)
            o->inputs[o->ninputs++] = argv[i];
    }
    if (!o->ninputs)
        fail("no input files", "");
}

static int native_ld(void)
{
    return access("/usr/lib/crt0.o", R_OK) == 0;
}

static char *nircc_path(void)
{
    if (native_ld())
        return "/bin/nircc";
    return join(self_dir, "nircc");
}

static int add_incs(char **argv, int n)
{
    char *p;

    if (native_ld()) {
        argv[n++] = "-I/usr/lib/nircc/include";
        argv[n++] = "-I/usr/include";
        return n;
    }
    p = malloc(strlen(self_dir) + 16);
    sprintf(p, "-I%s/include", self_dir);
    argv[n++] = p;
    return n;
}

static char *compile_c(struct opts *o, const char *in, const char *asm_out)
{
    char *argv[MAX_ARGS + 16];
    char bits[8];
    int n;
    int i;

    sprintf(bits, "-m%d", o->bits);
    n = 0;
    argv[n++] = nircc_path();
    argv[n++] = bits;
    n = add_incs(argv, n);
    for (i = 0; i < o->ncc1; i++)
        argv[n++] = o->cc1_flags[i];
    if (o->only_pp)
        argv[n++] = "-E";
    argv[n++] = "-o";
    argv[n++] = (char *)asm_out;
    argv[n++] = (char *)in;
    argv[n] = NULL;
    if (run(argv) != 0)
        fail("compilation failed for", in);
    return (char *)asm_out;
}

static void assemble(struct opts *o, const char *in, const char *obj)
{
    char *argv[8];

    argv[0] = "as";
    argv[1] = o->bits == 32 ? "--32" : "--64";
    argv[2] = (char *)in;
    argv[3] = "-o";
    argv[4] = (char *)obj;
    argv[5] = NULL;
    if (run(argv) != 0)
        fail("assembler failed for", in);
}

static void compile_obj(struct opts *o, const char *in, const char *obj)
{
    char *argv[MAX_ARGS + 16];
    int n;
    int i;

    n = 0;
    argv[n++] = nircc_path();
    argv[n++] = "-c";
    n = add_incs(argv, n);
    for (i = 0; i < o->ncc1; i++)
        argv[n++] = o->cc1_flags[i];
    argv[n++] = "-o";
    argv[n++] = (char *)obj;
    argv[n++] = (char *)in;
    argv[n] = NULL;
    if (run(argv) != 0)
        fail("compilation failed for", in);
}

static void link_nirld(struct opts *o, char **objs, int nobj)
{
    char *argv[MAX_FILES + MAX_ARGS + 12];
    int n;
    int i;

    n = 0;
    argv[n++] = "nirld";
    argv[n++] = "-o";
    argv[n++] = (char *)(o->out ? o->out : "a.out");
    argv[n++] = "/usr/lib/crt0.o";
    for (i = 0; i < nobj; i++)
        argv[n++] = objs[i];
    for (i = 0; i < o->nld; i++)
        argv[n++] = o->ld_flags[i];
    argv[n++] = "-L/usr/lib";
    argv[n++] = "-lc";
    argv[n] = NULL;
    if (run(argv) != 0)
        fail("link failed", argv[2]);
}

static void link_all(struct opts *o, char **objs, int nobj)
{
    char *argv[MAX_FILES + MAX_ARGS + 8];
    int n;
    int i;

    if (native_ld()) {
        link_nirld(o, objs, nobj);
        return;
    }
    n = 0;
    argv[n++] = "cc";
    argv[n++] = o->bits == 32 ? "-m32" : "-m64";
    argv[n++] = "-no-pie";
    for (i = 0; i < nobj; i++)
        argv[n++] = objs[i];
    for (i = 0; i < o->nld; i++)
        argv[n++] = o->ld_flags[i];
    argv[n++] = "-o";
    argv[n++] = (char *)(o->out ? o->out : "a.out");
    argv[n] = NULL;
    if (run(argv) != 0)
        fail("link failed", argv[n - 1]);
}

int main(int argc, char **argv)
{
    struct opts o;
    char *objs[MAX_FILES];
    char *asm_path;
    char *obj_path;
    const char *ext;
    int nobj;
    int i;

    memset(&o, 0, sizeof o);
    self_dir = dir_of(argv[0]);
    parse_args(argc, argv, &o);
    nobj = 0;
    for (i = 0; i < o.ninputs; i++) {
        ext = ext_of(o.inputs[i]);
        if (strcmp(ext, ".o") == 0) {
            objs[nobj++] = o.inputs[i];
            continue;
        }
        if (strcmp(ext, ".c") == 0 && !o.only_pp && !o.stop_s && o.bits == 64) {
            if (o.stop_c && o.out && o.ninputs == 1)
                obj_path = (char *)o.out;
            else if (o.stop_c)
                obj_path = swap_ext(o.inputs[i], ".o");
            else
                obj_path = make_temp(".o", 0);
            compile_obj(&o, o.inputs[i], obj_path);
            objs[nobj++] = obj_path;
            continue;
        }
        if (strcmp(ext, ".c") == 0) {
            if (o.only_pp || o.stop_s) {
                asm_path = (char *)(o.out ? o.out
                    : o.only_pp ? "/dev/stdout"
                    : swap_ext(o.inputs[i], ".s"));
            } else {
                asm_path = make_temp(".s", o.save_temps);
                if (o.save_temps)
                    asm_path = swap_ext(o.inputs[i], ".s");
            }
            compile_c(&o, o.inputs[i], asm_path);
            if (o.only_pp || o.stop_s)
                continue;
        } else if (strcmp(ext, ".s") == 0) {
            asm_path = o.inputs[i];
        } else {
            fail("unknown file type", o.inputs[i]);
            return 1;
        }
        if (o.stop_c && o.out && o.ninputs == 1)
            obj_path = (char *)o.out;
        else if (o.stop_c)
            obj_path = swap_ext(o.inputs[i], ".o");
        else
            obj_path = make_temp(".o", 0);
        assemble(&o, asm_path, obj_path);
        objs[nobj++] = obj_path;
    }
    if (!o.only_pp && !o.stop_s && !o.stop_c)
        link_all(&o, objs, nobj);
    cleanup();
    return 0;
}
