/* Test parser error recovery: syntax errors should not crash.
 * The fix: ll_parse_expr_stmt() returns NULL when ll_parse_expr() fails,
 * instead of dereferencing the NULL pointer.
 *
 * This file intentionally contains syntax errors to exercise the
 * error recovery path. The compiler should exit cleanly, not segfault. */

void test_valid_function(void) {
    int x = 42;
    int y = x + 1;
}

/* The next function has a syntax error — missing semicolon.
 * The parser should report the error and skip to the next function. */
void test_broken(void) {
    int a = 1
    int b = 2;  /* parser should recover here */
}

/* After error recovery, parsing should continue normally. */
void test_after_error(void) {
    int z = 99;
}
