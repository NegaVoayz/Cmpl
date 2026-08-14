/* struct init, positional */
struct P { int x, y; };
int main(void) { struct P p = {3, 4}; return p.x + p.y; }
