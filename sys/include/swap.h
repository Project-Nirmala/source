#ifndef _NIRMALA_SWAP_H
#define _NIRMALA_SWAP_H

#include <types.h>

#define SWAP_PAGE_SECTORS 8

struct blkdrv;

int      swap_on(struct blkdrv *drv, uint64_t start, uint64_t nsect, const char *name);
int      swap_off(struct blkdrv *drv, uint64_t start);
bool     swap_active(void);
bool     swap_overlaps(const struct blkdrv *drv, uint64_t start, uint64_t nsect);
void     swap_free(uint64_t slot);
void     swap_dup(uint64_t slot);
int      swap_fault(uint64_t va);
uint64_t swap_total_bytes(void);
uint64_t swap_free_bytes(void);

#endif
