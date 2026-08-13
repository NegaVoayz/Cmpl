/* test_sizeof_int.c -- sizeof(builtin type) must yield the type's size
 *
 * Regression for lr1.c: sizeof(int) was parsed as AST_SIZEOF_TYPE with a
 * NULL type_expr (the parser skipped "(int)" instead of parsing it), so
 * every sizeof(<builtin type>) evaluated to 0.  The enum-array tests
 * (test_enum_val.c, test_enum_arr.c) caught this via `N * sizeof(int)`;
 * here sizeof(int) is exercised directly.
 */
int main(void) {
    if (sizeof(int) != 4) return 1;
    if (sizeof(char) != 1) return 2;
    if (sizeof(char*) != 8) return 3;
    if (sizeof(int*) != 8) return 4;
    return 0;
}
