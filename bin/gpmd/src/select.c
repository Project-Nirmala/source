#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "gpmd.h"

static int is_word(char c)
{
    return c && (isalnum((unsigned char)c) || strchr("_-./~:@%+=", c) != NULL);
}

static void range(const struct selection *s, int *lo, int *hi)
{
    int a = s->anchor < s->cursor ? s->anchor : s->cursor;
    int b = s->anchor < s->cursor ? s->cursor : s->anchor;

    if (s->mode == SEL_LINE) {
        a -= a % s->cols;
        b = b - b % s->cols + s->cols - 1;
    } else if (s->mode == SEL_WORD) {
        if (is_word(s->screen[a]))
            while (a % s->cols && is_word(s->screen[a - 1]))
                a--;
        if (is_word(s->screen[b]))
            while ((b + 1) % s->cols && is_word(s->screen[b + 1]))
                b++;
    }
    *lo = a;
    *hi = b;
}

static void apply(const struct selection *s)
{
    int lo, hi;

    range(s, &lo, &hi);
    console_highlight(lo, hi);
}

static void copy_range(struct selection *s, int lo, int hi)
{
    int first = lo / s->cols, last = hi / s->cols, row;
    size_t cap = (size_t)(hi - lo + 1) + (size_t)(last - first) + 1, n = 0;
    char *out = malloc(cap);

    if (!out)
        return;
    for (row = first; row <= last; row++) {
        int c0 = row == first ? lo % s->cols : 0;
        int c1 = row == last ? hi % s->cols : s->cols - 1;
        const char *line = s->screen + (size_t)row * s->cols;
        size_t start = n;
        int c;

        for (c = c0; c <= c1; c++)
            out[n++] = line[c];
        while (n > start && out[n - 1] == ' ')
            n--;
        if (row < last)
            out[n++] = '\n';
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\n'))
        n--;
    if (!n) {
        free(out);
        return;
    }
    free(s->clip);
    s->clip = out;
    s->clip_len = n;
}

static int refresh(struct selection *s)
{
    return console_read_screen(s->screen, s->cols * s->rows);
}

int select_init(struct selection *s, int cols, int rows)
{
    memset(s, 0, sizeof(*s));
    s->cols = cols;
    s->rows = rows;
    s->last_cell = -1;
    s->screen = calloc((size_t)cols * rows, 1);
    return s->screen ? 0 : -1;
}

void select_begin(struct selection *s, int cell, long now_ms)
{
    if (refresh(s) < 0)
        return;
    if (s->last_cell == cell && now_ms - s->last_ms < CLICK_MS && s->clicks < 3)
        s->clicks++;
    else
        s->clicks = 1;
    s->last_cell = cell;
    s->last_ms = now_ms;
    s->mode = s->clicks == 1 ? SEL_CHAR : s->clicks == 2 ? SEL_WORD : SEL_LINE;
    s->anchor = cell;
    s->cursor = cell;
    s->dragging = 1;
    s->active = 1;
    apply(s);
}

void select_extend(struct selection *s, int cell)
{
    int lo, hi;

    if (!s->active || refresh(s) < 0)
        return;
    range(s, &lo, &hi);
    s->anchor = (cell - lo) < (hi - cell) ? hi : lo;
    s->cursor = cell;
    s->dragging = 1;
    apply(s);
}

void select_drag(struct selection *s, int cell)
{
    if (!s->dragging || cell == s->cursor)
        return;
    s->cursor = cell;
    apply(s);
}

void select_end(struct selection *s)
{
    int lo, hi;

    if (!s->dragging)
        return;
    s->dragging = 0;
    range(s, &lo, &hi);
    if (s->mode == SEL_CHAR && s->clicks == 1 && lo == hi) {
        console_highlight(-1, -1);
        s->active = 0;
        return;
    }
    copy_range(s, lo, hi);
}

void select_paste(const struct selection *s)
{
    if (s->clip_len)
        console_paste(s->clip, s->clip_len);
}
