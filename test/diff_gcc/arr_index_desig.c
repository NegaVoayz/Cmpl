/* array-index designator [i] = v */
int main(void) { int a[3] = {[2] = 9}; return a[2]; }
