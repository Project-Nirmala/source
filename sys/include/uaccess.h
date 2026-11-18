#ifndef _NIRMALA_UACCESS_H
#define _NIRMALA_UACCESS_H

#include <types.h>

int  copy_from_user(void *dst, uint64_t uva, size_t n);
int  copy_to_user(uint64_t uva, const void *src, size_t n);
long copy_str_from_user(char *dst, uint64_t uva, size_t max);

#endif
