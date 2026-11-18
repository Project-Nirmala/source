#ifndef _NIRMALA_WALLCLOCK_H
#define _NIRMALA_WALLCLOCK_H

#include <types.h>

void     wallclock_init(void);
uint64_t wall_seconds(void);
void     wall_time(uint64_t *sec, uint32_t *nsec);
uint64_t rtc_read_epoch(void);

#endif
