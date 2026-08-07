/* Test sizeof(expr), sizeof(type), and sizeof in binary expressions.
 * This covers the exact crash scenario: sizeof was unhandled in gen_expr(),
 * returning NULL which flowed into ir_build_sdiv(NULL, NULL) → segfault. */

typedef struct { int a; long b; char c; } Foo;

void test_sizeof_expr(void) {
    int arr[10];
    int n;

    /* sizeof expression */
    n = sizeof(arr);

    /* sizeof type */
    n = sizeof(int);
    n = sizeof(long);

    /* sizeof in division (the exact crash pattern) */
    n = sizeof(arr) / sizeof(arr[0]);

    /* sizeof in multiplication */
    n = sizeof(Foo) * 2;
}

void test_sizeof_compare(void) {
    /* sizeof in comparisons */
    if (sizeof(int) == 4) return;
    if (sizeof(long) > sizeof(int)) return;
}
