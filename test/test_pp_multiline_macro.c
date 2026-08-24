/* test_pp_multiline_macro.c — function-like macro invocation spanning
 * physical lines must not corrupt the source text.
 *
 * The pp is line-based and does not join an unclosed invocation across
 * lines, but the unclosed-invocation path used to return an ABSOLUTE
 * offset that the caller added to the identifier start again — silently
 * eating the macro name, the '(' and several characters of the first
 * argument (E3(FOO_BAR, ...) became "_BAR, ...").  That mangled real
 * source without any diagnostic.
 *
 * With the fix the invocation is left unexpanded but intact.  ADD3 is
 * also a real function (prototype below, BEFORE the macro definition so
 * it is not itself macro-expanded), so the unexpanded call still
 * computes 1+2+3 and the run passes either way — the test fails (parse
 * error or wrong result) only if the text is corrupted. */

int ADD3(int a, int b, int c)
{
    return a + b + c;
}

/* defined AFTER the function so the definition is not macro-expanded */
#define ADD3(a, b, c) ((a) + (b) + (c))

int main(void)
{
    int r = ADD3(1, 2,
                 3);

    return r == 6 ? 0 : 1;
}
