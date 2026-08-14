/* standalone struct definition inside a function body */
int main(void) {
    struct P { int x; int y; };
    struct P p = {3, 4};
    return p.x + p.y;  /* 7 */
}
