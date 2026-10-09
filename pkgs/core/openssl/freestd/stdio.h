#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
#ifndef NULL
#define NULL ((void *)0)
#endif
/* OpenSSL's BIO_FILE / FILE-using declarations only need the type name;
 * the kernel implements no hosted stdio.  Anything that actually calls into
 * these (e.g. PEM_read_fp/BIO_new_file) is not used by the memory-BIO client. */
struct _IO_FILE { int _flags; unsigned char *_ptr; };
typedef struct _IO_FILE FILE;
typedef long int fpos_t;
int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int snprintf(char *s, size_t n, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int fflush(FILE *stream);
extern FILE *stderr;
extern FILE *stdout;
extern FILE *stdin;
#endif
