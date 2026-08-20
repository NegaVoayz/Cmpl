/* Minimal C99 stdio.h stub for Cmpl self-hosting */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct _FILE FILE;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int   printf(const char* fmt, ...);
int   fprintf(FILE* f, const char* fmt, ...);
int   snprintf(char* buf, size_t size, const char* fmt, ...);
int   sprintf(char* buf, const char* fmt, ...);
int   scanf(const char* fmt, ...);
int   sscanf(const char* s, const char* fmt, ...);
int   fflush(FILE* f);
int   fputs(const char* s, FILE* f);
char* fgets(char* buf, int size, FILE* f);

/* va_list-taking printf/scanf family (C99 7.19.6.7-9): needed by any
 * real variadic helper that forwards to printf-family formatting. */
int   vprintf(const char* fmt, va_list ap);
int   vfprintf(FILE* f, const char* fmt, va_list ap);
int   vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int   vsprintf(char* buf, const char* fmt, va_list ap);
int   vscanf(const char* fmt, va_list ap);
int   vfscanf(FILE* f, const char* fmt, va_list ap);
int   vsscanf(const char* s, const char* fmt, va_list ap);

FILE* fopen(const char* path, const char* mode);
int   fclose(FILE* f);
int   fseek(FILE* f, long offset, int whence);
long  ftell(FILE* f);
int   fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
int   remove(const char* path);

/* non-standard but used by our code */
FILE* popen(const char* cmd, const char* mode);
int   pclose(FILE* f);

#endif
