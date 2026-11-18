#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nc.h"

static void usage(void)
{
    puts("usage: nirconfig [-k Kconfig] [-c .config] [-H header] COMMAND [ARGS]");
    puts("");
    puts("commands:");
    puts("  menu           curses menu, saves on 's'");
    puts("  default        write a configuration with all default values");
    puts("  old            reload the configuration, report new symbols, rewrite it");
    puts("  gen            write the header from the existing configuration");
    puts("  list           print every active symbol and its value");
    puts("  set SYM=VAL... set values non-interactively and rewrite the configuration");
    puts("");
    puts("options:");
    puts("  -k FILE        Kconfig file (default: Kconfig)");
    puts("  -c FILE        configuration file (default: .config)");
    puts("  -H FILE        also write a C header (gen writes config.h when omitted)");
}

static void emit(const char *cfile, const char *hfile)
{
    if (nc_conf_write(cfile) == 0)
        printf("%-28s [CONF]\n", cfile);
    if (hfile && nc_header_write(hfile) == 0)
        printf("%-28s [HDR]\n", hfile);
}

static void list_active(void)
{
    const struct nc_sym *s;

    for (s = nc_sym_first(); s; s = s->lnext) {
        if (!nc_sym_active(s) || s->type == NC_UNKNOWN)
            continue;
        if (s->type == NC_STRING)
            printf("%s = \"%s\"\n", s->name, s->value);
        else
            printf("%s = %s\n", s->name, s->value);
    }
}

static void report_new(void)
{
    const struct nc_sym *s;
    int count = 0;

    for (s = nc_sym_first(); s; s = s->lnext) {
        if (s->in_config || !nc_sym_active(s) || !s->prompt || s->type == NC_UNKNOWN)
            continue;
        printf("new symbol: %s = %s\n", s->name, s->value);
        count++;
    }
    if (count == 0)
        puts("no new symbols");
}

static int do_set(int argc, char **argv)
{
    int i;
    char *eq;
    char *name;
    struct nc_sym *s;
    int bad = 0;

    for (i = 0; i < argc; i++) {
        eq = strchr(argv[i], '=');
        if (!eq) {
            nc_warn("expected SYM=VALUE, got \"%s\"", argv[i]);
            bad = 1;
            continue;
        }
        name = nc_xstrndup(argv[i], (size_t)(eq - argv[i]));
        s = nc_sym_find(strncmp(name, "CONFIG_", 7) == 0 ? name + 7 : name);
        if (!s) {
            nc_warn("unknown symbol %s", name);
            bad = 1;
        } else if (nc_sym_set_user(s, eq + 1) != 0) {
            bad = 1;
        }
        free(name);
    }
    nc_resolve();
    return bad;
}

int main(int argc, char **argv)
{
    const char *kfile = "Kconfig";
    const char *cfile = ".config";
    const char *hfile = NULL;
    const char *cmd;
    struct nc_node *root;
    int i = 1;
    int have_conf;

    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-k") && i + 1 < argc) {
            kfile = argv[++i];
        } else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            cfile = argv[++i];
        } else if (!strcmp(argv[i], "-H") && i + 1 < argc) {
            hfile = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
        i++;
    }
    if (i >= argc) {
        usage();
        return 2;
    }
    cmd = argv[i++];

    root = nc_parse(kfile);

    if (!strcmp(cmd, "default")) {
        nc_resolve();
        emit(cfile, hfile);
    } else if (!strcmp(cmd, "old")) {
        have_conf = nc_conf_read(cfile) == 0;
        nc_resolve();
        if (have_conf)
            report_new();
        emit(cfile, hfile);
    } else if (!strcmp(cmd, "gen")) {
        if (nc_conf_read(cfile) != 0)
            nc_die("cannot read %s (run 'nirconfig default' or 'nirconfig menu' first)", cfile);
        nc_resolve();
        if (!hfile)
            hfile = "config.h";
        if (nc_header_write(hfile) == 0)
            printf("%-28s [HDR]\n", hfile);
    } else if (!strcmp(cmd, "list")) {
        nc_conf_read(cfile);
        nc_resolve();
        list_active();
    } else if (!strcmp(cmd, "set")) {
        nc_conf_read(cfile);
        nc_resolve();
        if (do_set(argc - i, argv + i))
            return 1;
        emit(cfile, hfile);
    } else if (!strcmp(cmd, "menu")) {
        nc_conf_read(cfile);
        nc_resolve();
        if (nc_menu_run(root)) {
            emit(cfile, hfile);
        } else {
            puts("configuration not saved");
        }
    } else {
        usage();
        return 2;
    }
    return 0;
}
