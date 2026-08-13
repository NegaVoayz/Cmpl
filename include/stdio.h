/* Minimal C99 stdio.h stub for Cmpl self-hosting */

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

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
int   fflush(FILE* f);
int   fputs(const char* s, FILE* f);
char* fgets(char* buf, int size, FILE* f);

FILE* fopen(const char* path, const char* mode);
int   fclose(FILE* f);
int   fseek(FILE* f, long offset, int whence);
long  ftell(FILE* f);
int   fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
int   snprintf(char* buf, size_t size, const char* fmt, ...);
int   remove(const char* path);

/* non-standard but used by our code */
FILE* popen(const char* cmd, const char* mode);
int   pclose(FILE* f);

#endif
