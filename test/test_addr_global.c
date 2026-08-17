/* test_addr_global.c — address-of on GLOBAL variables.
 *
 * &g on a global must yield the global's address (ptr @g), not an
 * inttoptr of its element value: gen_addr_of returned the element-typed
 * VAL_GLOBAL, and the caller's coercion emitted `inttoptr i32 @gv to
 * ptr`, which clang rejects ("global variable reference must have
 * pointer type").  Covers &int-global (returned/stored/deref'd through
 * a pointer), &array element, &struct member, and a global pointer var.
 */
int gv = 7;
int garr[4] = {1, 2, 3, 4};
struct P { int x; int y; } gst = {5, 6};
int *gp;

int *fg(void) { return &gv; }
int *farr(void) { return &garr[2]; }
int *fst(void) { return &gst.y; }
int *fmut(void) { int *p = &gv; *p = 9; return p; }
void fsetgp(void) { gp = &gv; }

int main(void)
{
    int rc = 0;

    if (*fg() != 7) rc |= 1;
    if (*farr() != 3) rc |= 2;
    if (*fst() != 6) rc |= 4;
    if (*fmut() != 9) rc |= 8;
    if (gv != 9) rc |= 16;
    fsetgp();
    if (gp != &gv) rc |= 32;
    if (*gp != 9) rc |= 64;

    return rc;
}
