#ifndef _NIRMALA_POWER_H
#define _NIRMALA_POWER_H

#include <types.h>

extern uint64_t acpi_rsdp_phys;

/* All three flush the block cache first and never return. */
__noreturn void power_restart(void);
__noreturn void power_off(void);
__noreturn void power_halt(void);

#endif
