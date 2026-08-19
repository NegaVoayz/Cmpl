/* Minimal C99 stddef.h stub for Cmpl self-hosting */

#ifndef _STDDEF_H
#define _STDDEF_H

#ifndef NULL
#define NULL ((void*)0)
#endif

typedef unsigned long long size_t;

/* member offset of `member` in `type`: the address of the member at a
 * null base is a constant byte offset (folded by the ICE evaluator) */
#define offsetof(type, member) ((size_t)&((type*)0)->member)

#endif
