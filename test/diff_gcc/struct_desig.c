/* struct init, .field designators (reordered) */
struct S { int a; int b; };
int main(void) { struct S s = {.b = 5, .a = 1}; return s.b - s.a; }
