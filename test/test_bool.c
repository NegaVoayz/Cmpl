/* test_bool.c -- C99 _Bool end-to-end: keyword, type, and 0/1 store
 * semantics.  Regression for _Bool previously lexing as a typedef name and
 * storing the raw int value (_Bool b = 5 stored 5, gcc stores 1).
 *
 * Run:
 *   cmpl -emit-llvm -Iinclude -I. -o /tmp/b.ll test/test_bool.c
 *   clang /tmp/b.ll -o /tmp/b && /tmp/b; echo $?
 */

_Bool nonzero(int x) { return x; }
_Bool pass(_Bool v)  { return v; }

int main(void)
{
    _Bool b = 5;
    _Bool c = (1 > 2);
    _Bool d = (_Bool)7;
    _Bool e = (_Bool)6;

    if (b != 1) return 1;
    if (c != 0) return 2;
    if (d != 1) return 3;
    if (e != 1) return 4;
    if (sizeof(_Bool) != 1) return 5;
    if (nonzero(0) != 0) return 6;
    if (nonzero(3) != 1) return 7;
    if (pass(1) != 1) return 8;
    if (pass(0) != 0) return 9;
    return 0;
}
