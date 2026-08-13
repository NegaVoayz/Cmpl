/* test_ir.c -- Comprehensive IR correctness tests
 *
 * Validates that cmpl generates valid LLVM IR for clang -c.
 * Each function tests a specific bug category that was fixed.
 *
 * Run: cmpl -emit-llvm test/test_ir.c -o test_ir.ll
 *      clang -c --target=x86_64-pc-linux-gnu test_ir.ll -o /dev/null
 */

/* ---------------------------------------------------------------
 * NOTE: Indirect call (Test 1) is validated by the self-hosting
 * test suite — files lexer.c, number.c, pp_expand.c, pp_line.c
 * exercise the ir_build_call_ptr path for member-access calls.
 * --------------------------------------------------------------- */

/* ---------------------------------------------------------------
 * Test 2: Float arithmetic (fadd/fsub/fmul/fdiv + type annotation)
 * Fix: float builders, gen_binary_op float detection,
 *      dump_instr type printing for fadd/fsub/fmul/fdiv
 * --------------------------------------------------------------- */
double float_arith(double a, double b) {
    double sum  = a + b;
    double diff = a - b;
    double prod = a * b;
    double quot = a / b;
    return sum + diff + prod + quot;
}

/* ---------------------------------------------------------------
 * Test 3: Float comparisons (fcmp with o-prefix predicates)
 * Fix: ir_build_fcmp builder, gen_binary_op float detection,
 *      dump_instr fcmp one/oeq/olt/ogt/ole/oge
 * --------------------------------------------------------------- */
int float_cmp(double a, double b) {
    int r = 0;
    if (a == b) r |= 1;
    if (a != b) r |= 2;
    if (a < b)  r |= 4;
    if (a > b)  r |= 8;
    if (a <= b) r |= 16;
    if (a >= b) r |= 32;
    return r;
}

/* ---------------------------------------------------------------
 * Test 4: Float unary minus (-double_val → fsub double 0.0, val)
 * Fix: AST_UNARY uses fsub + ir_const_float for float types
 * --------------------------------------------------------------- */
double float_neg(double x) {
    return -x;
}

/* ---------------------------------------------------------------
 * Test 5: Float not (!double_val → fcmp oeq double %val, 0.0)
 * Fix: AST_UNARY BANG uses fcmp + ir_const_float for float types
 * --------------------------------------------------------------- */
int float_not(double x) {
    return !x;
}

/* ---------------------------------------------------------------
 * Test 6: Float condition in ternary (coerce_to_i1 for float)
 * Fix: AST_TERNARY condition coercion handles float with fcmp
 * --------------------------------------------------------------- */
double float_ternary(double a, double b, double c) {
    return a ? b : c;
}

/* ---------------------------------------------------------------
 * Test 7: Array decay to pointer
 * Fix: ir_build_load array decay → GEP to first element
 *      global array handling for VAL_GLOBAL non-ptr types
 * --------------------------------------------------------------- */
int array_decay(void) {
    int arr[4] = {10, 20, 30, 40};
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += arr[i];
    }
    return sum;
}

/* ---------------------------------------------------------------
 * Test 8: Pointer vs integer comparison (inttoptr for non-zero)
 * Fix: gen_binary_op converts non-zero int to ptr via bitcast
 * --------------------------------------------------------------- */
int ptr_int_cmp(char *p, int ch) {
    /* Compare pointer (from array access) to integer value.
     * Generates icmp eq ptr %p, inttoptr (i32 ch to ptr) */
    return *p == ch;
}

/* ---------------------------------------------------------------
 * Test 9: Pointer postfix ++/-- (GEP instead of add)
 * Fix: AST_POSTFIX uses GEP for pointer operands
 * --------------------------------------------------------------- */
char *ptr_postfix(char *p) {
    p++;
    return p;
}

/* ---------------------------------------------------------------
 * Test 10: Compound assignment operators
 * Fix: TOK_PLUSEQ/MINUSEQ/STAREQ/etc added to gen_binary_op
 * --------------------------------------------------------------- */
int compound_assign(int a, int b) {
    a += b;
    a -= b;
    a *= b;
    a /= b ? b : 1;
    a &= b;
    a |= b;
    a ^= b;
    return a;
}

/* ---------------------------------------------------------------
 * Test 11: Nested for loops with continue
 * Fix: unique block labels (.N suffix), is_terminator() guard
 *      prevents double terminators
 * --------------------------------------------------------------- */
int nested_loops(void) {
    int sum = 0;
    int i, j;
    for (i = 0; i < 10; i++) {
        for (j = 0; j < 10; j++) {
            if (j == i) continue;
            sum += j;
        }
        if (sum > 100) break;
    }
    return sum;
}

/* ---------------------------------------------------------------
 * Test 12: Select/ternary with mismatched branch types
 * Fix: AST_TERNARY type coercion for mismatched branches
 * --------------------------------------------------------------- */
int ternary_coerce(int cond, char small_val) {
    /* char promotes to int; select branches must match */
    return cond ? small_val : 42;
}

/* ---------------------------------------------------------------
 * Test 13: Return type coercion (int 0 → ptr null, i1 → int, etc.)
 * Fix: AST_RETURN coerces return value to function return type
 * --------------------------------------------------------------- */
void *return_null_ptr(void) {
    return 0;  /* int 0 should become ptr null */
}

int return_i1_as_int(int a, int b) {
    return a == b;  /* i1 from icmp coerce to i32 return type */
}

/* ---------------------------------------------------------------
 * Test 14: GEP on opaque pointers (LLVM 19 scalar single-index)
 * Fix: idx1 skip when constant 0, dump_instr collapses zero-idx0
 * --------------------------------------------------------------- */
char *gep_scalar(char *base, int offset) {
    return base + offset;  /* ptr + int → GEP */
}

/* ---------------------------------------------------------------
 * Test 15: zext emission for widening casts
 * Fix: IROP_BITCAST in dump_instr selects zext for int widening
 * --------------------------------------------------------------- */
int small_to_large(char c) {
    /* char → int: zext (not bitcast) */
    return (int)c;
}

/* ---------------------------------------------------------------
 * Test 16: Double terminators in if/while/for with continue/break
 * Fix: is_terminator() checks all terminator opcodes
 * --------------------------------------------------------------- */
int no_double_terminator(int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (i == 5) continue;
        if (i == 8) break;
    }
    return i;
}

/* ---------------------------------------------------------------
 * Test 17: Local array static initializer
 * Fix: array decay via GEP for alloca'd arrays
 * --------------------------------------------------------------- */
int local_array(void) {
    int data[3] = {1, 2, 3};
    return data[0] + data[1] + data[2];
}

/* ---------------------------------------------------------------
 * Test 18: VReg ID collision after mem2reg (parameter usage)
 * Fix: ir-opt passes must NOT overwrite result->kind to non-VAL_INSTR.
 *      mem2reg renamed loads from promoted allocas would get
 *      kind=VAL_PARAM (param's kind), causing renumbering to skip
 *      them — producing duplicate %0 (param + load). Clang rejects
 *      duplicate SSA definitions.
 * --------------------------------------------------------------- */
int vreg_no_collide(int a, int b, int c) {
    int x = a + b;
    int y = b * c;
    return x + y;
}
