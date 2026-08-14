/* test_cast_deref.c -- *(type*)p must deref the cast pointer, not cast *p
 *
 * Regression test: `*(int*)p` used to parse as `(int*)(*p)` (CAST wrapping
 * UNARY-*), which emitted `load i32` then `inttoptr i32 to ptr` and returned
 * garbage.  It must parse as `*( (int*)p )` (UNARY-* over CAST) and return
 * the pointed-to value, matching gcc/clang.
 */
int main(void)
{
    int x = 5;
    int* p = &x;

    /* redundant cast: *(int*)p must still yield 5 */
    int a = *(int*)p;

    /* void* -> int* -> deref */
    void* vp = &x;
    int c = *(int*)vp;

    /* cast over address-of: low byte of x=5 (little-endian x86_64) */
    unsigned char b = *(unsigned char*)&x;

    /* cast over & over index: y[2] == 3 */
    char y[4] = {1, 2, 3, 4};
    unsigned char d = *(unsigned char*)&y[2];

    if (a != 5) return 1;
    if (c != 5) return 2;
    if (b != 5) return 3;
    if (d != 3) return 4;

    return 0;
}
