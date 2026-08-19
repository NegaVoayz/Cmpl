/* test_enum_runtime_value.c -- enumerator constants whose value is NOT
 * an AST-foldable literal (sizeof-based, pointer arithmetic, ternary)
 * must still resolve to their ICE-evaluated value when used in FUNCTION
 * BODY expressions.
 *
 * opt_enum only substitutes AST-foldable enumerator values; the rest
 * are registered in mod->enum_vals at IR gen and const contexts resolve
 * them via resolve_enum_idents, but gen_expr_ident had no enum_vals
 * lookup — a function-body use lowered to i32 undef (silent wrong code).
 * Regression: enum { SZ = sizeof(garr) } used in main printed garbage.
 * gcc parity is checked byte-for-byte by Stage C.
 */
#include <stdio.h>

int garr[4] = {1, 2, 3, 4};
int g = 7;

enum { SZ = sizeof(garr) };          /* sizeof-based: 16 */
enum { PD = &garr[2] - &garr[0] };   /* ptr diff in ICE: 2 */
enum { EQ = (garr == garr) };        /* ptr compare in ICE: 1 */
enum { T = (1 ? 10 : 20) };          /* ternary: 10 */

int main(void)
{
    enum { FS = sizeof(garr) + 1 };  /* function-scope enum: 17 */

    if (SZ != 16) return 1;
    if (PD != 2) return 2;
    if (EQ != 1) return 3;
    if (T != 10) return 4;
    if (FS != 17) return 5;

    printf("enum_runtime_value: %d %d %d %d %d\n", SZ, PD, EQ, T, FS);
    return 0;
}
