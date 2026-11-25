#ifndef _NIRMALA_MOUSE_H
#define _NIRMALA_MOUSE_H

#include <types.h>
#include <vfs.h>

void mouse_init(void);
void mouse_byte(uint8_t b);
bool mouse_present(void);

extern struct vnode mouse_vnode;

#endif
