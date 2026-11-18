#ifndef _NIRMALA_TYPES_H
#define _NIRMALA_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

#define __unused  __attribute__((unused))
#define __packed  __attribute__((packed))
#define __noreturn __attribute__((noreturn))

#endif
