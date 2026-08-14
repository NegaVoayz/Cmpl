/* 2D array with a multi-level [i][j] designator */
int main(void) {
    int a[2][3] = {[1][2] = 5};
    return a[0][0] + a[1][2];  /* 0 + 5 */
}
