#ifndef _NIRMALA_FBCON_H
#define _NIRMALA_FBCON_H

#include <types.h>
#include <vfs.h>

#define FONT_GLYPHS 95
#define FONT_WIDTH  8
#define FONT_HEIGHT 16

extern const uint8_t font8x16[FONT_GLYPHS][FONT_HEIGHT];

struct limine_framebuffer_response;

void fbcon_init(struct limine_framebuffer_response *resp);
void fbcon_putc(char c);
int fbcon_cols(void);
int fbcon_rows(void);
bool fbcon_text_ready(void);
bool fbcon_selection_active(void);
size_t fbcon_text_copy(unsigned start, void *dst, size_t len);
void fbcon_set_pointer(int col, int row, bool visible);
void fbcon_pixel_info(unsigned *w, unsigned *h, unsigned *cellw, unsigned *cellh);
void fbcon_set_selection(int start, int end);

extern struct vnode fbtext_vnode;

#endif
