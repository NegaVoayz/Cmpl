#ifndef PREPROCESS_H
#define PREPROCESS_H

/* Resolve #include directives in `filename`.
 * Returns a single malloc'd buffer with all included sources inlined,
 * or NULL on error. Caller must free() the result. */
char* preprocess(const char* filename);

#endif
