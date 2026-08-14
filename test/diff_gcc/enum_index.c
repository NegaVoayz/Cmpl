/* enum constant as designator index */
enum { B = 3 };
int main(void) { int a[4] = {[B] = 7}; return a[3]; }
