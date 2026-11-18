#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <curses.h>
#include "nc.h"

#define MAXDEPTH 32
#define MAXHELP  512
#define KEY_ESC  27

struct frame {
    struct nc_node *menu;
    int cur;
    int top;
};

static int node_visible(const struct nc_node *n)
{
    if (n->kind == N_SYM)
        return n->sym->type != NC_UNKNOWN && n->sym->prompt && n->sym->visible;
    if (!n->dep)
        return 1;
    return nc_truthy(nc_expr_eval(n->dep));
}

static int collect(const struct nc_node *m, struct nc_node ***out)
{
    const struct nc_node *c;
    struct nc_node **v;
    int n = 0;

    for (c = m->child; c; c = c->next)
        n++;
    v = nc_xmalloc(sizeof *v * (size_t)(n + 1));
    n = 0;
    for (c = m->child; c; c = c->next)
        if (node_visible(c))
            v[n++] = (struct nc_node *)c;
    *out = v;
    return n;
}

static char *label(const struct nc_node *n)
{
    struct nc_buf b;
    const struct nc_sym *s;

    nc_buf_init(&b);
    if (n->kind == N_COMMENT) {
        nc_buf_add(&b, "    *** ");
        nc_buf_add(&b, n->text);
        nc_buf_add(&b, " ***");
        return nc_buf_take(&b);
    }
    if (n->kind == N_MENU) {
        nc_buf_add(&b, "    --->  ");
        nc_buf_add(&b, n->text);
        return nc_buf_take(&b);
    }
    s = n->sym;
    switch (s->type) {
    case NC_BOOL:
        if (s->forced)
            nc_buf_add(&b, "-*-   ");
        else if (!strcmp(s->value, "y"))
            nc_buf_add(&b, "[*]   ");
        else
            nc_buf_add(&b, "[ ]   ");
        break;
    case NC_STRING:
        nc_buf_add(&b, "(\"");
        nc_buf_add(&b, s->value);
        nc_buf_add(&b, "\")  ");
        break;
    default:
        nc_buf_add(&b, "(");
        nc_buf_add(&b, s->value);
        nc_buf_add(&b, ")  ");
        break;
    }
    nc_buf_add(&b, s->prompt);
    return nc_buf_take(&b);
}

static void line(int y, const char *text, int attr)
{
    int rows, cols, w, i, len;

    getmaxyx(stdscr, rows, cols);
    w = (y == rows - 1) ? cols - 1 : cols;
    if (w < 1)
        return;
    move(y, 0);
    if (attr)
        attron(attr);
    len = (int)strlen(text);
    if (len > w)
        len = w;
    addnstr(text, len);
    for (i = len; i < w; i++)
        addch(' ');
    if (attr)
        attroff(attr);
}

static int first_sel(struct nc_node **it, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (it[i]->kind != N_COMMENT)
            return i;
    return 0;
}

static int step(struct nc_node **it, int n, int cur, int dir)
{
    int i = cur + dir;

    while (i >= 0 && i < n) {
        if (it[i]->kind != N_COMMENT)
            return i;
        i += dir;
    }
    return cur;
}

static void draw(struct frame *st, int sp, struct nc_node **it, int n, const char *status)
{
    struct frame *f = &st[sp];
    struct nc_buf path;
    char *text, *p;
    int rows, cols, listh, i;

    getmaxyx(stdscr, rows, cols);
    (void)cols;
    listh = rows - 4;
    if (listh < 1)
        listh = 1;
    if (f->cur < f->top)
        f->top = f->cur;
    if (f->cur >= f->top + listh)
        f->top = f->cur - listh + 1;
    if (f->top < 0)
        f->top = 0;

    erase();
    line(0, " NirmalaSYS configuration", A_REVERSE);

    nc_buf_init(&path);
    for (i = 0; i <= sp; i++) {
        if (i)
            nc_buf_add(&path, " > ");
        nc_buf_add(&path, st[i].menu->text);
    }
    p = nc_buf_take(&path);
    line(1, p, A_BOLD);
    free(p);

    for (i = 0; i < listh; i++) {
        if (f->top + i >= n)
            break;
        text = label(it[f->top + i]);
        line(2 + i, text, f->top + i == f->cur ? A_REVERSE : 0);
        free(text);
    }

    line(rows - 2, " Up/Down move  Enter select  y/n set  ? help  Left back  s save  q quit", A_REVERSE);
    line(rows - 1, status, 0);
    refresh();
}

static void show_help(const struct nc_node *n)
{
    static int starts[MAXHELP];
    static int lens[MAXHELP];
    struct nc_buf b;
    char *text;
    int rows, cols, width, count, off, i, j, ch, l, brk, pos, total;

    nc_buf_init(&b);
    if (n->kind == N_MENU) {
        nc_buf_add(&b, n->text);
        nc_buf_add(&b, "\n\n(submenu)");
    } else if (n->kind == N_COMMENT) {
        nc_buf_add(&b, n->text);
    } else {
        nc_buf_add(&b, n->sym->prompt);
        nc_buf_add(&b, " (CONFIG_");
        nc_buf_add(&b, n->sym->name);
        nc_buf_add(&b, ")\n\n");
        nc_buf_add(&b, n->sym->help ? n->sym->help : "no help available");
    }
    text = nc_buf_take(&b);
    total = (int)strlen(text);
    off = 0;

    for (;;) {
        getmaxyx(stdscr, rows, cols);
        width = cols - 4;
        if (width < 10)
            width = 10;
        count = 0;
        pos = 0;
        while (count < MAXHELP) {
            l = 0;
            brk = -1;
            for (j = pos; j < total && text[j] != '\n' && l < width; j++, l++)
                if (text[j] == ' ')
                    brk = j;
            if (j < total && text[j] != '\n' && brk > pos) {
                starts[count] = pos;
                lens[count++] = brk - pos;
                pos = brk + 1;
            } else {
                starts[count] = pos;
                lens[count++] = l;
                if (j >= total)
                    break;
                pos = text[j] == '\n' ? j + 1 : j;
            }
        }
        if (off > count - (rows - 3))
            off = count - (rows - 3);
        if (off < 0)
            off = 0;

        erase();
        line(0, " Help", A_REVERSE);
        for (i = 0; i < rows - 3 && off + i < count; i++) {
            move(2 + i, 2);
            addnstr(text + starts[off + i], lens[off + i]);
        }
        line(rows - 1, " Up/Down scroll  any other key to close", A_REVERSE);
        refresh();

        ch = getch();
        if (ch == KEY_UP || ch == 'k')
            off--;
        else if (ch == KEY_DOWN || ch == 'j')
            off++;
        else if (ch == KEY_PPAGE)
            off -= rows - 4;
        else if (ch == KEY_NPAGE)
            off += rows - 4;
        else if (ch != KEY_RESIZE)
            break;
    }
    free(text);
}

static int edit(struct nc_sym *s)
{
    char buf[256];
    char head[256];
    const char *tn = s->type == NC_INT ? "int" : s->type == NC_HEX ? "hex" : "string";
    int rows, cols, len, ch, i, w, ok;

    strncpy(buf, s->value, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    len = (int)strlen(buf);

    for (;;) {
        getmaxyx(stdscr, rows, cols);
        sprintf(head, " %.120s (%s): ", s->prompt, tn);
        w = cols - 1;
        move(rows - 1, 0);
        attron(A_REVERSE);
        for (i = 0; i < w; i++)
            addch(' ');
        move(rows - 1, 0);
        addnstr(head, w);
        attroff(A_REVERSE);
        i = (int)strlen(head);
        if (i < w)
            addnstr(buf, w - i);
        refresh();

        ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (len == 0 && s->type != NC_STRING)
                return 0;
            ok = nc_sym_set_user(s, buf) == 0;
            if (ok)
                nc_resolve();
            return ok ? 1 : -1;
        }
        if (ch == KEY_ESC || ch == ERR)
            return 0;
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0)
                buf[--len] = '\0';
        } else if (ch >= 32 && ch < 127 && len < (int)sizeof buf - 1) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
}

static void activate(struct nc_node *n, struct frame *st, int *sp, char *status, int *dirty)
{
    struct nc_sym *s;
    int r;

    if (n->kind == N_MENU) {
        if (*sp + 1 < MAXDEPTH) {
            (*sp)++;
            st[*sp].menu = n;
            st[*sp].cur = -1;
            st[*sp].top = 0;
        }
        return;
    }
    s = n->sym;
    if (s->type == NC_BOOL) {
        if (s->forced) {
            sprintf(status, " %.60s is selected by another symbol", s->name);
            return;
        }
        if (nc_sym_set_user(s, strcmp(s->value, "y") ? "y" : "n") == 0) {
            nc_resolve();
            *dirty = 1;
        }
        return;
    }
    r = edit(s);
    if (r > 0)
        *dirty = 1;
    else if (r < 0)
        sprintf(status, " invalid value for %.60s", s->name);
}

static void set_bool(struct nc_node *n, const char *val, char *status, int *dirty)
{
    struct nc_sym *s;

    if (n->kind != N_SYM || n->sym->type != NC_BOOL)
        return;
    s = n->sym;
    if (s->forced) {
        sprintf(status, " %.60s is selected by another symbol", s->name);
        return;
    }
    if (nc_sym_set_user(s, val) == 0) {
        nc_resolve();
        *dirty = 1;
    } else {
        sprintf(status, " cannot set %.60s", s->name);
    }
}

int nc_menu_run(struct nc_node *root)
{
    struct frame st[MAXDEPTH];
    struct nc_node **items;
    struct frame *f;
    char status[200];
    int sp = 0;
    int n, ch, rows, cols, listh, i;
    int dirty = 0;
    int result = 0;
    int saved_err, devnull;

    saved_err = dup(2);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        dup2(devnull, 2);
        close(devnull);
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    st[0].menu = root;
    st[0].cur = -1;
    st[0].top = 0;
    status[0] = '\0';

    for (;;) {
        f = &st[sp];
        n = collect(f->menu, &items);
        if (f->cur < 0 || f->cur >= n || items[f->cur]->kind == N_COMMENT)
            f->cur = first_sel(items, n);
        draw(st, sp, items, n, status);
        status[0] = '\0';
        getmaxyx(stdscr, rows, cols);
        (void)cols;
        listh = rows - 4;
        if (listh < 1)
            listh = 1;

        ch = getch();
        if (ch == ERR) {
            free(items);
            break;
        }
        if (n == 0 && ch != KEY_LEFT && ch != KEY_ESC && ch != 'b' && ch != 's' && ch != 'q') {
            free(items);
            continue;
        }
        if (ch == KEY_UP || ch == 'k') {
            f->cur = step(items, n, f->cur, -1);
        } else if (ch == KEY_DOWN || ch == 'j') {
            f->cur = step(items, n, f->cur, 1);
        } else if (ch == KEY_PPAGE) {
            for (i = 0; i < listh; i++)
                f->cur = step(items, n, f->cur, -1);
        } else if (ch == KEY_NPAGE) {
            for (i = 0; i < listh; i++)
                f->cur = step(items, n, f->cur, 1);
        } else if (ch == KEY_HOME || ch == 'g') {
            f->cur = first_sel(items, n);
        } else if (ch == KEY_END || ch == 'G') {
            f->cur = n - 1;
            if (items[f->cur]->kind == N_COMMENT)
                f->cur = step(items, n, f->cur, -1);
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == KEY_RIGHT || ch == ' ') {
            activate(items[f->cur], st, &sp, status, &dirty);
        } else if (ch == 'y') {
            set_bool(items[f->cur], "y", status, &dirty);
        } else if (ch == 'n') {
            set_bool(items[f->cur], "n", status, &dirty);
        } else if (ch == KEY_LEFT || ch == KEY_ESC || ch == 'b' || ch == KEY_BACKSPACE) {
            if (sp > 0)
                sp--;
            else
                strcpy(status, " already at the top level, s saves and q quits");
        } else if (ch == '?' || ch == 'h') {
            show_help(items[f->cur]);
        } else if (ch == 's') {
            result = 1;
            free(items);
            break;
        } else if (ch == 'q') {
            if (!dirty) {
                free(items);
                break;
            }
            line(rows - 1, " Discard changes? (y/n)", A_REVERSE);
            refresh();
            ch = getch();
            if (ch == 'y' || ch == 'Y') {
                free(items);
                break;
            }
        }
        free(items);
    }

    endwin();
    if (saved_err >= 0) {
        dup2(saved_err, 2);
        close(saved_err);
    }
    return result;
}
