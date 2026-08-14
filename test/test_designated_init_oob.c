/* test_designated_init_oob.c -- out-of-range [i] designator index is
 * diagnosed and the invalid store is skipped (slot stays zero) rather
 * than emitted as an out-of-bounds GEP.  gcc rejects int a[3] = {[5] = 1}.
 */
#include <stdio.h>

int gi[3] = {[5] = 1};       /* const path: OOB diagnosed + skipped */

int main(void)
{
    int rc = 0;

    int a[3] = {[5] = 1};    /* runtime path: OOB diagnosed + skipped */
    if (a[0] != 0 || a[1] != 0 || a[2] != 0)
        { printf("a %d %d %d\n", a[0], a[1], a[2]); rc |= 1; }

    int b[3] = {[1] = 4};    /* in-range control */
    if (b[0] != 0 || b[1] != 4 || b[2] != 0)
        { printf("b %d %d %d\n", b[0], b[1], b[2]); rc |= 2; }

    if (gi[0] != 0 || gi[1] != 0 || gi[2] != 0)
        { printf("gi %d %d %d\n", gi[0], gi[1], gi[2]); rc |= 4; }

    if (!rc) printf("designated oob OK\n");
    return rc;
}
