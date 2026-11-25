#ifndef _SYS_FBTEXT_H
#define _SYS_FBTEXT_H

#define NIR_FBINFO    0x4E10UL
#define NIR_FBPOINTER 0x4E11UL
#define NIR_FBSELECT  0x4E12UL
#define NIR_FBREAD    0x4E13UL

struct nir_fbinfo {
    unsigned int cols;
    unsigned int rows;
    unsigned int selection;
    unsigned int width;
    unsigned int height;
    unsigned int cellw;
    unsigned int cellh;
};

struct nir_fbptr {
    int col;
    int row;
    int visible;
};

struct nir_fbsel {
    int start;
    int end;
};

struct nir_fbread {
    int   start;
    int   len;
    void *buf;
};

#endif
