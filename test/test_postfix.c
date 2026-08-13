/* Test postfix ++ and -- operators.
 * These were unhandled in gen_expr(), causing NULL dereferences when
 * postfix ops appeared inside expressions (e.g., for-loop update). */

void test_postfix_inc(void) {
    int i = 0;
    int j;

    /* simple post-increment */
    i++;

    /* post-increment in expression */
    j = i++;

    /* post-decrement */
    i--;

    /* post-decrement in expression */
    j = i--;
}

void test_postfix_in_loop(void) {
    int i;
    int sum = 0;

    for (i = 0; i < 10; i++) {
        sum = sum + i;
    }
}

void test_postfix_mixed(void) {
    int a = 5;
    int b = 10;
    int c;

    /* postfix in binary expr */
    c = a++ + b--;
    c = a-- - b++;
}
