#include <sys/mouse.h>
#include "gpmd.h"

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void pointer_init(struct pointer *p, int width, int height, int cellw, int cellh, int gain)
{
    p->width = width;
    p->height = height;
    p->cellw = cellw;
    p->cellh = cellh;
    p->cols = width / cellw;
    p->gain = gain;
    p->x = width / 2;
    p->y = height / 2;
    p->col = p->x / cellw;
    p->row = p->y / cellh;
}

int pointer_move(struct pointer *p, int dx, int dy)
{
    int col, row, changed;

    p->x = clamp(p->x + dx * p->gain, 0, p->width - 1);
    p->y = clamp(p->y + dy * p->gain, 0, p->height - 1);
    col = p->x / p->cellw;
    row = p->y / p->cellh;
    changed = col != p->col || row != p->row;
    p->col = col;
    p->row = row;
    return changed;
}

int pointer_place(struct pointer *p, unsigned ax, unsigned ay)
{
    int col, row, changed;

    p->x = clamp((int)((unsigned long long)ax * (unsigned)p->width / MOUSE_ABS_MAX), 0, p->width - 1);
    p->y = clamp((int)((unsigned long long)ay * (unsigned)p->height / MOUSE_ABS_MAX), 0, p->height - 1);
    col = p->x / p->cellw;
    row = p->y / p->cellh;
    changed = col != p->col || row != p->row;
    p->col = col;
    p->row = row;
    return changed;
}

int pointer_cell(const struct pointer *p)
{
    return p->row * p->cols + p->col;
}
