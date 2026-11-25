#ifndef _SYS_MOUSE_H
#define _SYS_MOUSE_H

#define MOUSE_BTN_LEFT   1
#define MOUSE_BTN_RIGHT  2
#define MOUSE_BTN_MIDDLE 4

#define MOUSE_FLAG_OVERRUN 1

#define NIR_MOUSEINFO 0x4D01UL
#define NIR_MOUSEABS  0x4D02UL

#define MOUSE_ABS_MAX 65536

struct mouse_event {
    short         dx;
    short         dy;
    short         dz;
    unsigned char buttons;
    unsigned char flags;
};

struct mouse_info {
    unsigned int present;
    unsigned int id;
    unsigned int wheel;
    unsigned int overruns;
};

struct mouse_abs {
    unsigned int valid;
    unsigned int x;
    unsigned int y;
};

#endif
