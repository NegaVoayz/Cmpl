/* deep designator continuation escape (gcc parity):
 *   [1][0][1] = 5, 6  -> c[1][0][1]=5, c[1][1][0]=6  (depth-3 pop)
 *   .a[2] = 5, 6      -> s.a[2]=5, s.b=6              (last-element escape)
 *   .a[1] = 5,6,7,8   -> a[1]=5, a[2]=6, b=7, c=8     (multi-element chain)
 *   (const) .a[1]=5,6,7,8 -> g same shape             */
struct S { int a[3]; int b; int c; };
struct S g = {.a[1] = 5, 6, 7, 8};
int main(void) {
    int c[2][2][2] = {[1][0][1] = 5, 6};
    struct S s = {.a[2] = 5, 6};
    struct S t = {.a[1] = 5, 6, 7, 8};
    return c[1][0][1] + c[1][1][0]
         + s.a[2] + s.b
         + t.a[1] + t.a[2] + t.b + t.c
         + g.a[1] + g.a[2] + g.b + g.c;
    /* 5 + 6 + 5 + 6 + 5 + 6 + 7 + 8 + 5 + 6 + 7 + 8 = 74 */
}
