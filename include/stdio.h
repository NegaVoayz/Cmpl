/* Minimal C99 stdio.h stub for Cmpl self-hosting */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

typedef struct _FILE FILE;

#ifndef NULL
#define NULL ((void*)0)
#endif

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* path, const char* mode);
int   fclose(FILE* f);
int   fprintf(FILE* f, const char* fmt, ...);
int   fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
int   snprintf(char* buf, size_t size, const char* fmt, ...);

/* non-standard but used by our code */
FILE* popen(const char* cmd, const char* mode);
int   pclose(FILE* f);

#endif
