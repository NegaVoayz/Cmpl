#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    int   len;
    int   cap;
} Buffer;

void buf_init(Buffer* b) {
    b->data = NULL; b->len = 0; b->cap = 0;
}

void buf_append(Buffer* b, const char* s, int slen) {
    if (b->len + slen + 1 > b->cap) {
        b->cap = b->len + slen + 256;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, slen);
    b->len += slen;
    b->data[b->len] = '\0';
}

int main(void) {
    Buffer buf;
    buf_init(&buf);
    buf_append(&buf, "hello", 5);
    fprintf(stdout, "len=%d cap=%d data='%s'\n", buf.len, buf.cap, buf.data);
    return 0;
}
