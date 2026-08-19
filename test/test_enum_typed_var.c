/* test_enum_typed_var.c -- enum-tag references as variable types.
 *
 * The LL declaration dispatcher routed EVERY `enum` token to the
 * enum-DEFINITION parser (ll_parse_enum_def), which ll_expect(SEMI)s
 * when there is no `{` body — so `enum Color x;` (a tag reference used
 * as a type) failed to parse with "expected token 89 (;) ... got 44".
 * ll_decl_dispatch.c now routes only definition forms (`enum {..}`,
 * `enum Tag {..}`, forward `enum Tag;`) to the def parser; references
 * fall through to ll_parse_type_specs, which builds a TYPE_ENUM.
 */

#include <stdio.h>

enum Color { RED, GREEN = 5, BLUE };

enum Color g_global;        /* zero-initialized enum global */
enum Color g_arr[3];        /* enum array */
enum Color g_init = BLUE;   /* enum const-init value */

enum Color pick(enum Color c)   /* enum param + return */
{
    return c == GREEN ? BLUE : RED;
}

int main(void)
{
    enum Color x = GREEN;
    enum Color y;               /* local, no init */
    y = x + 1;                  /* enum arithmetic on locals */
    enum Color* p = &x;         /* pointer to enum local */
    enum Color a[2];            /* local enum array */
    a[0] = RED;
    a[1] = BLUE;

    printf("x=%d y=%d g=%d pick=%d arr=%d,%d garr=%d,%d,%d\n",
        (int)x, (int)y, (int)g_init, (int)pick(GREEN),
        (int)a[0], (int)a[1], (int)g_arr[0], (int)g_arr[1], (int)g_arr[2]);

    if (x != 5 || y != 6 || g_init != 6) return 1;
    if (pick(GREEN) != 6 || a[1] != 6 || g_arr[2] != 0) return 2;
    if (*p != 5) return 3;
    printf("OK\n");
    return 0;
}
