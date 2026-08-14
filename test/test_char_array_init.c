/* test_char_array_init.c -- char arrays initialized from string literals
 * must receive the string BYTES, zero-padded, not the string pointer
 * (C11 6.7.9p14/p21).  Previously cmpl emitted `alloca [4 x i8]` +
 * `store ptr @.str, ptr %0` (the pointer value written into the array
 * slot), corrupting the stack for locals and emitting invalid IR for
 * globals.  Unsized arrays (char a[] = "s") were never size-inferred. */

char glob_sized[4]   = "G1";
char glob_unsized[]  = "G2";
char glob_trunc[2]   = "abc";    /* NUL dropped; gcc warns */

struct S { char tag[4]; int n; };

int main(void)
{
    char loc_sized[4] = "x";
    char loc_unsized[] = "yz";
    char loc_trunc[2] = "abc";
    char brace[4] = {"xy"};
    char* p = loc_sized;
    struct S s = { "S1", 7 };
    struct S s2 = { "S2", 120, 7 };  /* 7 is excess; gcc drops it */

    if (loc_sized[0] != 'x' || loc_sized[1] != 0 || loc_sized[3] != 0) return 1;
    if (p[0] != 'x') return 2;
    if (loc_unsized[0] != 'y' || loc_unsized[1] != 'z' ||
        loc_unsized[2] != 0) return 3;
    if (sizeof(loc_unsized) != 3) return 4;
    if (loc_trunc[0] != 'a' || loc_trunc[1] != 'b') return 5;
    if (glob_sized[0] != 'G' || glob_sized[1] != '1' || glob_sized[2] != 0) return 6;
    if (glob_unsized[0] != 'G' || glob_unsized[1] != '2' ||
        glob_unsized[2] != 0) return 7;
    if (sizeof(glob_unsized) != 3) return 8;
    if (glob_trunc[0] != 'a' || glob_trunc[1] != 'b') return 9;
    if (s.tag[0] != 'S' || s.tag[1] != '1' || s.tag[2] != 0 || s.n != 7) return 10;
    if (brace[0] != 'x' || brace[1] != 'y' || brace[2] != 0) return 11;
    if (s2.tag[0] != 'S' || s2.tag[1] != '2' || s2.tag[2] != 0 ||
        s2.n != 120) return 12;

    return 0;
}
