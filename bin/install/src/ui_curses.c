#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tz.h>
#include "engine.h"
#include "ui.h"

#define PAIR_BASE 1
#define PAIR_SEL 2
#define PAIR_TITLE 3
#define PAIR_ERR 4
#define PAIR_OK 5

static WINDOW *win;
static int rows, cols, wh, ww;
static unsigned part_esp, part_swap, part_root;

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

static void status(const char *s)
{
    attron(A_REVERSE);
    mvprintw(rows - 1, 0, "%-*.*s", cols, cols, s);
    attroff(A_REVERSE);
    refresh();
}

/* Scrolls when the list is taller than the window. help[], if given, is shown on the status line. */
static int menu_ex(int y, const char *const *items, const int *disabled, const char *const *help, int n, int sel)
{
    int ch, i, w = ww - 6, vis = wh - 1 - y, top = 0;

    if (vis > n)
        vis = n;
    if (vis < 1)
        vis = 1;
    for (;;) {
        if (sel < top)
            top = sel;
        if (sel >= top + vis)
            top = sel - vis + 1;
        for (i = 0; i < vis; i++) {
            int k = top + i;

            if (k == sel)
                wattron(win, attr_sel());
            mvwprintw(win, y + i, 3, "%-*.*s", w, w, items[k]);
            if (k == sel)
                wattroff(win, attr_sel());
        }
        if (n > vis) {
            mvwaddch(win, y, ww - 3, top > 0 ? '^' : ' ');
            mvwaddch(win, y + vis - 1, ww - 3, top + vis < n ? 'v' : ' ');
        }
        if (help)
            status(help[sel]);
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
        } else if (ch == 'q' || ch == 27) {
            return -1;
        }
    }
}

static int menu(int y, const char *const *items, const int *disabled, int n, int sel)
{
    return menu_ex(y, items, disabled, NULL, n, sel);
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

static int screen_disks(const struct inst_disk *disks, int count)
{
    const char *items[INST_MAX_DISKS + 1];
    char lines[INST_MAX_DISKS][96];
    int disabled[INST_MAX_DISKS + 1];
    int i, usable = 0, sel;

    begin("Select a disk", " Up/Down: select   Enter: choose   q: back");
    for (i = 0; i < count; i++) {
        snprintf(lines[i], sizeof(lines[i]), "%-8s %8lu MiB   %s%s", disks[i].name, (unsigned long)disks[i].mib,
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

static void account_init(struct account *acc)
{
    memset(acc, 0, sizeof(*acc) * MKROOT_MAX_ACCOUNTS);
    snprintf(acc[0].name, sizeof(acc[0].name), "root");
    snprintf(acc[0].home, sizeof(acc[0].home), "/root");
    snprintf(acc[0].shell, sizeof(acc[0].shell), "/bin/dash");
}

/* Returns 0 when the password was set, -1 when cancelled (the old one is kept). */
static int screen_rootpw(struct account *acc)
{
    char pw[sizeof(acc[0].password)];
    int r = screen_password("Root password", "root", pw, sizeof(pw));

    if (r == 0)
        memcpy(acc[0].password, pw, sizeof(pw));
    memset(pw, 0, sizeof(pw));
    return r;
}

/* Returns 1 when acc[1] holds a user afterwards, 0 when there is none, -1 when cancelled. */
static int screen_user(struct account *acc)
{
    char name[64], pw[sizeof(acc[1].password)];

    for (;;) {
        begin("User account", " Enter: continue   Esc: back");
        text(2, "Create a regular account, or leave the name empty for none.");
        text(3, "Use a-z, 0-9, '_' and '-', starting with a letter or '_'.");
        if (input(5, "Username", name, sizeof(name), 0) < 0)
            return -1;
        if (!name[0]) {
            memset(&acc[1], 0, sizeof(acc[1]));
            return 0;
        }
        if (inst_valid_username(name))
            break;
        note("User account", "That username is not valid.", "Use 1-31 characters and not \"root\".");
    }
    if (screen_password("User account", name, pw, sizeof(pw)) < 0)
        return -1;
    memset(&acc[1], 0, sizeof(acc[1]));
    snprintf(acc[1].name, sizeof(acc[1].name), "%s", name);
    snprintf(acc[1].home, sizeof(acc[1].home), "/home/%s", name);
    snprintf(acc[1].shell, sizeof(acc[1].shell), "/bin/dash");
    acc[1].uid = acc[1].gid = 1000;
    memcpy(acc[1].password, pw, sizeof(pw));
    memset(pw, 0, sizeof(pw));
    return 1;
}

/* A list of choices; returns the chosen index, or -1 when cancelled. */
static int screen_choose(const char *title, const char *prompt, const char *const *items, int n, int cur)
{
    begin(title, " Up/Down: select   Enter: choose   q: back");
    text(2, prompt);
    return menu(4, items, NULL, n, cur < 0 ? 0 : cur);
}

static int index_of(const char *const *list, int n, const char *s)
{
    int i;

    for (i = 0; i < n; i++)
        if (!strcmp(list[i], s))
            return i;
    return -1;
}

static void screen_keyboard(struct sysconfig *cfg)
{
    int i = screen_choose("Keyboard", "Choose the keyboard layout:", inst_keymaps, inst_nkeymaps,
                          index_of(inst_keymaps, inst_nkeymaps, cfg->keymap));

    if (i >= 0)
        snprintf(cfg->keymap, sizeof(cfg->keymap), "%s", inst_keymaps[i]);
}

static void screen_locale(struct sysconfig *cfg)
{
    int i = screen_choose("Locale", "Choose the system locale:", inst_locales, inst_nlocales,
                          index_of(inst_locales, inst_nlocales, cfg->locale));

    if (i >= 0)
        snprintf(cfg->locale, sizeof(cfg->locale), "%s", inst_locales[i]);
}

static void screen_timezone(struct sysconfig *cfg)
{
    const char *items[64];
    char lines[64][48];
    int i, n = tz_count < 64 ? tz_count : 64;

    for (i = 0; i < n; i++) {
        snprintf(lines[i], sizeof(lines[i]), "%-24s %s", tz_table[i].name, tz_table[i].abbr);
        items[i] = lines[i];
    }
    i = screen_choose("Time zone", "Choose the time zone:", items, n, tz_find(cfg->timezone));
    if (i >= 0)
        snprintf(cfg->timezone, sizeof(cfg->timezone), "%s", tz_table[i].name);
}

static void screen_hostname(struct sysconfig *cfg)
{
    char name[sizeof(cfg->hostname)];

    for (;;) {
        begin("Hostname", " Enter: continue   Esc: back");
        text(2, "Enter the name of this machine.");
        text(3, "Use a-z, 0-9 and '-', up to 31 characters.");
        if (input(5, "Hostname", name, sizeof(name), 0) < 0)
            return;
        if (inst_valid_hostname(name)) {
            snprintf(cfg->hostname, sizeof(cfg->hostname), "%s", name);
            return;
        }
        note("Hostname", "That hostname is not valid.", "Use a-z, 0-9 and '-', not starting or ending with '-'.");
    }
}

static void screen_network(struct sysconfig *cfg)
{
    static const char *const items[] = { "DHCP (automatic)", "Static address" };
    struct sysconfig t = *cfg;
    char *const f[4] = { t.ip, t.mask, t.gw, t.dns };
    static const char *const label[4] = { "IP address", "Netmask", "Gateway", "DNS server" };
    int i, k = screen_choose("Network", "How should the network be set up?", items, 2, cfg->net_static);

    if (k < 0)
        return;
    if (k == 0) {
        cfg->net_static = 0;
        return;
    }
    t.net_static = 1;
    for (i = 0; i < 4;) {
        char v[16];

        begin("Static network", " Enter: continue   Esc: back");
        text(2, "Enter IPv4 addresses, for example 192.168.1.10.");
        if (input(4, label[i], v, sizeof(v), 0) < 0)
            return;
        if (!inst_valid_ipv4(v)) {
            note("Static network", "That is not a valid IPv4 address.", "Enter four numbers from 0 to 255, separated by dots.");
            continue;
        }
        snprintf(f[i], 16, "%s", v);
        i++;
    }
    *cfg = t;
}

static int parse_mib(const char *s, unsigned *out)
{
    char *e;
    unsigned long v;

    if (!*s)
        return 0;
    v = strtoul(s, &e, 10);
    if (*e || v > 0x7FFFFFFFul)
        return -1;
    *out = (unsigned)v;
    return 0;
}

static void screen_partitions(void)
{
    char b[16];
    unsigned esp = part_esp, swp = part_swap, rt = part_root;

    begin("Partitions", " Enter: next field (empty = keep)   Esc: cancel");
    text(1, "Sizes are in MiB. Leave a field empty to keep its value.");
    text(2, "The layout is: boot, swap (optional), root.");
    snprintf(b, sizeof(b), "%u", esp ? esp : INST_ESP_MIB);
    text(4, "Boot (EFI) partition size, default 100:");
    if (input(5, "Boot MiB", b, sizeof(b), 0) < 0)
        return;
    if (parse_mib(b, &esp) < 0) {
        note("Partitions", "Not a valid number.", "Nothing was changed.");
        return;
    }
    text(7, "Swap partition size, 0 for none:");
    if (input(8, "Swap MiB", b, sizeof(b), 0) < 0)
        return;
    if (parse_mib(b, &swp) < 0) {
        note("Partitions", "Not a valid number.", "Nothing was changed.");
        return;
    }
    text(10, "Root partition size, 0 for the rest of the disk:");
    if (input(11, "Root MiB", b, sizeof(b), 0) < 0)
        return;
    if (parse_mib(b, &rt) < 0) {
        note("Partitions", "Not a valid number.", "Nothing was changed.");
        return;
    }
    part_esp = esp == INST_ESP_MIB ? 0 : esp;
    part_swap = swp;
    part_root = rt;
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
    snprintf(line, sizeof(line), "Partition 1: EFI boot partition, %lu MiB", (unsigned long)(lay->esp_sectors / 2048));
    text(4, line);
    if (lay->swap_sectors) {
        snprintf(line, sizeof(line), "Partition 2: swap, %lu MiB", (unsigned long)(lay->swap_sectors / 2048));
        text(5, line);
    }
    snprintf(line, sizeof(line), "Partition %d: NirmalaSYS root, %lu MiB", lay->swap_sectors ? 3 : 2,
             (unsigned long)(lay->root_sectors / 2048));
    text(lay->swap_sectors ? 6 : 5, line);
    snprintf(line, sizeof(line), "Accounts: %s%s%s", acc[0].name, nacc > 1 ? ", " : "", nacc > 1 ? acc[1].name : "");
    text(7, line);
    wattron(win, attr_err());
    text(9, "ALL DATA ON THIS DISK WILL BE ERASED.");
    wattroff(win, attr_err());
    r = menu(12, items, NULL, 2, 0);
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
                          int nacc, const struct sysconfig *cfg)
{
    char err[160];
    static const char *const ret[] = { "Return" };
    static const char *const after[] = { "Reboot", "Power off", "Live system" };
    int r;

    begin("Installing", " Please wait. Do not power off the machine.");
    draw_progress(0, NULL);
    r = inst_run(d->name, lay, acc, (unsigned)nacc, cfg, draw_progress, NULL, err, sizeof(err));
    if (r != 0) {
        begin("Installation failed", " Enter: continue");
        wattron(win, attr_err());
        text(2, "The installation did not complete.");
        wattroff(win, attr_err());
        text(4, err);
        text(5, "No further changes were made to the running system.");
        menu(8, ret, NULL, 1, 0);
        return 1;
    }
    for (;;) {
        int c;

        begin("Finished", " Up/Down: select   Enter: choose");
        wattron(win, attr_ok());
        text(2, "Installation complete.");
        wattroff(win, attr_ok());
        text(4, "Remove the installation media, then reboot to");
        text(5, "start the new disk in UEFI mode.");
        c = menu(8, after, NULL, 3, 0);
        if (c == 0 || c == 1) {
            sync();
            inst_power(c); /* only returns on failure */
            note("Cannot continue", c ? "Powering off failed." : "Restarting failed.",
                 "Choose Live system and run reboot or poweroff.");
        } else if (c == 2) {
            return 0;
        }
    }
}

enum {
    M_KEYBOARD, M_NETWORK, M_HOSTNAME, M_LOCALE, M_TIMEZONE, M_ROOTPW, M_USER, M_DISK, M_PART, M_INSTALL, M_EXIT, M_COUNT
};

static const char *const menu_label[M_COUNT] = {
    "Keyboard", "Network", "Hostname", "Locale", "Timezone", "RootPassword", "UserAccount", "Disk", "Partitions", "Install", "Exit"
};

static const char *const menu_help[M_COUNT] = {
    " Set system keyboard layout",
    " Set up the network (DHCP or a static address)",
    " Set system hostname",
    " Set system locale",
    " Set system time zone",
    " Set the root (administrator) password - required",
    " Create a regular user account (optional)",
    " Choose the disk to install to - required. ALL DATA ON IT WILL BE ERASED",
    " Set partition sizes and an optional swap partition (default: automatic)",
    " Start installation with the saved settings",
    " Exit the installer",
};

int ui_curses_run(void)
{
    struct sysconfig cfg;
    struct account acc[MKROOT_MAX_ACCOUNTS];
    struct inst_disk disk;
    struct inst_layout lay;
    SCREEN *scr;
    int rc = 1, sel = 0, have_disk = 0, have_user = 0, done = 0;

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

    inst_config_defaults(&cfg);
    account_init(acc);

    while (!done) {
        char lines[M_COUNT][80], err[128];
        const char *items[M_COUNT];
        int i, root_set = acc[0].password[0] != 0;

        for (i = 0; i < M_COUNT; i++)
            items[i] = lines[i];
        for (i = 0; i < M_COUNT; i++)
            snprintf(lines[i], sizeof(lines[i]), "%-13s", menu_label[i]);
        snprintf(lines[M_KEYBOARD] + 13, 67, "%s", cfg.keymap);
        if (cfg.net_static)
            snprintf(lines[M_NETWORK] + 13, 67, "static %s", cfg.ip);
        else
            snprintf(lines[M_NETWORK] + 13, 67, "DHCP");
        snprintf(lines[M_HOSTNAME] + 13, 67, "%s", cfg.hostname);
        snprintf(lines[M_LOCALE] + 13, 67, "%s", cfg.locale);
        snprintf(lines[M_TIMEZONE] + 13, 67, "%s", cfg.timezone);
        snprintf(lines[M_ROOTPW] + 13, 67, "%s", root_set ? "set" : "NOT SET (required)");
        snprintf(lines[M_USER] + 13, 67, "%s", have_user ? acc[1].name : "none");
        if (have_disk)
            snprintf(lines[M_DISK] + 13, 67, "%s  %lu MiB  %s", disk.name, (unsigned long)disk.mib, disk.model);
        else
            snprintf(lines[M_DISK] + 13, 67, "NOT SELECTED (required)");
        if (!part_esp && !part_swap && !part_root)
            snprintf(lines[M_PART] + 13, 67, "automatic (no swap)");
        else
            snprintf(lines[M_PART] + 13, 67, "boot %u, swap %u, root %u MiB (0 = default/rest)",
                     part_esp ? part_esp : INST_ESP_MIB, part_swap, part_root);
        snprintf(lines[M_INSTALL] + 13, 67, "Start installation");
        snprintf(lines[M_EXIT] + 13, 67, "Exit the installer");

        begin("NirmalaSYS installation menu", " Up/Down: select   Enter: open");
        text(1, "Set the items in any order, then choose Install.");
        i = menu_ex(3, items, NULL, menu_help, M_COUNT, sel);
        if (i < 0)
            continue;
        sel = i;

        switch (i) {
        case M_KEYBOARD:
            screen_keyboard(&cfg);
            break;
        case M_NETWORK:
            screen_network(&cfg);
            break;
        case M_HOSTNAME:
            screen_hostname(&cfg);
            break;
        case M_LOCALE:
            screen_locale(&cfg);
            break;
        case M_TIMEZONE:
            screen_timezone(&cfg);
            break;
        case M_ROOTPW:
            screen_rootpw(acc);
            break;
        case M_USER: {
            int u = screen_user(acc);

            if (u >= 0)
                have_user = u;
            break;
        }
        case M_DISK: {
            struct inst_disk disks[INST_MAX_DISKS];
            int pick = screen_disks(disks, inst_list(disks, INST_MAX_DISKS));

            if (pick >= 0) {
                disk = disks[pick];
                have_disk = 1;
            }
            break;
        }
        case M_PART:
            screen_partitions();
            break;
        case M_INSTALL:
            if (!root_set) {
                note("Cannot install", "The root password is not set.", "Open RootPassword and set one first.");
                sel = M_ROOTPW;
            } else if (!have_disk) {
                note("Cannot install", "No disk has been selected.", "Open Disk and choose where to install.");
                sel = M_DISK;
            } else if (inst_config_check(&cfg, err, sizeof(err)) < 0) {
                note("Cannot install", err, "Fix the setting and try again.");
            } else if (inst_plan_ex(disk.name, part_esp, part_swap, part_root, &lay, err, sizeof(err)) < 0) {
                note("Cannot install", err, "Change the disk or the partition sizes.");
                sel = M_PART;
            } else if (screen_confirm(&disk, &lay, acc, have_user ? 2 : 1)) {
                rc = screen_install(&disk, &lay, acc, have_user ? 2 : 1, &cfg);
                if (rc == 0)
                    done = 1;
            }
            break;
        case M_EXIT:
            rc = 1;
            done = 1;
            break;
        }
    }
    memset(acc, 0, sizeof(acc));
    if (win)
        delwin(win);
    bkgd(A_NORMAL);
    clear();
    refresh();
    curs_set(1);
    endwin();
    return rc;
}
