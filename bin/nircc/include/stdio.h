#ifndef STDIO_H
#define STDIO_H
#include <stddef.h>
#include <stdarg.h>
struct _IO_FILE;
typedef struct _IO_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int fputc(int c, FILE *f);
int vsprintf(char *buf, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
int fputs(const char *s, FILE *f);
int puts(const char *s);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *f);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
#endif
