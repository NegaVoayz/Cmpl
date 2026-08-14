/* struct init, brace-elided array member */
struct A { char a[3]; };
int main(void) { struct A a = {'a', 'b', 'c'}; return a.a[2] - a.a[0]; }
