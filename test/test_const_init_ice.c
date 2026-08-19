/* test_const_init_ice.c -- constant initializers beyond bare literals.
 *
 * gen_const_init only handled literals, idents, casts and unary +-/~/&
 * directly; sizeof, _Alignof, ternary selections, arithmetic over
 * constants and mixed float/int in file-scope/static initializers fell
 * into the silent default fallback and emitted 0 (wrong code — e.g.
 * `int* p = &g;` initialized NULL).  These are now evaluated with the
 * same integer-constant-expression semantics as _Static_assert
 * (ir_gen_sa.c), shared through ir_gen_const_ice.c; enum value exprs
 * (opt_enum + register_enum_def) and constant array bounds
 * (int a[sizeof(int)*2], resolve_array_sizes) use the same evaluator.
 *
 * Negative cases (kept in probes, not here — every test/*.c must
 * compile): runtime-variable bounds still fail loudly ("array bound is
 * not a constant expression"); pointer arith like `&g + 0` in a
 * file-scope init is still unsupported.
 */

/* --- sizeof / _Alignof in const-init values --- */
int g_sz = sizeof(int);                        /* 4 */
int g_sz2 = sizeof(int) + sizeof(char);        /* 5 */
int g_al = _Alignof(double);                   /* 8 */
int g_szptr = sizeof(int*);                    /* 8 */
int g_neg = -sizeof(char);                     /* -1 */
unsigned long g_u = sizeof(int) * 100UL;       /* 400 */

/* --- ternary with constant condition --- */
int g_tern = 1 ? 2 : 3;                        /* 2 */
int g_tern2 = (5 > 3) ? 10 : 20;               /* 10 */
int g_tern3 = 0 ? 1 : -1;                      /* -1 */
int g_mixcond = sizeof(int) ? 7 : 8;           /* 7 */

/* --- arithmetic over constants --- */
int g_charadd = 'a' + 1;                       /* 98 */
long g_long = 1L << 40;                        /* 1099511627776 */
int g_cast = (int)(sizeof(int) * 2.5);         /* 10 (float result) */

/* --- mixed float/int arithmetic --- */
double g_f1 = 2 * 1.5;                         /* 3.0 */
double g_f2 = 1.5 + 2;                         /* 3.5 */
double g_f3 = sizeof(int) + 0.5;               /* 4.5 */
float  g_f4 = 3 / 2.0f;                        /* 1.5 */
double g_ff = 1.5 + 2.5;                       /* 4.0 (opt-folded) */
int    g_fi = 2 * 1.5;                         /* 3 (truncated) */

/* --- &global / &function pointer initializers --- */
int g_target = 42;
int* g_ptr = &g_target;                        /* ptr @g_target */
static int helper(void) { return 1; }
int (*g_fp)(void) = &helper;                   /* ptr @helper */

/* --- enum values from non-literal exprs (opt_enum cannot fold) --- */
enum { ESZ = sizeof(int) };                    /* 4 */
enum { E1 = 10, E2 = E1 + sizeof(char) };      /* E1 = 10, E2 = 11 */
enum { E3 = ESZ * 2 };                         /* 8 */
int g_enum_sz = ESZ;                           /* 4 */
int g_enum_arith = ESZ + 1;                    /* 5 */
int g_enum_tern = E2 ? 3 : 4;                  /* 3 */
int g_enum_e3 = E3;                            /* 8 */

/* --- constant array bounds from sizeof/enum/ternary exprs --- */
int arr_sz[sizeof(int) * 2];                   /* 8 */
int arr_enum[ESZ * 2];                         /* 8 */
int arr_tern[(1 ? 3 : 4) + 1];                 /* 4 */
struct SM { int a[sizeof(int) * 2]; };         /* member bound 8 */
int g_structsz = sizeof(struct SM);            /* 32 */

int main(void)
{
    if (g_sz != 4) return 1;
    if (g_sz2 != 5) return 2;
    if (g_al != 8) return 3;
    if (g_szptr != 8) return 4;
    if (g_neg != -1) return 5;
    if (g_u != 400UL) return 6;
    if (g_tern != 2) return 7;
    if (g_tern2 != 10) return 8;
    if (g_tern3 != -1) return 9;
    if (g_mixcond != 7) return 10;
    if (g_charadd != 98) return 11;
    if (g_long != 1099511627776LL) return 12;
    if (g_cast != 10) return 13;
    if (g_f1 != 3.0) return 14;
    if (g_f2 != 3.5) return 15;
    if (g_f3 != 4.5) return 16;
    if (g_f4 != 1.5f) return 17;
    if (g_ff != 4.0) return 18;
    if (g_fi != 3) return 19;
    if (g_ptr != &g_target) return 20;
    if (*g_ptr != 42) return 21;
    if (g_fp != &helper) return 22;
    if ((*g_fp)() != 1) return 23;
    if (g_enum_sz != 4) return 24;
    if (g_enum_arith != 5) return 25;
    if (g_enum_tern != 3) return 26;
    if (g_enum_e3 != 8) return 27;
    if (sizeof(arr_sz) != 32) return 28;
    if (sizeof(arr_enum) != 32) return 29;
    if (sizeof(arr_tern) != 16) return 30;
    if (g_structsz != 32) return 31;

    _Static_assert(sizeof(int) == 4, "sizeof(int)");
    _Static_assert(ESZ == 4, "ESZ");
    _Static_assert(E3 == 8, "E3");
    return 0;
}
