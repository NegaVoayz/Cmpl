/* test_struct_fwd_ref.c -- struct tag referenced as a pointer inside
 * another struct BEFORE its own definition.
 *
 * Regression test for a self-hosting bug: the struct dedup cache keyed
 * the forward-declared (incomplete) struct by name, so the later complete
 * definition was shadowed -- sizeof() came back 0 and field access
 * lowered to undef, breaking sym_scope_pop in the self-built compiler.
 */

typedef struct Holder {
    struct Node* first;
} Holder;

typedef struct Node {
    int          val;
    struct Node* next;
} Node;

int main(void)
{
    Node a, b;
    Holder h;

    a.val = 10;
    a.next = &b;
    b.val = 20;
    b.next = 0;

    h.first = &a;

    if (sizeof(Node) == 0) return 1;   /* incomplete struct shadowed the def */
    if (h.first == 0) return 2;
    if (h.first->val != 10) return 3;  /* field access through forward-ref ptr */
    if (h.first->next->val != 20) return 4;

    return 0;
}
