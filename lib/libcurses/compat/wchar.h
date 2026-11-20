#ifndef _COMPAT_WCHAR_H
#define _COMPAT_WCHAR_H
#include <stddef.h>
typedef int wint_t;
typedef struct { int __state; } mbstate_t;
#define WEOF ((wint_t)-1)
size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *d, const wchar_t *s);
wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n);
int wcwidth(wchar_t c);
int wcscmp(const wchar_t *a, const wchar_t *b);
wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
#endif
