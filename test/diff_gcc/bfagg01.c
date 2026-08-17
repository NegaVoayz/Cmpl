/* bfagg01.c -- diff_gcc: bit-field structs with aggregate members.
 * Locks cmpl's layout of AST-shaped structs (1-bit flags next to
 * String/union members) to gcc's x86-64 SysV layout: sizeofs must
 * match and neighboring-flag RMW semantics must match. */
#include <stdio.h>

typedef struct { const char* data; int len; } Str;

struct ASTNodeLike {
    int    type;
    int    line;
    void*  next;
    union {
        struct {
            long long int_val;
            char   char_val;
            double float_val;
            Str    str_val;
            unsigned is_unsigned : 1;
        } literal;
        struct { void* left; void* right; int op; } binary;
    } body;
};

struct TypeLike {
    int    kind;
    void*  next;
    void*  inner;
    Str    name;
    void*  params;
    int    arr_size;
    unsigned is_const      : 1;
    unsigned is_volatile   : 1;
    unsigned is_variadic   : 1;
    unsigned func_form     : 1;
    unsigned size_inferred : 1;
    Str    size_name;
};

struct Inner {
    unsigned a : 1;
    unsigned b : 1;
    int    v;
};
struct Outer {
    int    k;
    struct Inner in;
};

int main(void) {
    int sz_node = sizeof(struct ASTNodeLike);
    int sz_type = sizeof(struct TypeLike);
    int sz_inner = sizeof(struct Inner);
    int sz_outer = sizeof(struct Outer);

    struct TypeLike t;
    t.is_const = 1; t.is_volatile = 1; t.is_variadic = 1;
    t.func_form = 1; t.size_inferred = 1;
    t.is_const = 0; t.size_inferred = 0;

    struct Outer o;
    o.k = 1;
    o.in.v = 0;
    o.in.a = 1; o.in.b = 1;
    o.in.a = 0;

    printf("%d %d %d %d\n", sz_node, sz_type, sz_inner, sz_outer);
    printf("%d %d %d %d %d %d %d\n",
           t.is_const, t.is_volatile, t.is_variadic, t.func_form,
           t.size_inferred, o.in.a, o.in.b);
    return 0;
}
