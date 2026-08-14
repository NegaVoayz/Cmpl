/* by-value struct call */
struct P { int x, y; };
int f(struct P p) { return p.x + p.y; }
int main(void) { struct P p = {5, 6}; return f(p); }
