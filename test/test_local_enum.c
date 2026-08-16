/* test_local_enum.c -- function-scope enums and enum-sized local arrays
 *
 * Regression tests for two related bugs:
 *  1. enum constants declared inside a function body (function-scope
 *     enums) resolved to undef in IR (opt_enum/collect_enum_vals only
 *     scanned top-level decls) — `enum { N = 16 }; int arr[N];` gave
 *     alloca [0 x i32] and `icmp ... undef`.
 *  2. a local enum/typedef statement as the FIRST statement of a block
 *     stopped resolve_stmt_chain (is_stmt_type lacked AST_ENUM_DEF /
 *     AST_TYPEDEF), leaving following local var decls with unresolved
 *     TYPE_NAMED → member access on them emitted undef.
 */
#include <stdio.h>

typedef struct Box Box;
struct Box { int val; Box* next; };

int count(Box* b)
{
    enum { LIMIT = 16 };
    int n = 0;
    Box* p = b;
    while (p && n < LIMIT) {
        n += p->val;
        p = p->next;
    }
    return n;
}

int arrsum(void)
{
    enum { N = 16 };
    int arr[N];
    int i, s = 0;
    for (i = 0; i < N; i++) arr[i] = i;
    for (i = 0; i < N; i++) s += arr[i];
    return s;
}

int typedef_first(void)
{
    typedef int MyInt;
    MyInt x = 5;
    return x * 2;
}

int main(void)
{
    Box a = { 1, 0 };
    Box b = { 2, &a };
    Box c = { 4, &b };

    if (count(&c) != 7) return 1;
    if (arrsum() != 120) return 2;
    if (typedef_first() != 10) return 3;

    printf("local enum ok: %d %d %d\n", count(&c), arrsum(), typedef_first());
    return 0;
}
