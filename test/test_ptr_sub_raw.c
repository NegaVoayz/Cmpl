#include <stddef.h>
ptrdiff_t test_raw(const char* p, const char* q) {
    return p - q;
}
