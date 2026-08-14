/* multi-step designator continuation escape (gcc parity):
 *   [1][1] = 5, 6  -> m[1][1]=5, m[2][0]=6            (int m[3][2])
 *   .a[1] = 7,8,9  -> s.a[1]=7, s.a[2]=8, s.b=9        (runtime)
 *   .a[1] = 10,20,30 -> g.a[1]=10, g.a[2]=20, g.b=30   (const) */
struct S { int a[3]; int b; };
struct S g = {.a[1] = 10, 20, 30};
int main(void) {
    int m[3][2] = {[1][1] = 5, 6};
    struct S s = {.a[1] = 7, 8, 9};
    return m[1][1] + m[2][0] + s.a[1] + s.a[2] + s.b + g.a[1] + g.a[2] + g.b;
    /* 5 + 6 + 7 + 8 + 9 + 10 + 20 + 30 = 95 */
}
