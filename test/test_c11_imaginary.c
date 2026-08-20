/* test_c11_imaginary.c — C11 _Imaginary specifier acceptance (parse only).
 * gcc does not implement _Imaginary, so this is NOT a differential test and
 * deliberately has no main(): Stage B compiles it with cmpl and validates the
 * IR with clang -c, while Stage C / diff_o1 / diff_gcc skip files without
 * main.  The IR has no imaginary arithmetic, so _Imaginary maps to the
 * underlying real floating type, exactly like _Complex.
 */

_Imaginary double g_im = 3.0;

_Imaginary double halve(_Imaginary double z) { return z * 0.5; }

double use_imaginary(double x)
{
    _Imaginary double local = x + 1.0;

    return local + halve(g_im);
}
