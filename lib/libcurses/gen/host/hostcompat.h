#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define __RCSID(x)
#define __unused __attribute__((unused))
#define __dead __attribute__((noreturn))
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __UNCONST(x) ((void *)(uintptr_t)(const void *)(x))
#define _DIAGASSERT(x) ((void)0)
#define __predict_false(x) (x)
#define __predict_true(x) (x)
const char *getprogname(void);
size_t strlcpy(char *d, const char *s, size_t n);
#define __printflike(a, b) __attribute__((format(printf, a, b)))
#define __BEGIN_DECLS
#define __END_DECLS
void *emalloc(size_t n);
void *ecalloc(size_t a, size_t b);
char *estrdup(const char *s);
int easprintf(char **out, const char *fmt, ...);
