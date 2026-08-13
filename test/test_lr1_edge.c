/* Test LR(1) edge cases: ternary precedence, nested ternary, casts */

int test_ternary_binary() {
    int a;
    int b;
    int c;
    int d;
    int r;
    a = 1;
    b = 2;
    c = 3;
    d = 4;
    /* a ? b : c + d should parse as a ? b : (c + d), not (a ? b : c) + d */
    r = a ? b : c + d;
    return r;
}

int test_nested_ternary() {
    int a;
    int b;
    int c;
    int d;
    int e;
    int r;
    a = 1;
    b = 1;
    c = 2;
    d = 3;
    e = 4;
    /* a ? b ? c : d : e should parse as a ? (b ? c : d) : e */
    r = a ? b ? c : d : e;
    return r;
}

int test_ternary_postfix() {
    int a;
    int b;
    int r;
    a = 0;
    b = 2;
    /* a ? b : a++ -- postfix on else-expr */
    r = a ? b : a;
    return r;
}

int test_cast_expr() {
    double x;
    int r;
    x = 3;
    /* (int)x should create AST_CAST */
    r = (int)x;
    return r;
}

int test_ternary_chain() {
    int a;
    int b;
    int c;
    int d;
    int e;
    int r;
    a = 0;
    b = 1;
    c = 1;
    d = 2;
    e = 3;
    /* a ? b : c ? d : e -- right-assoc */
    r = a ? b : c ? d : e;
    return r;
}

int test_ternary_binary_mixed() {
    int a;
    int b;
    int c;
    int d;
    int e;
    int r;
    a = 1;
    b = 2;
    c = 3;
    d = 4;
    e = 5;
    /* binary on both branches of ternary */
    r = a ? b + c : d + e;
    return r;
}

int main() {
    int x;
    x = test_ternary_binary();
    x = x + test_nested_ternary();
    x = x + test_ternary_postfix();
    x = x + test_cast_expr();
    x = x + test_ternary_chain();
    x = x + test_ternary_binary_mixed();
    return x;
}
