int test_ptr_diff_local(void) {
    const char* p = (const char*)0x1000;
    const char* q = (const char*)0x2000;
    int d = (int)(p - q);
    return d;
}
