/* Test file for source-level AST optimizations */

/* -- Constant Folding -- */
int fold_add(void)        { return 2 + 3; }
int fold_mul(void)        { return 5 * 4; }
int fold_sub(void)        { return 10 - 3; }
int fold_div(void)        { return 20 / 4; }
int fold_unary_minus(void){ return -3 + 7; }
int fold_not(void)        { return !0; }
int fold_bitnot(void)     { return ~0; }
int fold_compare_gt(void) { return 5 > 3; }
int fold_compare_lt(void) { return 3 < 5; }
int fold_compare_eq(void) { return 5 == 5; }
int fold_compare_neq(void){ return 5 != 5; }
int fold_logical_and(void){ return 1 && 1; }
int fold_logical_or(void) { return 0 || 1; }
int fold_precedence(void) { return 1 + 2 * 3; }
int fold_bitwise(void)    { return 3 & 1; }
int fold_shift(void)      { return 1 << 3; }

/* -- Constant Propagation -- */
int prop_simple(void) {
    int x = 5;
    return x + 3;
}

int prop_no_reassign(void) {
    int a = 10;
    int b = a + 2;
    return b;
}

int prop_no_reassign2(void) {
    int x = 5;
    int y = x;
    return y;
}

/* -- Dead Code: After Terminator -- */
int dead_after_return(void) {
    return 42;
    int x = 5;
    return 0;
}

/* -- Dead Code: if(0) -- */
int dead_if_zero(void) {
    if (0) {
        return 1;
    }
    return 0;
}

int dead_if_zero_else(void) {
    if (0) {
        return 1;
    } else {
        return 2;
    }
}

/* -- Dead Code: if(1) -- */
int dead_if_one(void) {
    if (1) {
        return 42;
    } else {
        return 0;
    }
}

/* -- Dead Code: while(0) -- */
int dead_while_zero(void) {
    while (0) {
        return 1;
    }
    return 0;
}
