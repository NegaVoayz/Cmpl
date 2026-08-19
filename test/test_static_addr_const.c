/* test_static_addr_const.c -- function-scope statics as address-constant
 * roots in static initializers.
 *
 * Regression: `static int* p = arr + 1;` inside a function failed with
 * "initializer element is not constant" — the ICE address-constant path
 * (ice_eval/ice_addr_of) only knew FILE-scope globals, and the emitted
 * constant used the SOURCE name while the real global is mangled
 * <func>.<var>.<line>.  gen_static_local now registers each static in
 * the module type table with its mangled IR name, and the const-init
 * emitters (gen_const_ident / gen_const_unary / gen_const_ice_eval)
 * translate the name via ir_const_ir_name at VAL_GLOBAL creation.
 *
 * Also covers the struct-decl parser fix: `static struct {..} st;`
 * kept LINK_HOST (storage class dropped on struct-typed declarators),
 * so a function-scope static struct was a plain local and a file-scope
 * one emitted EXTERNAL.  Values > 127 catch i8 truncation; gcc parity
 * is checked byte-for-byte by Stage C / diff_gcc.
 */
#include <stdio.h>

int g_scalar = 150;

struct S { int f; int g; };
static struct S gs = {200, 250};          /* file-scope static struct */

static int* make_ptrs(void)
{
    static int arr[4] = {100, 200, 300, 400};
    static int* p = arr + 1;              /* decay + pointee arith */
    static int* q = &arr[2];              /* &arr[i] */
    static int* s = &g_scalar;            /* &file-scope global */
    static struct { int f; } st = {300};  /* function-scope static struct */
    static int* t = &st.f;                /* &static.field */
    static struct S* u = &gs;             /* &file-scope static struct */

    printf("static_addr_const: %d %d %d %d %d %d %d\n",
           p[0], p[1], q[0], *s, *t, u->f, u->g);
    return p;
}

int main(void)
{
    int* p = make_ptrs();
    if (p[0] != 200) return 1;
    if (p[1] != 300) return 2;
    printf("done\n");
    return 0;
}
