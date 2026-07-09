#include "preprocess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INCLUDES 64
#define MAX_PATH 512

typedef struct {
    char* data;
    int   len;
    int   cap;
} Buffer;

static void buf_init(Buffer* b) {
    b->data = NULL; b->len = 0; b->cap = 0;
}

static void
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

static char*
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

static void
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

static int
already_included(const char* abs_path, char** seen, int seen_count)
{
    for (int i = 0; i < seen_count; i++) {
        if (strcmp(seen[i], abs_path) == 0) return 1;
    }
    return 0;
}

/* Resolve #include directives recursively. */
static int
resolve(const char* base_dir, const char* src, int srclen,
        Buffer* out, char** seen, int* seen_count)
{
    const char* p = src;
    const char* end = src + srclen;

    while (p < end) {
        if (*p == '#' && (p == src || p[-1] == '\n')) {
            const char* ls = p;

            p++;
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p + 7 >= end || memcmp(p, "include", 7) != 0) {
                buf_append(out, ls, (int)(p - ls));
                continue;
            }
            p += 7;
            while (p < end && (*p == ' ' || *p == '\t')) p++;

            int  is_local = 0;
            char inc_path[MAX_PATH];
            const char* q;

            if (*p == '"') { is_local = 1; p++; q = p; }
            else if (*p == '<') { p++; q = p; }
            else { buf_append(out, ls, (int)(p - ls)); continue; }

            while (q < end && *q != '"' && *q != '>' && *q != '\n') q++;
            int n = (int)(q - p);

            if (n >= MAX_PATH) n = MAX_PATH - 1;
            memcpy(inc_path, p, n);
            inc_path[n] = '\0';
            p = q + 1;
            while (p < end && *p != '\n') p++;
            if (p < end) p++;

            if (!is_local) {
                buf_append(out, ls, (int)(p - ls));
                continue;
            }

            char full[MAX_PATH];
            int  written = snprintf(full, MAX_PATH, "%s/%s", base_dir, inc_path);

            if (written >= MAX_PATH) continue;

            if (*seen_count >= MAX_INCLUDES) continue;
            if (already_included(full, seen, *seen_count)) continue;

            seen[*seen_count] = strdup(full);
            (*seen_count)++;

            int  inc_len;
            char* inc_src = read_file(full, &inc_len);

            if (!inc_src) continue;

            char inc_dir[MAX_PATH];
            dir_of(full, inc_dir, MAX_PATH);

            buf_append(out, "\n", 1);
            resolve(inc_dir, inc_src, inc_len, out, seen, seen_count);
            buf_append(out, "\n", 1);
            free(inc_src);
            continue;
        }
        buf_append(out, p, 1);
        p++;
    }
    return 0;
}

char*
preprocess(const char* filename)
{
    int  len;
    char* src = read_file(filename, &len);

    if (!src) {
        fprintf(stderr, "preprocess: cannot open '%s'\n", filename);
        return NULL;
    }

    char base_dir[MAX_PATH];
    dir_of(filename, base_dir, MAX_PATH);

    Buffer out;
    buf_init(&out);

    char* seen[MAX_INCLUDES];
    int   seen_count = 0;

    resolve(base_dir, src, len, &out, seen, &seen_count);

    for (int i = 0; i < seen_count; i++) free(seen[i]);
    free(src);
    return out.data;
}
