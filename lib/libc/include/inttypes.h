#ifndef _LIBC_INTTYPES_H
#define _LIBC_INTTYPES_H

#include <stdint.h>

#define PRIdMAX "ld"
#define PRIuMAX "lu"
#define PRIdPTR "ld"
#define PRIxMAX "lx"

intmax_t strtoimax(const char *s, char **end, int base);

uintmax_t strtoumax(const char *s, char **end, int base);

#endif
