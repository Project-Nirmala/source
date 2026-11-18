#ifndef _NIRMALA_FBCON_H
#define _NIRMALA_FBCON_H

#include <types.h>

#define FONT_GLYPHS 95
#define FONT_WIDTH  8
#define FONT_HEIGHT 16

extern const uint8_t font8x16[FONT_GLYPHS][FONT_HEIGHT];

struct limine_framebuffer_response;

void fbcon_init(struct limine_framebuffer_response *resp);
void fbcon_putc(char c);

#endif
