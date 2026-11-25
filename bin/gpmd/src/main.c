#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "gpmd.h"

#define MOUSE_PATH   "/dev/mouse"
#define FBCON_PATH   "/dev/fbcon"
#define CONSOLE_PATH "/dev/console"

static volatile int stop;

static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-d] [-s pixels-per-mickey]\n", prog);
}

static void handle(struct pointer *ptr, struct selection *sel, const struct mouse_event *ev, int *buttons, int abs_moved,
                   int have_abs)
{
    int moved = have_abs ? abs_moved : pointer_move(ptr, ev->dx, ev->dy);
    int cell = pointer_cell(ptr);
    int pressed = ev->buttons & ~*buttons;
    int released = *buttons & ~ev->buttons;

    *buttons = ev->buttons;
    if (moved) {
        console_show_pointer(ptr->col, ptr->row);
        select_drag(sel, cell);
    }
    if (pressed & MOUSE_BTN_LEFT)
        select_begin(sel, cell, now_ms());
    if (pressed & MOUSE_BTN_RIGHT)
        select_extend(sel, cell);
    if (pressed & MOUSE_BTN_MIDDLE)
        select_paste(sel);
    if (released & (MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT))
        select_end(sel);
}

int main(int argc, char **argv)
{
    struct pointer ptr;
    struct selection sel;
    struct mouse_event ev;
    unsigned cols, rows, width, height;
    int sens = DEFAULT_SENSITIVITY, background = 0, buttons = 0, absolute, i, r;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d")) {
            background = 1;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            sens = atoi(argv[++i]);
            if (sens < 1) {
                usage(argv[0]);
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (mouse_open(MOUSE_PATH) < 0) {
        fprintf(stderr, "gpmd: cannot open %s\n", MOUSE_PATH);
        return 1;
    }
    if (console_open(FBCON_PATH, CONSOLE_PATH) < 0) {
        fprintf(stderr, "gpmd: cannot open %s or %s: %s\n", FBCON_PATH, CONSOLE_PATH, strerror(errno));
        return 1;
    }
    if (console_info(&cols, &rows, &width, &height) < 0) {
        fprintf(stderr, "gpmd: framebuffer text layer not ready: %s\n", strerror(errno));
        return 1;
    }
    pointer_init(&ptr, (int)width, (int)height, (int)(width / cols), (int)(height / rows), sens);
    if (select_init(&sel, (int)cols, (int)rows) < 0) {
        fprintf(stderr, "gpmd: out of memory\n");
        return 1;
    }
    if (background) {
        pid_t pid = fork();

        if (pid < 0) {
            fprintf(stderr, "gpmd: cannot fork\n");
            return 1;
        }
        if (pid > 0)
            return 0;
        setsid();
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    console_show_pointer(ptr.col, ptr.row);
    absolute = mouse_absolute();
    while (!stop) {
        unsigned ax = 0, ay = 0;
        int have_abs = 0, abs_moved = 0;

        r = absolute ? mouse_next_abs(&ev, &ax, &ay, &have_abs) : mouse_next(&ev);
        if (r < 0)
            continue;
        if (r == 0)
            break;
        if (have_abs)
            abs_moved = pointer_place(&ptr, ax, ay);
        else if (absolute)
            ev.dx = ev.dy = 0;
        handle(&ptr, &sel, &ev, &buttons, abs_moved, have_abs);
    }
    console_hide_pointer();
    console_highlight(-1, -1);
    return stop ? 0 : 1;
}
