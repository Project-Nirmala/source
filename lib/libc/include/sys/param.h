#ifndef _LIBC_SYS_PARAM_H
#define _LIBC_SYS_PARAM_H

#include <limits.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN PATH_MAX
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#endif
