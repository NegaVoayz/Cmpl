/* flagbf01.c -- diff_gcc: the compiler's own flag fields as 1-bit
 * bit-fields.  Mirrors the exact shapes of struct Type, struct Token
 * and the AST literal arm after the boolean flags became
 * `unsigned x : 1`: adjacent flags between String members, a flag
 * directly before a union, and a flag at the end of a union arm.
 * Locks cmpl's layout + RMW semantics to gcc's. */
#include <stdio.h>

typedef struct { const char* data; int len; } String;

/* shape of tokenizer/ast.h struct Type */
struct TypeLike {
    int      kind;
    void*    next;
    void*    inner;
    String   name;
    void*    params;
    int      arr_size;
    unsigned is_const      : 1;
    unsigned is_volatile   : 1;
    unsigned is_variadic   : 1;
    unsigned func_form     : 1;
    unsigned size_inferred : 1;
    String   size_name;
};

/* shape of tokenizer/token.h struct Token (flag directly before union) */
struct TokenLike {
    int      kind;
    int      line;
    int      col;
    void*    next;
    unsigned is_unsigned : 1;
    union {
        long long int_val;
        double    float_val;
        String    str_val;
    } body;
};

/* shape of ast_node.h literal arm (flag last, after a String member) */
struct LitLike {
    long long int_val;
    char      char_val;
    double    float_val;
    String    str_val;
    unsigned  is_unsigned : 1;
};

int main(void) {
    int sz_t = sizeof(struct TypeLike);
    int sz_tk = sizeof(struct TokenLike);
    int sz_l = sizeof(struct LitLike);

    struct TypeLike t;
    t.is_const = 1; t.is_volatile = 1; t.is_variadic = 1;
    t.func_form = 1; t.size_inferred = 1;
    t.is_const = 0; t.size_inferred = 0;
    t.name.data = "n"; t.name.len = 1;
    t.size_name.data = "s"; t.size_name.len = 1;

    struct TokenLike tk;
    tk.is_unsigned = 1;
    tk.body.int_val = 42;
    tk.is_unsigned = 0;
    int saved = (int)tk.body.int_val;

    struct LitLike L;
    L.int_val = 7;
    L.str_val.data = "x"; L.str_val.len = 1;
    L.is_unsigned = 1;
    L.is_unsigned = 0;

    printf("%d %d %d\n", sz_t, sz_tk, sz_l);
    printf("%d %d %d %d %d %d %d\n",
           t.is_const, t.is_volatile, t.is_variadic, t.func_form,
           t.size_inferred, saved, L.is_unsigned);
    printf("%d %d\n", t.name.len, L.str_val.len);
    return 0;
}
