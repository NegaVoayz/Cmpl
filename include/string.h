/* Minimal C99 string.h stub for Cmpl self-hosting */

#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int   memcmp(const void* s1, const void* s2, size_t n);
int   strcmp(const void* s1, const void* s2);
int   strncmp(const void* s1, const void* s2, size_t n);
size_t strlen(const void* s);
void* strcpy(void* dest, const void* src);
void* strncpy(void* dest, const void* src, size_t n);
void* strrchr(const void* s, int c);
void* strdup(const void* s);
void* strstr(const void* haystack, const void* needle);
void* strpbrk(const void* s, const void* accept);

#endif
