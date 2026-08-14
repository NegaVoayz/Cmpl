/* test_paren_multiply.c -- parenthesized multiplication must not be
 * misparsed as a cast.
 *
 * is_cast_start's (IDENT*) heuristic treated `(a * b)` as a cast to
 * type `a*`, so every parenthesized multiply failed to parse.  The
 * fix scans the star chain: (T*)x / (T**)x end at ')', (a * b) does
 * not.  Regression covers plain parens, parens in larger expressions,
 * pointer casts, and typedef-pointer casts.
 */
typedef char Byte;

int main(void)
{
    char buf[8];
    char* p = (char*)buf;
    Byte* q = (Byte*)buf;
    int a = 3, b = 4;
    int x = (a * b);
    int y = (a * b) + 1;
    int z = 2 * (a * b);
    int w = (a * b) * 5;

    if (x != 12) return 1;
    if (y != 13) return 2;
    if (z != 24) return 3;
    if (w != 60) return 4;
    if (p != buf) return 5;
    if (q != buf) return 6;

    return 0;
}
