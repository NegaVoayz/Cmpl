/* test_gvn_overflow.c -- GVN table grows past the old 64-entry cap (B-34).
 * One straight-line expression, no stores/calls: 80 distinct muls
 * (x*2 .. x*81) then x*68 again.  The first x*68 sits at table
 * index 66, beyond the silent 64-cap, so pre-fix GVN misses the
 * dup (81 muls); a growable table CSEs it (80 muls).  Correctness
 * holds either way -- the -O1 .ll mul count is the evidence. */

int
f(int x)
{
    return x * 2 + x * 3 + x * 4 + x * 5 + x * 6 + x * 7 + x * 8 + x * 9 + x * 10 + x * 11 + x * 12 + x * 13 + x * 14 + x * 15 + x * 16 + x * 17 + x * 18 + x * 19 + x * 20 + x * 21 + x * 22 + x * 23 + x * 24 + x * 25 + x * 26 + x * 27 + x * 28 + x * 29 + x * 30 + x * 31 + x * 32 + x * 33 + x * 34 + x * 35 + x * 36 + x * 37 + x * 38 + x * 39 + x * 40 + x * 41 + x * 42 + x * 43 + x * 44 + x * 45 + x * 46 + x * 47 + x * 48 + x * 49 + x * 50 + x * 51 + x * 52 + x * 53 + x * 54 + x * 55 + x * 56 + x * 57 + x * 58 + x * 59 + x * 60 + x * 61 + x * 62 + x * 63 + x * 64 + x * 65 + x * 66 + x * 67 + x * 68 + x * 69 + x * 70 + x * 71 + x * 72 + x * 73 + x * 74 + x * 75 + x * 76 + x * 77 + x * 78 + x * 79 + x * 80 + x * 81 + x * 68;
}

int main(void)
{
    return f(7) == 23716 ? 0 : 1;
}
