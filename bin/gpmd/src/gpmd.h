#ifndef GPMD_H
#define GPMD_H

#include <stddef.h>
#include <sys/mouse.h>

#define DEFAULT_SENSITIVITY 1
#define CLICK_MS            400

enum { SEL_CHAR, SEL_WORD, SEL_LINE };

struct pointer {
    int x, y;
    int width, height;
    int cellw, cellh;
    int cols;
    int gain;
    int col, row;
};

struct selection {
    int    cols, rows;
    int    anchor, cursor;
    int    mode;
    int    active;
    int    dragging;
    int    clicks;
    int    last_cell;
    long   last_ms;
    char  *screen;
    char  *clip;
    size_t clip_len;
};

int  mouse_open(const char *path);
int  mouse_next(struct mouse_event *ev);
int  mouse_absolute(void);
int  mouse_next_abs(struct mouse_event *ev, unsigned *ax, unsigned *ay, int *have_abs);

void pointer_init(struct pointer *p, int width, int height, int cellw, int cellh, int gain);
int  pointer_move(struct pointer *p, int dx, int dy);
int  pointer_place(struct pointer *p, unsigned ax, unsigned ay);
int  pointer_cell(const struct pointer *p);

int  console_open(const char *fbpath, const char *ttypath);
int  console_info(unsigned *cols, unsigned *rows, unsigned *width, unsigned *height);
void console_show_pointer(int col, int row);
void console_hide_pointer(void);
void console_highlight(int start, int end);
int  console_read_screen(char *buf, int len);
int  console_paste(const char *text, size_t len);

int  select_init(struct selection *s, int cols, int rows);
void select_begin(struct selection *s, int cell, long now_ms);
void select_extend(struct selection *s, int cell);
void select_drag(struct selection *s, int cell);
void select_end(struct selection *s);
void select_paste(const struct selection *s);

#endif
