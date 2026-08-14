/* brace elision followed by a designator: .z after positional elements
 * (C99 6.7.8p20 — the designator targets the enclosing aggregate) */
int main(void) {
    struct S { int a[3]; int z; };
    struct S s = {1, 2, .z = 7};
    return s.a[1] + s.z;  /* 2 + 7 */
}
