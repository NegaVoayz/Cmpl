/* test_fnptr_typedef_pret.c — function-form typedef with a POINTER
 * return used through `*`: typedef int *FP(int); FP *p3 — p3 must be a
 * pointer to a function returning int* (PTR(FUNC(int -> PTR(int)))).
 *
 * Previously `typedef int *FP(int);` never registered FP as a typedef
 * (the PTR-wrapped declarator fell through decl_build_func_def into a
 * silent function declaration), so `FP *p3` became an unresolved
 * PTR(i32): calls returned i32 and (*p3) loaded i8 — invalid IR.
 */
typedef int *FP(int);
FP *gp3;
int gv = 3;

int *mk(int v) { gv = v; return &gv; }

int main(void)
{
    int rc = 0;
    int *r;

    gp3 = mk;
    r = gp3(5);
    if (*r != 5 || gv != 5) rc |= 1;
    r = (*gp3)(6);
    if (*r != 6 || gv != 6) rc |= 2;
    { FP *lp3 = mk;
      r = lp3(7);
      if (*r != 7 || gv != 7) rc |= 4;
      r = (*lp3)(8);
      if (*r != 8 || gv != 8) rc |= 8; }
    if (sizeof(gp3) != sizeof(int *)) rc |= 16;

    return rc;
}
