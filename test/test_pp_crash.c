/* Regression test: preprocessor directive name matching (memcmp in handle_directive).
 * The self-built binary crashed here because const char* pointer types got corrupted
 * in the IR optimizer pipeline (ConstFold simplify/simplify id, SELECT folding, GEP
 * addrspace dumping). */
#define FOO 1
#if FOO
int x = 42;
#else
int x = 0;
#endif

#ifdef FOO
int y = 1;
#else
int y = 0;
#endif

#ifndef BAR
int z = 2;
#endif

#undef FOO
#ifndef FOO
int w = 3;
#endif

int main(void)
{
    /* #if FOO used to evaluate false (macros were not expanded in #if);
     * x must be 42, total must be 48 */
    return (x + y + z + w == 48) ? 0 : 1;
}
