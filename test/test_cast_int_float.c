/* test_cast_int_float.c -- int<->float conversions must emit valid IR
 *
 * Regression test: casts across the int/float boundary used to emit
 * invalid LLVM IR (zext i32 to double, trunc double to i32). They
 * must now emit sitofp / fptosi and produce correct runtime values.
 *
 * Run: cmpl -emit-llvm test/test_cast_int_float.c -o t.ll
 *      clang -c --target=x86_64-pc-linux-gnu t.ll -o /dev/null
 *      clang t.ll -o t && ./t        # must exit 0
 */

/* explicit cast: int -> float (sitofp) */
double cast_up(int x) { return (double)x; }

/* explicit cast: float -> int (fptosi, truncates toward zero) */
int cast_down(double x) { return (int)x; }

/* return coercion: int expr returned from double function */
double ret_up(int x) { return x; }

/* return coercion: double expr returned from int function */
int ret_down(double x) { return x; }

/* call-arg coercion: int passed to double parameter */
double arg_up(double a, int b) { return a + b; }

/* call-arg coercion: double passed to int parameter */
int arg_down(int a, double b) { return a + b; }

/* select with int and double branches */
int sel_mixed(int c, int x, double y) { return c ? x : (int)y; }

/* ?: with implicitly-converted int/double branches */
int sel_implicit(int c, int x, double y) { return c ? x : y; }

/* store coercion: int assigned/stored into double slot and back */
double assign_up(int x) {
    double d;
    d = x;              /* assignment: int -> double */
    return d;
}

int assign_down(double x) {
    int i;
    i = x;              /* assignment: double -> int */
    return i;
}

double init_up(int x) {
    double d = x;       /* var initializer: int -> double */
    return d;
}

int init_down(double x) {
    int i = x;          /* var initializer: double -> int */
    return i;
}

int main(void) {
    if (cast_up(7) != 7.0) return 1;
    if (cast_down(3.7) != 3) return 2;
    if (cast_down(-3.7) != -3) return 3;
    if (ret_up(5) != 5.0) return 4;
    if (ret_down(2.5) != 2) return 5;
    if (arg_up(2.5, 3) != 5.5) return 6;
    if (arg_down(2, 2.9) != 4) return 7;
    if (sel_mixed(1, 42, 3.7) != 42) return 8;
    if (sel_mixed(0, 42, 3.7) != 3) return 9;
    if (sel_implicit(1, 5, 2.5) != 5) return 10;
    if (sel_implicit(0, 5, 2.5) != 2) return 11;
    if (assign_up(9) != 9.0) return 12;
    if (assign_down(4.7) != 4) return 13;
    if (init_up(11) != 11.0) return 14;
    if (init_down(6.9) != 6) return 15;
    return 0;
}
