/* test_struct_decl.c -- struct/union declarations and member access
 *
 * Regression for two silent-miscompilation bugs:
 *  1. assignments to members of GLOBAL struct variables were dropped
 *     (gen_store_ptr's ident path only accepted pointer-typed symbols,
 *     but globals carry the struct type directly) -> s.a = 3 vanished
 *     and the field stayed zero.
 *  2. local `struct X v;` / `union U u;` declarations produced an
 *     unsized IR_STRUCT (struct refs were only resolved on top-level
 *     decls) -> clang rejected the alloca.
 */

struct Point { int x; int y; };
struct Point g_p;

union Val { int i; long l; };
union Val g_v;

struct Box { int tag; union { int i; long l; } u; };

int main(void)
{
    struct Point p;      /* local named struct */
    union Val v;         /* local named union */
    struct Box b;        /* local struct with anonymous union member */

    g_p.x = 3;
    g_p.y = 4;
    p.x = 5;
    p.y = 6;
    v.i = 7;
    g_v.i = 8;
    b.tag = 1;
    b.u.i = 9;

    return (g_p.x + g_p.y + p.x + p.y + v.i + g_v.i + b.tag + b.u.i == 43)
        ? 0 : 1;
}
