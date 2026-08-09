/* types.h -- fundamental types shared across all compiler modules */

#ifndef BASE_TYPES_H
#define BASE_TYPES_H

/* Non-owning string slice — used throughout the compiler.
 * Points into source text or arena-allocated memory. */
typedef struct {
    const char* data;
    int         length;
} String;

#endif /* BASE_TYPES_H */
