#include <search.h>
#include <stdint.h>
#include <cdbw.h>
#include <string.h>
const char *getprogname(void) { return "tic"; }
size_t strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; } return l; }
struct cdbw *cdbw_open(void) { return 0; }
int cdbw_put(struct cdbw *a, const void *b, size_t c, const void *d, size_t e) { (void)a;(void)b;(void)c;(void)d;(void)e; return -1; }
int cdbw_put_data(struct cdbw *a, const void *b, size_t c, uint32_t *d) { (void)a;(void)b;(void)c;(void)d; return -1; }
int cdbw_put_key(struct cdbw *a, const void *b, size_t c, uint32_t d) { (void)a;(void)b;(void)c;(void)d; return -1; }
int cdbw_output(struct cdbw *a, int b, const char *c, uint32_t (*d)(void)) { (void)a;(void)b;(void)c;(void)d; return -1; }
void cdbw_close(struct cdbw *a) { (void)a; }
uint32_t cdbw_stable_seeder(void) { return 0; }
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <err.h>
void *emalloc(size_t n) { void *p = malloc(n); if (!p) err(1, "malloc"); return p; }
void *ecalloc(size_t a, size_t b) { void *p = calloc(a, b); if (!p) err(1, "calloc"); return p; }
char *estrdup(const char *s) { char *p = strdup(s); if (!p) err(1, "strdup"); return p; }
int easprintf(char **out, const char *fmt, ...) { va_list ap; int r; va_start(ap, fmt); r = vasprintf(out, fmt, ap); va_end(ap); if (r < 0) err(1, "asprintf"); return r; }
void hdestroy1(void (*f)(void *), void (*g)(void *)) { (void)f; (void)g; hdestroy(); }
int ospeed_unused;
struct TERMINAL; struct TERMINAL *cur_term;
