/* Test: multi-declarator with initializers and ternary expression.
 * Verifies:
 *   - int a = 1, b = 2; works inside functions
 *   - ternary expression in initializer before comma works
 *   - extern int x, y, z; works at top level with multi-globals
 */
extern int g_x, g_y, g_z;

struct Foo { int val; };

int test_multivar(void) {
    struct Foo *a = 0, *b = 0;
    int sum = g_x + g_y + g_z;

    /* ternary in initializer with comma declarator after */
    int t = sum ? 1 : 0, dummy = 0;

    (void)dummy;
    return t + sum;
}
