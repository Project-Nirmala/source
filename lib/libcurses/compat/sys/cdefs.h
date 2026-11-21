#ifndef _COMPAT_CDEFS_H
#define _COMPAT_CDEFS_H
#define __warn_references(a, b)
#define _DIAGASSERT(x) ((void)0)
#define __BEGIN_DECLS
#define __END_DECLS
#define __dead __attribute__((noreturn))
#define __unused __attribute__((unused))
#define __printflike(a, b) __attribute__((format(printf, a, b)))
#define __RCSID(x)
#define __COPYRIGHT(x)
#define __weak_alias(a, b)
#define __predict_false(x) __builtin_expect((x) != 0, 0)
#define __predict_true(x) __builtin_expect((x) != 0, 1)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __CONCAT(a, b) a##b
#define __BIT(n) (1u << (n))
#include <stdarg.h>
typedef __builtin_va_list __va_list;

#define __scanflike(a, b) __attribute__((format(scanf, a, b)))
#define __strong_alias(a, b)
#define __UNCONST(x) ((void *)(unsigned long)(const void *)(x))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __sysv_signal
#include <stdint.h>
#include <stddef.h>
#define __nothing ((void)0)
#endif
