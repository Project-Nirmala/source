#ifndef STDLIB_H
#define STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int code);
void abort(void);
long strtol(const char *s, char **end, int base);
#endif
