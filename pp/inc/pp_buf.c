/* pp_buf.c -- dynamic string buffer + file I/O helpers (shared across pp/).
 *
 * Moved out of pp.c in B-20 so the main processor stays a slim driver.
 * Every helper here is non-static and declared in pp.h; the Buffer type
 * lives in pp.h and is used by the macro/line/directive machinery.
 */

#include "../pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Dynamic string buffer --- */

void
buf_init(Buffer* b)
{
    b->data = NULL; b->len = 0; b->cap = 0;
}

void
buf_append(Buffer* b, const char* s, int slen)
{
    if (b->len + slen + 1 > b->cap) {
        b->cap = b->len + slen + 256;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, slen);
    b->len += slen;
    b->data[b->len] = '\0';
}

void
buf_free(Buffer* b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* --- File I/O --- */

char*
read_file(const char* path, int* out_len)
{
    FILE* f = fopen(path, "rb");

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    *out_len = (int)size;
    return buf;
}

void
dir_of(const char* path, char* dir, int dir_sz)
{
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    const char* sep = (slash > back) ? slash : back;

    (void)dir_sz;
    if (sep) {
        int n = (int)(sep - path);
        if (n >= dir_sz) n = dir_sz - 1;
        memcpy(dir, path, n);
        dir[n] = '\0';
    } else {
        dir[0] = '.'; dir[1] = '\0';
    }
}
