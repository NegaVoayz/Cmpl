/* test_fnptr_dstar_paren.c — fully-parenthesized double-star function
 * pointer DEFINITIONS: int *((*get_star2(int sel))(int, int)) and the
 * doubly-parenthesized int *(((*get_star3(int sel))(int, int))).
 *
 * The extra paren group pushes the (*name(inner))(outer) rotation down
 * to a nested declarator depth; the leading `int *` must still thread
 * into the innermost return (previously it wrapped the outer function,
 * misclassifying the definition as a fnptr variable — "expected SEMI").
 */
int *addp(int a, int b) { return (int *)(long)(a + b); }
int *subp(int a, int b) { return (int *)(long)(a - b); }

int *((*get_star2(int sel))(int, int)) { return sel ? addp : subp; }
int *(((*get_star3(int sel))(int, int))) { return sel ? addp : subp; }

int main(void)
{
    int rc = 0;

    if (get_star2(1)(2, 3) != (int *)(long)5) rc |= 1;
    if ((*get_star2(0))(10, 3) != (int *)(long)7) rc |= 2;
    if (get_star3(0)(5, 5) != (int *)(long)10) rc |= 4;
    if ((*get_star3(1))(2, 3) != (int *)(long)5) rc |= 8;
    { int *(*sp)(int, int) = get_star2(0);
      if ((*sp)(9, 2) != (int *)(long)7) rc |= 16; }

    return rc;
}
