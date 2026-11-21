#ifndef _LIBC_ERR_H
#define _LIBC_ERR_H

#include <stdarg.h>

void err(int code, const char *fmt, ...);
void errx(int code, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);
void verr(int code, const char *fmt, va_list ap);
void verrx(int code, const char *fmt, va_list ap);
void vwarn(const char *fmt, va_list ap);
void vwarnx(const char *fmt, va_list ap);

#endif
