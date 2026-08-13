/* test_bool_store.c -- boolean and narrow-int stores must widen to int
 *
 * Regression test: `int x = (a == b);` used to emit `store i1 ...` into
 * an i32 slot (leaving 3 garbage bytes), and `x = c` (char) stored i8
 * into an i32 slot. Both are fixed by widening the value to the slot
 * type (zext) in ir_build_store. Without the fix, the compiler's own
 * parser read garbage for `inside_sizeof` and crashed in
 * lr1_parse_expr (SEGV reading address 0x4).
 *
 * Run: cmpl -emit-llvm test/test_bool_store.c -o t.ll
 *      clang -c --target=x86_64-pc-linux-gnu t.ll -o /dev/null
 *      clang t.ll -o t && ./t        # must exit 0
 */

/* assignment: i1 -> i32 store */
int bool_to_int(int a, int b) {
    int x;
    x = (a == b);
    return x;
}

/* initializer: i1 -> i32 store */
int bool_init(int a, int b) {
    int x = (a != b);
    return x;
}

/* assignment: i8 -> i32 store */
int char_to_int(char c) {
    int x;
    x = c;
    return x;
}

/* initializer: i16 -> i32 store */
int short_init(short s) {
    int x = s;
    return x;
}

int main(void) {
    if (bool_to_int(3, 3) != 1) return 1;
    if (bool_to_int(1, 2) != 0) return 2;
    if (bool_init(5, 5) != 0) return 3;
    if (bool_init(1, 2) != 1) return 4;
    if (char_to_int(65) != 65) return 5;
    if (char_to_int(0) != 0) return 6;
    if (short_init(1000) != 1000) return 7;
    if (short_init(0) != 0) return 8;
    return 0;
}
