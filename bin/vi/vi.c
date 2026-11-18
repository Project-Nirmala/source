#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

#define MAXFILE (96 * 1024)
#define YANKMAX 8192
#define ESC     0x1B

enum { K_UP = 1000, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END, K_DEL };
enum { M_NORMAL, M_INSERT };

static char  store_a[MAXFILE + 2], store_b[MAXFILE + 2];
static char *buf = store_a, *undo = store_b;
static size_t len, undo_len, cur, undo_cur, top;
static char  yank[YANKMAX];
static size_t yank_len;
static int    yank_lines;
static char   fname[256], msg[160], lastsearch[80];
static int    searchdir = 1, mode = M_NORMAL, modified, rows = 24, cols = 80, want_col = -1;
static int    pending_key = -1, quit;
static struct termios orig;
static char   ob[16384];
static size_t oblen;

static void ob_add(const char *s, size_t n)
{
    if (oblen + n > sizeof(ob))
        n = sizeof(ob) - oblen;
    memcpy(ob + oblen, s, n);
    oblen += n;
}

static void ob_str(const char *s)
{
    ob_add(s, strlen(s));
}

static void ob_flush(void)
{
    size_t off = 0;

    while (off < oblen) {
        long n = write(1, ob + off, oblen - off);

        if (n <= 0)
            break;
        off += (size_t)n;
    }
    oblen = 0;
}

static void term_raw(void)
{
    struct termios raw;
    struct winsize ws;

    tcgetattr(0, &orig);
    raw = orig;
    raw.c_lflag &= ~(unsigned)(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 2 && ws.ws_col > 10) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
    ob_str("\x1b[?1049h");
    ob_flush();
}

static void term_restore(void)
{
    ob_str("\x1b[?1049l");
    ob_flush();
    tcsetattr(0, TCSANOW, &orig);
}

static int read_byte(int wait)
{
    unsigned char c;
    struct termios t;
    long n;

    if (wait)
        return read(0, &c, 1) == 1 ? c : -1;
    tcgetattr(0, &t);
    t.c_cc[VMIN] = 0;
    tcsetattr(0, TCSANOW, &t);
    n = read(0, &c, 1);
    t.c_cc[VMIN] = 1;
    tcsetattr(0, TCSANOW, &t);
    return n == 1 ? c : -1;
}

static int read_key(void)
{
    int c, d, e;

    if (pending_key >= 0) {
        c = pending_key;
        pending_key = -1;
        return c;
    }
    c = read_byte(1);
    if (c < 0)
        return 'q' & 0x1F;
    if (c != ESC)
        return c;
    d = read_byte(0);
    if (d < 0) {
        usleep(10000);
        d = read_byte(0);
    }
    if (d < 0)
        return ESC;
    if (d != '[' && d != 'O') {
        pending_key = d;
        return ESC;
    }
    e = read_byte(1);
    switch (e) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    }
    if (e >= '0' && e <= '9') {
        int f = read_byte(1);

        if (f == '~') {
            if (e == '3')
                return K_DEL;
            if (e == '1' || e == '7')
                return K_HOME;
            if (e == '4' || e == '8')
                return K_END;
        }
    }
    return ESC;
}

static size_t line_start(size_t p)
{
    while (p > 0 && buf[p - 1] != '\n')
        p--;
    return p;
}

static size_t line_end(size_t p)
{
    while (p < len && buf[p] != '\n')
        p++;
    return p;
}

static size_t next_line(size_t p)
{
    p = line_end(p);
    return p < len ? p + 1 : len;
}

static size_t prev_line(size_t p)
{
    p = line_start(p);
    return p == 0 ? 0 : line_start(p - 1);
}

static int cls(char c)
{
    if (c == ' ' || c == '\t' || c == '\n')
        return 0;
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ? 1 : 2;
}

static size_t word_fwd(size_t p)
{
    int c;

    if (p >= len)
        return len;
    c = cls(buf[p]);
    if (c)
        while (p < len && cls(buf[p]) == c)
            p++;
    while (p < len && cls(buf[p]) == 0)
        p++;
    return p;
}

static size_t word_back(size_t p)
{
    int c;

    if (p == 0)
        return 0;
    p--;
    while (p > 0 && cls(buf[p]) == 0)
        p--;
    c = cls(buf[p]);
    while (p > 0 && cls(buf[p - 1]) == c)
        p--;
    return p;
}

static void set_msg(const char *s)
{
    strncpy(msg, s, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
}

static void fix_cursor(void)
{
    if (len == 0) {
        cur = 0;
        return;
    }
    if (mode == M_NORMAL) {
        if (cur >= len)
            cur = len - 1;
        if (buf[cur] == '\n' && cur > line_start(cur))
            cur--;
    } else if (cur > len) {
        cur = len;
    }
}

static void save_undo(void)
{
    memcpy(undo, buf, len);
    undo_len = len;
    undo_cur = cur;
}

static int insert_at(size_t pos, const char *s, size_t n)
{
    if (len + n > MAXFILE) {
        set_msg("Buffer full");
        return -1;
    }
    memmove(buf + pos + n, buf + pos, len - pos);
    memcpy(buf + pos, s, n);
    len += n;
    modified = 1;
    return 0;
}

static void delete_range(size_t a, size_t b)
{
    if (b > len)
        b = len;
    if (a >= b)
        return;
    memmove(buf + a, buf + b, len - b);
    len -= b - a;
    modified = 1;
}

static void ensure_trailing_nl(void)
{
    if (len > 0 && buf[len - 1] != '\n' && len < MAXFILE) {
        buf[len++] = '\n';
    }
}

static void set_yank(const char *s, size_t n, int lines)
{
    if (n > YANKMAX)
        n = YANKMAX;
    memcpy(yank, s, n);
    yank_len = n;
    yank_lines = lines;
}

static int load_file(const char *name)
{
    int fd = open(name, O_RDONLY);
    long n;

    len = 0;
    if (fd < 0) {
        if (errno == ENOENT) {
            set_msg("[New file]");
            return 0;
        }
        snprintf(msg, sizeof(msg), "\"%s\": %s", name, strerror(errno));
        return -1;
    }
    while ((n = read(fd, buf + len, MAXFILE - len)) > 0)
        len += (size_t)n;
    close(fd);
    if (n < 0) {
        snprintf(msg, sizeof(msg), "\"%s\": %s", name, strerror(errno));
        len = 0;
        return -1;
    }
    if (len == MAXFILE) {
        char probe;

        fd = open(name, O_RDONLY);
        if (fd >= 0) {
            lseek(fd, (long)MAXFILE, 0);
            if (read(fd, &probe, 1) == 1) {
                set_msg("File too large for this editor");
                close(fd);
                len = 0;
                return -1;
            }
            close(fd);
        }
    }
    ensure_trailing_nl();
    {
        size_t i, lines = 0;

        for (i = 0; i < len; i++)
            if (buf[i] == '\n')
                lines++;
        snprintf(msg, sizeof(msg), "\"%s\" %luL, %luC", name, (unsigned long)lines, (unsigned long)len);
    }
    return 0;
}

static int save_file(const char *name)
{
    int fd;
    size_t off = 0, lines = 0, i;

    ensure_trailing_nl();
    fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(msg, sizeof(msg), "\"%s\": %s", name, strerror(errno));
        return -1;
    }
    while (off < len) {
        long n = write(fd, buf + off, len - off);

        if (n <= 0) {
            snprintf(msg, sizeof(msg), "write error: %s", strerror(errno));
            close(fd);
            return -1;
        }
        off += (size_t)n;
    }
    close(fd);
    for (i = 0; i < len; i++)
        if (buf[i] == '\n')
            lines++;
    snprintf(msg, sizeof(msg), "\"%s\" %luL, %luC written", name, (unsigned long)lines, (unsigned long)len);
    modified = 0;
    return 0;
}

static int screen_col(size_t ls, size_t p)
{
    int col = 0;

    for (; ls < p; ls++)
        col += buf[ls] == '\t' ? 8 - col % 8 : 1;
    return col;
}

static void render(void)
{
    size_t p, ls = line_start(cur);
    int r, curcol, linenum = 1, shown;
    char status[256], right[48];
    size_t i;

    while (cur < top)
        top = prev_line(top);
    shown = 0;
    for (p = top; p < ls; p = next_line(p))
        shown++;
    while (shown >= rows - 1) {
        top = next_line(top);
        shown--;
    }

    oblen = 0;
    ob_str("\x1b[?25l");
    p = top;
    for (r = 0; r < rows - 1; r++) {
        char pos[16];

        snprintf(pos, sizeof(pos), "\x1b[%d;1H", r + 1);
        ob_str(pos);
        if (p > len || (p == len && !(len == 0 || cur == len))) {
            ob_str("~");
        } else {
            size_t e = line_end(p), q;
            int col = 0;

            for (q = p; q < e && col < cols; q++) {
                if (buf[q] == '\t') {
                    do {
                        ob_str(" ");
                        col++;
                    } while (col % 8 && col < cols);
                } else {
                    ob_add(&buf[q], 1);
                    col++;
                }
            }
            p = e < len ? e + 1 : len + 1;
            ob_str("\x1b[K");
            continue;
        }
        p = len + 2;
        ob_str("\x1b[K");
    }

    for (i = 0; i < cur && i < len; i++)
        if (buf[i] == '\n')
            linenum++;
    curcol = screen_col(ls, cur);
    snprintf(right, sizeof(right), "%d,%d", linenum, curcol + 1);
    if (msg[0])
        snprintf(status, sizeof(status), "%s", msg);
    else if (mode == M_INSERT)
        snprintf(status, sizeof(status), "-- INSERT --");
    else
        snprintf(status, sizeof(status), "%s%s", fname[0] ? fname : "[No Name]", modified ? " [+]" : "");
    {
        char pos[16];
        int w = (int)strlen(status), rw = (int)strlen(right), k;

        snprintf(pos, sizeof(pos), "\x1b[%d;1H", rows);
        ob_str(pos);
        ob_str("\x1b[7m");
        if (w > cols - rw - 1)
            w = cols - rw - 1;
        ob_add(status, (size_t)w);
        for (k = w; k < cols - rw; k++)
            ob_str(" ");
        ob_str(right);
        ob_str("\x1b[0m");
        snprintf(pos, sizeof(pos), "\x1b[%d;%dH", shown + 1, (curcol < cols ? curcol : cols - 1) + 1);
        ob_str(pos);
    }
    ob_str("\x1b[?25h");
    ob_flush();
}

static int prompt(const char *pre, char *out, size_t max)
{
    size_t n = 0;

    for (;;) {
        char line[200], pos[16];
        int k;

        snprintf(line, sizeof(line), "%s%.*s", pre, (int)n, out);
        oblen = 0;
        snprintf(pos, sizeof(pos), "\x1b[%d;1H", rows);
        ob_str(pos);
        ob_add(line, strlen(line) < (size_t)cols - 1 ? strlen(line) : (size_t)cols - 1);
        ob_str("\x1b[K");
        ob_flush();
        k = read_key();
        if (k == ESC || k == 3) {
            out[0] = 0;
            return -1;
        }
        if (k == '\n' || k == '\r') {
            out[n] = 0;
            return 0;
        }
        if (k == 0x7F || k == 8) {
            if (n == 0) {
                out[0] = 0;
                return -1;
            }
            n--;
        } else if (k >= 0x20 && k < 0x7F && n + 1 < max) {
            out[n++] = (char)k;
        }
    }
}

static int find_text(const char *pat, int dir, size_t *at)
{
    size_t pl = strlen(pat), i;

    if (!pl || len < pl)
        return 0;
    if (dir > 0) {
        for (i = cur + 1; i + pl <= len; i++)
            if (!memcmp(buf + i, pat, pl)) {
                *at = i;
                return 1;
            }
        for (i = 0; i <= cur && i + pl <= len; i++)
            if (!memcmp(buf + i, pat, pl)) {
                *at = i;
                set_msg("search hit BOTTOM, continuing at TOP");
                return 1;
            }
    } else {
        for (i = cur; i > 0; i--)
            if (i - 1 + pl <= len && !memcmp(buf + i - 1, pat, pl)) {
                *at = i - 1;
                return 1;
            }
        for (i = len >= pl ? len - pl + 1 : 0; i > cur; i--)
            if (!memcmp(buf + i - 1, pat, pl)) {
                *at = i - 1;
                set_msg("search hit TOP, continuing at BOTTOM");
                return 1;
            }
    }
    return 0;
}

static void do_search(int dir)
{
    size_t at;

    if (!lastsearch[0])
        return;
    searchdir = dir;
    if (find_text(lastsearch, dir, &at)) {
        cur = at;
    } else {
        snprintf(msg, sizeof(msg), "Pattern not found: %s", lastsearch);
    }
}

static void ex_command(char *cmd)
{
    char *arg;
    int bang = 0;

    while (*cmd == ' ' || *cmd == ':')
        cmd++;
    if (!*cmd)
        return;
    if (*cmd >= '0' && *cmd <= '9') {
        int n = atoi(cmd), l = 1;
        size_t p = 0;

        while (l < n && p < len) {
            p = next_line(p);
            l++;
        }
        cur = p < len ? p : (len ? line_start(len - 1) : 0);
        return;
    }
    arg = cmd;
    while (*arg && *arg != ' ' && *arg != '!')
        arg++;
    if (*arg == '!') {
        bang = 1;
        *arg++ = 0;
    } else if (*arg) {
        *arg++ = 0;
    }
    while (*arg == ' ')
        arg++;

    if (!strcmp(cmd, "w") || !strcmp(cmd, "write")) {
        const char *name = *arg ? arg : fname;

        if (!*name)
            set_msg("No file name");
        else if (save_file(name) == 0 && !fname[0])
            strncpy(fname, name, sizeof(fname) - 1);
    } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
        if (modified && !bang)
            set_msg("No write since last change (add ! to override)");
        else
            quit = 1;
    } else if (!strcmp(cmd, "wq") || !strcmp(cmd, "x") || !strcmp(cmd, "xit")) {
        const char *name = *arg ? arg : fname;

        if (!*name)
            set_msg("No file name");
        else if (save_file(name) == 0)
            quit = 1;
    } else if (!strcmp(cmd, "e") && bang) {
        if (fname[0]) {
            load_file(fname);
            modified = 0;
            cur = top = 0;
        }
    } else if (!strcmp(cmd, "r") || !strcmp(cmd, "read")) {
        int fd = *arg ? open(arg, O_RDONLY) : -1;
        static char tmp[MAXFILE];
        long n, total = 0;

        if (fd < 0) {
            snprintf(msg, sizeof(msg), "Cannot read \"%s\"", arg);
            return;
        }
        while ((n = read(fd, tmp + total, sizeof(tmp) - (size_t)total)) > 0)
            total += n;
        close(fd);
        if (total > 0) {
            save_undo();
            if (tmp[total - 1] != '\n' && (size_t)total < sizeof(tmp))
                tmp[total++] = '\n';
            insert_at(next_line(cur), tmp, (size_t)total);
            cur = next_line(cur);
        }
    } else if (!strcmp(cmd, "set")) {
    } else {
        snprintf(msg, sizeof(msg), "Not an editor command: %s", cmd);
    }
}

static int motion(int key, int n, size_t *target, int *linewise, int *inclusive)
{
    size_t p = cur;
    int i;

    *linewise = 0;
    *inclusive = 0;
    switch (key) {
    case 'h':
    case K_LEFT:
        for (i = 0; i < n && p > line_start(p); i++)
            p--;
        break;
    case 'l':
    case K_RIGHT:
        for (i = 0; i < n && p < line_end(p); i++)
            p++;
        break;
    case 'w':
        for (i = 0; i < n; i++)
            p = word_fwd(p);
        break;
    case 'b':
        for (i = 0; i < n; i++)
            p = word_back(p);
        break;
    case '0':
    case K_HOME:
        p = line_start(p);
        break;
    case '^': {
        size_t e = line_end(p);

        p = line_start(p);
        while (p < e && (buf[p] == ' ' || buf[p] == '\t'))
            p++;
        break;
    }
    case '$':
    case K_END:
        for (i = 1; i < n; i++)
            p = next_line(p);
        p = line_end(p);
        *inclusive = 1;
        break;
    case 'j':
    case K_DOWN:
        *linewise = 1;
        for (i = 0; i < n && next_line(p) < len; i++)
            p = next_line(p);
        break;
    case 'k':
    case K_UP:
        *linewise = 1;
        for (i = 0; i < n && line_start(p) > 0; i++)
            p = prev_line(p);
        break;
    case 'G': {
        size_t q = 0;

        *linewise = 1;
        if (n > 0 && n != 1000000) {
            int l;

            for (l = 1; l < n && next_line(q) < len; l++)
                q = next_line(q);
            p = q;
        } else {
            p = len ? line_start(len - 1) : 0;
        }
        break;
    }
    default:
        return 0;
    }
    *target = p;
    return 1;
}

static void move_cursor(int key, int count)
{
    size_t t, ls;
    int lw, inc, col;

    if (key == 'j' || key == K_DOWN || key == 'k' || key == K_UP) {
        if (want_col < 0)
            want_col = (int)(cur - line_start(cur));
        if (motion(key, count ? count : 1, &t, &lw, &inc)) {
            size_t e = line_end(t);

            ls = line_start(t);
            col = want_col;
            cur = ls + (size_t)col <= e ? ls + (size_t)col : e;
        }
        return;
    }
    want_col = -1;
    if (key == 'G' && !count)
        count = 1000000;
    if (motion(key, count ? count : 1, &t, &lw, &inc))
        cur = t;
}

static void operator_cmd(int op, int count)
{
    int key = read_key(), n2 = 0, lw, inc, count2;
    size_t t, a, b;
    int linewise = 0;

    while (key >= '1' && key <= '9') {
        n2 = n2 * 10 + (key - '0');
        key = read_key();
        while (key >= '0' && key <= '9') {
            n2 = n2 * 10 + (key - '0');
            key = read_key();
        }
    }
    count2 = (count ? count : 1) * (n2 ? n2 : 1);
    if (key == 'G' && !count && !n2)
        count2 = 1000000;

    if (key == op) {
        int i;

        a = line_start(cur);
        b = a;
        for (i = 0; i < count2 && b < len; i++)
            b = next_line(b);
        linewise = 1;
    } else if (key == 'g') {
        if (read_key() != 'g')
            return;
        a = 0;
        b = next_line(cur);
        linewise = 1;
        if (cur > a)
            a = 0;
    } else {
        int kk = key;

        if (!motion(kk, count2, &t, &lw, &inc))
            return;
        if (op == 'c' && key == 'w' && len && cls(buf[cur]) != 0) {
            int w;

            t = cur;
            for (w = 0; w < count2 && t < len; w++) {
                int c;

                while (w > 0 && t < len && cls(buf[t]) == 0)
                    t++;
                c = cls(buf[t]);
                while (t < len && cls(buf[t]) == c && c != 0)
                    t++;
            }
        }
        if (lw) {
            size_t x = cur < t ? cur : t, y = cur < t ? t : cur;

            a = line_start(x);
            b = next_line(y);
            linewise = 1;
        } else {
            if (key == 'w' && t > line_end(cur))
                t = line_end(cur);
            a = cur < t ? cur : t;
            b = cur < t ? t : cur;
        }
    }
    if (b > len)
        b = len;
    if (a >= b && !(op == 'c' && linewise))
        return;
    if (op == 'y') {
        char m[40];

        set_yank(buf + a, b - a, linewise);
        if (linewise) {
            snprintf(m, sizeof(m), "%lu lines yanked", (unsigned long)count2);
            set_msg(m);
        }
        return;
    }
    save_undo();
    set_yank(buf + a, b - a, linewise);
    if (op == 'c' && linewise) {
        size_t e = b > a && buf[b - 1] == '\n' ? b - 1 : b;

        delete_range(a, e);
        cur = a;
    } else {
        delete_range(a, b);
        cur = a;
    }
    if (op == 'c') {
        mode = M_INSERT;
        msg[0] = 0;
    }
}

static void put_cmd(int after, int count)
{
    int i;

    if (!yank_len) {
        set_msg("Nothing in register");
        return;
    }
    save_undo();
    if (yank_lines) {
        size_t pos = after ? next_line(cur) : line_start(cur);

        if (after && pos == len && len > 0 && buf[len - 1] != '\n')
            insert_at(len, "\n", 1);
        for (i = 0; i < (count ? count : 1); i++)
            if (insert_at(pos, yank, yank_len) < 0)
                break;
        cur = pos;
    } else {
        size_t pos = after && cur < line_end(cur) ? cur + 1 : cur;

        for (i = 0; i < (count ? count : 1); i++)
            if (insert_at(pos, yank, yank_len) < 0)
                break;
        cur = pos + yank_len * (size_t)(count ? count : 1) - 1;
    }
}

static void insert_char(char c)
{
    if (c == '\n') {
        size_t ls = line_start(cur), i = ls;
        char indent[64];
        size_t n = 0;

        while (i < cur && (buf[i] == ' ' || buf[i] == '\t') && n < sizeof(indent) - 1)
            indent[n++] = buf[i++];
        if (insert_at(cur, "\n", 1) < 0)
            return;
        cur++;
        if (n && insert_at(cur, indent, n) == 0)
            cur += n;
        return;
    }
    if (insert_at(cur, &c, 1) == 0)
        cur++;
}

static void insert_key(int k)
{
    switch (k) {
    case ESC:
    case 3:
        mode = M_NORMAL;
        ensure_trailing_nl();
        if (cur > line_start(cur))
            cur--;
        fix_cursor();
        break;
    case '\r':
    case '\n':
        insert_char('\n');
        break;
    case 0x7F:
    case 8:
        if (cur > 0) {
            delete_range(cur - 1, cur);
            cur--;
        }
        break;
    case K_DEL:
        if (cur < len)
            delete_range(cur, cur + 1);
        break;
    case K_LEFT:
        if (cur > line_start(cur))
            cur--;
        break;
    case K_RIGHT:
        if (cur < line_end(cur))
            cur++;
        break;
    case K_UP:
    case K_DOWN: {
        size_t col = cur - line_start(cur), t = cur, e;

        if (k == K_UP && line_start(cur) > 0)
            t = prev_line(cur);
        else if (k == K_DOWN && next_line(cur) < len)
            t = next_line(cur);
        else
            break;
        e = line_end(t);
        cur = line_start(t) + col <= e ? line_start(t) + col : e;
        break;
    }
    case K_HOME:
        cur = line_start(cur);
        break;
    case K_END:
        cur = line_end(cur);
        break;
    default:
        if (k == '\t' || (k >= 0x20 && k < 0x7F))
            insert_char((char)k);
        break;
    }
}

static void enter_insert(void)
{
    mode = M_INSERT;
    msg[0] = 0;
}

static void normal_key(int k)
{
    int count = 0, i;

    msg[0] = (k == ':' || k == '/' || k == '?') ? 0 : msg[0];
    while ((k >= '1' && k <= '9') || (count && k >= '0' && k <= '9')) {
        count = count * 10 + (k - '0');
        k = read_key();
    }
    switch (k) {
    case 'i':
        save_undo();
        enter_insert();
        break;
    case 'a':
        save_undo();
        if (cur < line_end(cur))
            cur++;
        enter_insert();
        break;
    case 'I':
        save_undo();
        move_cursor('^', 0);
        enter_insert();
        break;
    case 'A':
        save_undo();
        cur = line_end(cur);
        enter_insert();
        break;
    case 'o':
        save_undo();
        cur = line_end(cur);
        insert_char('\n');
        enter_insert();
        break;
    case 'O': {
        size_t ls = line_start(cur);

        save_undo();
        cur = ls;
        if (insert_at(ls, "\n", 1) == 0)
            enter_insert();
        break;
    }
    case 'x':
    case K_DEL: {
        size_t e = line_end(cur), n = (size_t)(count ? count : 1);

        if (cur < e) {
            save_undo();
            if (cur + n > e)
                n = e - cur;
            set_yank(buf + cur, n, 0);
            delete_range(cur, cur + n);
        }
        break;
    }
    case 'X':
        if (cur > line_start(cur)) {
            save_undo();
            delete_range(cur - 1, cur);
            cur--;
        }
        break;
    case 's':
        if (cur < line_end(cur)) {
            save_undo();
            delete_range(cur, cur + 1);
        } else {
            save_undo();
        }
        enter_insert();
        break;
    case 'r': {
        int c = read_key();

        if (c >= 0x20 && c < 0x7F && cur < line_end(cur)) {
            save_undo();
            buf[cur] = (char)c;
            modified = 1;
        }
        break;
    }
    case '~':
        if (cur < line_end(cur)) {
            char c = buf[cur];

            save_undo();
            if (c >= 'a' && c <= 'z')
                buf[cur] = (char)(c - 32);
            else if (c >= 'A' && c <= 'Z')
                buf[cur] = (char)(c + 32);
            modified = 1;
            if (cur + 1 < line_end(cur))
                cur++;
        }
        break;
    case 'd':
    case 'c':
    case 'y':
        operator_cmd(k, count);
        break;
    case 'D':
        save_undo();
        set_yank(buf + cur, line_end(cur) - cur, 0);
        delete_range(cur, line_end(cur));
        break;
    case 'C':
        save_undo();
        set_yank(buf + cur, line_end(cur) - cur, 0);
        delete_range(cur, line_end(cur));
        enter_insert();
        break;
    case 'Y': {
        size_t a = line_start(cur), b = next_line(cur);

        set_yank(buf + a, b - a, 1);
        break;
    }
    case 'p':
        put_cmd(1, count);
        break;
    case 'P':
        put_cmd(0, count);
        break;
    case 'J': {
        size_t e = line_end(cur);

        if (e < len - 1 || (e < len && next_line(cur) < len)) {
            size_t q = e + 1;

            save_undo();
            while (q < len && (buf[q] == ' ' || buf[q] == '\t'))
                q++;
            delete_range(e, q);
            insert_at(e, " ", 1);
            cur = e;
        }
        break;
    }
    case 'u':
        if (undo_len || modified) {
            char *t = buf;
            size_t tl = len, tc = cur;

            buf = undo;
            undo = t;
            len = undo_len;
            undo_len = tl;
            cur = undo_cur;
            undo_cur = tc;
            modified = 1;
        } else {
            set_msg("Already at oldest change");
        }
        break;
    case 'g': {
        int c = read_key();

        if (c == 'g') {
            cur = 0;
            if (count) {
                for (i = 1; i < count && next_line(cur) < len; i++)
                    cur = next_line(cur);
            }
        }
        break;
    }
    case 'Z': {
        int c = read_key();

        if (c == 'Z') {
            if (!fname[0])
                set_msg("No file name");
            else if (save_file(fname) == 0)
                quit = 1;
        } else if (c == 'Q') {
            quit = 1;
        }
        break;
    }
    case ':': {
        char line[160];

        if (prompt(":", line, sizeof(line)) == 0)
            ex_command(line);
        break;
    }
    case '/':
    case '?': {
        char line[80];

        if (prompt(k == '/' ? "/" : "?", line, sizeof(line)) == 0 && line[0]) {
            strncpy(lastsearch, line, sizeof(lastsearch) - 1);
            do_search(k == '/' ? 1 : -1);
        }
        break;
    }
    case 'n':
        do_search(searchdir);
        break;
    case 'N':
        do_search(-searchdir);
        break;
    case 6:
    case 2:
        for (i = 0; i < rows - 2; i++)
            move_cursor(k == 6 ? 'j' : 'k', 1);
        break;
    case 7:
        snprintf(msg, sizeof(msg), "\"%s\"%s %lu bytes", fname[0] ? fname : "[No Name]",
                 modified ? " [Modified]" : "", (unsigned long)len);
        break;
    case 12:
        break;
    default:
        move_cursor(k, count);
        break;
    }
    if (k != 'j' && k != 'k' && k != K_UP && k != K_DOWN)
        want_col = -1;
    fix_cursor();
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        dprintf(2, "usage: vi [file]\n");
        return 1;
    }
    if (argc == 2) {
        strncpy(fname, argv[1], sizeof(fname) - 1);
        load_file(fname);
    }
    term_raw();
    while (!quit) {
        int k, avail;

        fix_cursor();
        render();
        do {
            k = read_key();
            if (mode == M_INSERT)
                insert_key(k);
            else
                normal_key(k);
            if (mode == M_INSERT && msg[0] && k != ESC)
                msg[0] = 0;
            avail = 0;
            ioctl(0, FIONREAD, &avail);
        } while (!quit && avail > 0);
    }
    term_restore();
    return 0;
}
