#ifndef _NIRMALA_FPU_H
#define _NIRMALA_FPU_H

#include <types.h>

#define FPU_AREA_SIZE 512

void  fpu_init(void);
void *fpu_align(void *raw);
void  fpu_save(void *area);
void  fpu_restore(const void *area);
void  fpu_reset(void *area);

#endif
