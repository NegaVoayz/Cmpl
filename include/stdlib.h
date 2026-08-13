/* Minimal C99 stdlib.h stub for Cmpl self-hosting */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

void* malloc(size_t size);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void  free(void* ptr);

char* getenv(const char* name);
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*));

/* numeric conversion (used by the tokenizer) */
double             strtod(const char* nptr, char** endptr);
long               strtol(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);

#endif
