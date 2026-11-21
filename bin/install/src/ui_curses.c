#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"
#include "ui.h"

#define PAIR_BASE 1
#define PAIR_SEL 2
#define PAIR_TITLE 3
#define PAIR_ERR 4
#define PAIR_OK 5

static WINDOW *win;
static int rows, cols, wh, ww;

static int attr_base(void)
{
    return has_colors() ? COLOR_PAIR(PAIR_BASE) : A_NORMAL;
}

static int attr_sel(void)
{
    return has_colors() ? COLOR_PAIR(PAIR_SEL) : A_REVERSE;
}

static int attr_title(void)
{
    return has_colors() ? COLOR_PAIR(PAIR_TITLE) | A_BOLD : A_BOLD;
}

static int attr_err(void)
{
    return has_colors() ? COLOR_PAIR(PAIR_ERR) | A_BOLD : A_BOLD;
}

static int attr_ok(void)
{
    return has_colors() ? COLOR_PAIR(PAIR_OK) | A_BOLD : A_BOLD;
}

static void setup_colors(void)
{
    if (!has_colors())
        return;
    start_color();
    init_pair(PAIR_BASE, COLOR_WHITE, COLOR_BLUE);
    init_pair(PAIR_SEL, COLOR_BLACK, COLOR_CYAN);
    init_pair(PAIR_TITLE, COLOR_YELLOW, COLOR_BLUE);
    init_pair(PAIR_ERR, COLOR_RED, COLOR_BLUE);
    init_pair(PAIR_OK, COLOR_GREEN, COLOR_BLUE);
}

static void begin(const char *title, const char *help)
{
    int y, x;

    getmaxyx(stdscr, rows, cols);
    wh = rows - 4 < 18 ? rows - 4 : 18;
    ww = cols - 4 < 72 ? cols - 4 : 72;
    y = (rows - wh) / 2;
    x = (cols - ww) / 2;
    if (win)
        delwin(win);
    bkgd(attr_base());
    erase();
    attron(A_REVERSE);
    mvprintw(0, 0, "%-*.*s", cols, cols, " NirmalaSYS installer");
    mvprintw(rows - 1, 0, "%-*.*s", cols, cols, help);
    attroff(A_REVERSE);
    refresh();
    win = newwin(wh, ww, y, x);
    keypad(win, TRUE);
    wbkgd(win, attr_base());
    werase(win);
    wborder(win, '|', '|', '-', '-', '+', '+', '+', '+');
    wattron(win, attr_title());
    mvwprintw(win, 0, 2, " %s ", title);
    wattroff(win, attr_title());
}

static void text(int y, const char *s)
{
    mvwprintw(win, y, 3, "%-*.*s", ww - 6, ww - 6, s);
}

static int menu(int y, const char *const *items, const int *disabled, int n, int sel)
{
    int ch, i, w = ww - 6;

    for (;;) {
        for (i = 0; i < n; i++) {
            if (i == sel)
                wattron(win, attr_sel());
            mvwprintw(win, y + i, 3, "%-*.*s", w, w, items[i]);
            if (i == sel)
                wattroff(win, attr_sel());
        }
        wrefresh(win);
        ch = wgetch(win);
        if (ch == KEY_DOWN || ch == 'j') {
            int k = sel;

            do {
                k = (k + 1) % n;
            } while (disabled && disabled[k] && k != sel);
            sel = k;
        } else if (ch == KEY_UP || ch == 'k') {
            int k = sel;

            do {
                k = (k + n - 1) % n;
            } while (disabled && disabled[k] && k != sel);
            sel = k;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (!disabled || !disabled[sel])
                return sel;
        } else if (ch == 'q') {
            return -1;
        }
    }
}

static int input(int y, const char *label, char *buf, size_t size, int secret)
{
    size_t n = 0;
    int ch, i, lw = (int)strlen(label), field = ww - 10 - lw;

    if (field < 4)
        field = 4;
    buf[0] = 0;
    curs_set(1);
    for (;;) {
        int shown = n > (size_t)field ? field : (int)n;

        mvwprintw(win, y, 3, "%s: ", label);
        for (i = 0; i < field; i++)
            waddch(win, ' ');
        wmove(win, y, 3 + lw + 2);
        for (i = 0; i < shown; i++)
            waddch(win, secret ? '*' : (unsigned char)buf[i]);
        wrefresh(win);
        ch = wgetch(win);
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            curs_set(0);
            return 0;
        }
        if (ch == 27) {
            curs_set(0);
            return -1;
        }
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && n)
            buf[--n] = 0;
        else if (ch >= 32 && ch < 127 && n + 1 < size) {
            buf[n++] = (char)ch;
            buf[n] = 0;
        }
    }
}

static void note(const char *title, const char *line1, const char *line2)
{
    static const char *const items[] = { "Continue" };

    begin(title, " Enter: continue");
    wattron(win, attr_err());
    text(2, line1);
    wattroff(win, attr_err());
    text(4, line2);
    menu(7, items, NULL, 1, 0);
}

static int screen_welcome(void)
{
    static const char *const items[] = { "Install NirmalaSYS to a disk", "Quit to the shell" };

    begin("Welcome", " Up/Down: select   Enter: continue   q: quit");
    text(2, "Welcome to NirmalaSYS.");
    text(4, "This installer prepares a disk for NirmalaSYS:");
    text(5, "  - an EFI boot partition (100 MiB)");
    text(6, "  - a root partition using the rest of the disk");
    text(7, "  - an administrator account and an optional user");
    text(9, "The machine must start from the disk in UEFI mode.");
    return menu(12, items, NULL, 2, 0);
}

static int screen_disks(const struct inst_disk *disks, int count)
{
    const char *items[INST_MAX_DISKS + 1];
    char lines[INST_MAX_DISKS][96];
    int disabled[INST_MAX_DISKS + 1];
    int i, usable = 0, sel;

    begin("Select a disk", " Up/Down: select   Enter: choose   q: back");
    for (i = 0; i < count; i++) {
        snprintf(lines[i], sizeof(lines[i]), "%-6s %8lu MiB   %s%s", disks[i].name, (unsigned long)disks[i].mib,
                 disks[i].model, disks[i].busy ? "   (in use)" : "");
        items[i] = lines[i];
        disabled[i] = disks[i].busy;
        if (!disks[i].busy)
            usable++;
    }
    items[count] = "Back";
    disabled[count] = 0;
    if (count == 0) {
        text(2, "No disks were found.");
    } else if (usable == 0) {
        text(2, "Every disk is in use by the running system.");
    } else {
        text(2, "Choose the disk to install to:");
    }
    for (sel = 0; sel < count && disabled[sel]; sel++)
        ;
    i = menu(4, items, disabled, count + 1, sel);
    return i == count ? -1 : i;
}

static int screen_password(const char *title, const char *who, char *out, size_t size)
{
    char again[128], line[96];

    for (;;) {
        begin(title, " Enter: continue   Esc: back");
        snprintf(line, sizeof(line), "Set a password for %s.", who);
        text(2, line);
        if (input(4, "Password", out, size, 1) < 0)
            return -1;
        if (!out[0]) {
            note(title, "The password cannot be empty.", "Choose a different password.");
            continue;
        }
        if (input(6, "Retype password", again, sizeof(again), 1) < 0)
            return -1;
        if (!strcmp(out, again)) {
            memset(again, 0, sizeof(again));
            return 0;
        }
        memset(again, 0, sizeof(again));
        note(title, "The passwords do not match.", "Enter the password again.");
    }
}

static int screen_accounts(struct account *acc, int *nacc)
{
    char name[64];

    memset(acc, 0, sizeof(*acc) * MKROOT_MAX_ACCOUNTS);
    snprintf(acc[0].name, sizeof(acc[0].name), "root");
    snprintf(acc[0].home, sizeof(acc[0].home), "/root");
    snprintf(acc[0].shell, sizeof(acc[0].shell), "/bin/sh");
    if (screen_password("Administrator account", "root", acc[0].password, sizeof(acc[0].password)) < 0)
        return -1;

    for (;;) {
        begin("User account", " Enter: continue   Esc: back");
        text(2, "Create a regular account, or leave the name empty to skip.");
        text(3, "Use a-z, 0-9, '_' and '-', starting with a letter or '_'.");
        if (input(5, "Username", name, sizeof(name), 0) < 0)
            return -1;
        if (!name[0]) {
            *nacc = 1;
            return 0;
        }
        if (inst_valid_username(name))
            break;
        note("User account", "That username is not valid.", "Use 1-31 characters and not \"root\".");
    }
    snprintf(acc[1].name, sizeof(acc[1].name), "%s", name);
    snprintf(acc[1].home, sizeof(acc[1].home), "/home/%s", name);
    snprintf(acc[1].shell, sizeof(acc[1].shell), "/bin/sh");
    acc[1].uid = acc[1].gid = 1000;
    if (screen_password("User account", name, acc[1].password, sizeof(acc[1].password)) < 0)
        return -1;
    *nacc = 2;
    return 0;
}

static int screen_confirm(const struct inst_disk *d, const struct inst_layout *lay, const struct account *acc,
                          int nacc)
{
    static const char *const items[] = { "Cancel", "Erase the disk and install" };
    char line[96];
    int r;

    begin("Confirm", " Up/Down: select   Enter: confirm");
    snprintf(line, sizeof(line), "Install to %s (%lu MiB, %s)", d->name, (unsigned long)d->mib, d->model);
    text(2, line);
    text(4, "Partition 1: EFI boot partition, 100 MiB");
    snprintf(line, sizeof(line), "Partition 2: NirmalaSYS root, %lu MiB", (unsigned long)(lay->root_sectors / 2048));
    text(5, line);
    snprintf(line, sizeof(line), "Accounts: %s%s%s", acc[0].name, nacc > 1 ? ", " : "", nacc > 1 ? acc[1].name : "");
    text(6, line);
    wattron(win, attr_err());
    text(8, "ALL DATA ON THIS DISK WILL BE ERASED.");
    wattroff(win, attr_err());
    r = menu(11, items, NULL, 2, 0);
    return r == 1;
}

static void draw_progress(int step, void *ctx)
{
    int i, gw = ww - 14, fill, pct;

    (void)ctx;
    for (i = 0; i < INST_STEPS; i++) {
        const char *mark = i < step ? "[x]" : i == step ? "[>]" : "[ ]";

        if (i == step)
            wattron(win, A_BOLD);
        mvwprintw(win, 2 + i, 3, "%s %-*.*s", mark, ww - 12, ww - 12, inst_step_names[i]);
        if (i == step)
            wattroff(win, A_BOLD);
    }
    fill = step >= INST_STEPS ? gw : step * gw / INST_STEPS;
    pct = step * 100 / INST_STEPS > 100 ? 100 : step * 100 / INST_STEPS;
    mvwprintw(win, 11, 3, "[");
    for (i = 0; i < gw; i++)
        waddch(win, i < fill ? '#' : ' ');
    wprintw(win, "] %3d%%", pct);
    wrefresh(win);
}

static int screen_install(const struct inst_disk *d, const struct inst_layout *lay, const struct account *acc,
                          int nacc)
{
    char err[160];
    static const char *const items[] = { "Return" };
    int r;

    begin("Installing", " Please wait. Do not power off the machine.");
    draw_progress(0, NULL);
    r = inst_run(d->name, lay, acc, (unsigned)nacc, draw_progress, NULL, err, sizeof(err));
    begin(r == 0 ? "Finished" : "Installation failed", " Enter: continue");
    if (r == 0) {
        wattron(win, attr_ok());
        text(2, "Installation complete.");
        wattroff(win, attr_ok());
        text(4, "Shut down, remove the installation media");
        text(5, "and boot the new disk in UEFI mode.");
    } else {
        wattron(win, attr_err());
        text(2, "The installation did not complete.");
        wattroff(win, attr_err());
        text(4, err);
        text(5, "No further changes were made to the running system.");
    }
    menu(8, items, NULL, 1, 0);
    return r == 0 ? 0 : 1;
}

int ui_curses_run(void)
{
    struct inst_disk disks[INST_MAX_DISKS];
    struct account acc[MKROOT_MAX_ACCOUNTS];
    struct inst_layout lay;
    SCREEN *scr;
    int rc = 1;

    scr = newterm(getenv("TERM") ? getenv("TERM") : "linux", stdout, stdin);
    if (!scr)
        return -1;
    set_term(scr);
    cbreak();
    noecho();
    nonl();
    keypad(stdscr, TRUE);
    curs_set(0);
    setup_colors();

    for (;;) {
        char err[128];
        int count, pick, nacc = 0, choice = screen_welcome();

        if (choice != 0) {
            rc = 1;
            break;
        }
        count = inst_list(disks, INST_MAX_DISKS);
        pick = screen_disks(disks, count);
        if (pick < 0)
            continue;
        if (inst_plan(disks[pick].name, &lay, err, sizeof(err)) < 0) {
            note("Cannot install", err, "Choose another disk.");
            continue;
        }
        if (screen_accounts(acc, &nacc) < 0) {
            memset(acc, 0, sizeof(acc));
            continue;
        }
        if (!screen_confirm(&disks[pick], &lay, acc, nacc)) {
            memset(acc, 0, sizeof(acc));
            continue;
        }
        rc = screen_install(&disks[pick], &lay, acc, nacc);
        memset(acc, 0, sizeof(acc));
        break;
    }
    if (win)
        delwin(win);
    bkgd(A_NORMAL);
    clear();
    refresh();
    curs_set(1);
    endwin();
    return rc;
}
