/* test_bf_agg_member.c -- bit-field structs with AGGREGATE members
 * (struct/union/String), the exact shapes the compiler's own AST uses:
 * 1-bit flags adjacent to String members, flags inside union arms, and
 * nested structs containing bit-fields.  These shapes previously
 * miscompiled (bitcast i64 -> %struct invalid IR; flag offsets wrong
 * because aggregate sizes were clamped to 4 bytes in the layout pass). */
#include <stdio.h>

typedef struct { const char* data; int len; } Str;

/* shape of the compiler's AST_Node.body.literal */
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
        struct { void* operand; int op; } unary;
    } body;
};

/* shape of the compiler's struct Type: plain members, 5 adjacent 1-bit
 * flags between two String members */
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

/* nested struct containing bit-fields, used as a member of another */
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
    int rc = 0;

    struct ASTNodeLike n;
    n.type = 1; n.line = 2; n.next = 0;
    n.body.literal.int_val = 42;
    n.body.literal.char_val = 'z';
    n.body.literal.float_val = 1.5;
    n.body.literal.str_val.data = "lit"; n.body.literal.str_val.len = 3;
    n.body.literal.is_unsigned = 1;

    /* toggling the flag must not disturb neighboring members/bytes */
    int saved_int = (int)n.body.literal.int_val;
    n.body.literal.is_unsigned = 0;
    if (saved_int != 42 || n.body.literal.char_val != 'z') rc |= 1;
    if (n.body.literal.float_val != 1.5) rc |= 2;
    if (n.body.literal.str_val.len != 3) rc |= 4;
    if (n.body.literal.str_val.data[0] != 'l') rc |= 8;

    /* switching union arms (binary arm shares the literal arm's bytes) */
    n.body.binary.op = 7;
    if (n.body.binary.op != 7) rc |= 16;

    struct TypeLike t;
    t.kind = 5; t.arr_size = 8;
    t.name.data = "nm"; t.name.len = 2;
    t.size_name.data = "sz"; t.size_name.len = 2;
    t.is_const = 1; t.is_volatile = 0; t.is_variadic = 1;
    t.func_form = 1; t.size_inferred = 0;
    t.is_const = 0;
    if (!t.is_variadic || !t.func_form) rc |= 32;
    if (t.size_inferred || t.is_volatile || t.is_const) rc |= 64;
    if (t.name.len != 2 || t.size_name.len != 2) rc |= 128;

    struct Outer o;
    o.k = 1;
    o.in.v = 0;
    o.in.a = 1; o.in.b = 1;
    o.in.a = 0;
    if (o.in.b != 1 || o.in.v != 0) rc |= 256;
    o.in.v = 99;
    if (o.in.a != 0 || o.in.b != 1 || o.in.v != 99) rc |= 512;

    printf("rc=%d\n", rc);
    return 0;
}
