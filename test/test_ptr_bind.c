// Verify C99 declarator binding: int* a, b; gives int* to a, int to b
int test_ptr_bind(void) {
    int* a, b;
    b = 42;
    a = &b;
    return *a;
}
