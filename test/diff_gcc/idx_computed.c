/* [i] designator with a computed (constant-folded) index */
int main(void) {
    int a[4] = {[1 + 2] = 9};
    return a[3];  /* 9 */
}
