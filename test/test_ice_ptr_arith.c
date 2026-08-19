/* test_ice_ptr_arith.c -- address-constant pointer difference and
 * comparison in constant expressions (file-scope inits + _Static_assert):
 *   garr - garr         == 0
 *   &garr[2] - &garr[0] == 2
 *   garr + 2 - garr     == 2
 *   (char*)&garr[2] - (char*)&garr[0] == 8
 *   garr == garr        == 1
 *   &garr[1] < &garr[3] == 1
 *   &g != &garr         == 1   (distinct objects compare unequal)
 * All same-base; cross-object difference/relational stays rejected (UB).
 * Regression: ice_eval's binary path rejected every ptr-ptr op
 * ("invalid address constant arithmetic") even though gcc folds these to
 * constants.  gcc parity is checked byte-for-byte by Stage C.
 */
#include <stdio.h>

int garr[4] = {1, 2, 3, 4};
int g = 7;

long d1 = &garr[2] - &garr[0];
long d2 = garr + 2 - garr;
long d3 = (char*)&garr[2] - (char*)&garr[0];
int e1 = garr == garr;
int e2 = &garr[1] < &garr[3];
int e3 = &garr[2] >= &garr[0];
int e4 = &g != &garr;

_Static_assert(&garr[2] - &garr[0] == 2, "diff");
_Static_assert(garr == garr, "same");
_Static_assert(garr + 2 - garr == 2, "decay diff");
_Static_assert(&g != &garr, "distinct");

int main(void)
{
    if (d1 != 2) return 1;
    if (d2 != 2) return 2;
    if (d3 != 8) return 3;
    if (!e1 || !e2 || !e3 || !e4) return 4;

    printf("ice_ptr_arith: %ld %ld %ld %d %d %d %d\n",
           d1, d2, d3, e1, e2, e3, e4);
    return 0;
}
