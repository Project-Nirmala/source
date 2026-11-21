#ifndef _NIRMALA_CMDLINE_H
#define _NIRMALA_CMDLINE_H

#include <types.h>

void        cmdline_set(const char *s);
const char *cmdline_get(void);
bool        cmdline_value(const char *key, char *out, size_t size);

#endif
