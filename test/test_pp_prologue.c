/* test_pp_prologue.c -- pp_read_ident: ws-skip + ident-scan prologue (B-30).
 *
 * Regression for B-30: exercises every directive path that parses an
 * identifier: #define (object + function-like), #undef, #ifdef/#ifndef,
 * defined(X) and defined NAME, function-like params, and `#`-stringize.
 * Each conditional sets a GOT_* macro main() verifies, so a mis-parsed
 * directive flips a flag and fails the run. */

#define STR(x)   #x
#define PASTE(x) STR(x)
#define F(a, b)  ((a) * 100 + (b))
#define ALPHA    1
#define BETA     2

#undef NOT_DEFINED              /* no-op: name absent from the macro table */

#ifdef ALPHA
#define GOT_ALPHA 1
#else
#define GOT_ALPHA 0
#endif

#ifndef ALPHA
#define GOT_ALPHA_NEG 1
#else
#define GOT_ALPHA_NEG 0
#endif

#if defined(BETA)
#define GOT_BETA_PAREN 1
#else
#define GOT_BETA_PAREN 0
#endif

#if defined BETA
#define GOT_BETA_NOPAREN 1
#else
#define GOT_BETA_NOPAREN 0
#endif

#if defined GAMMA
#define GOT_GAMMA 1
#else
#define GOT_GAMMA 0
#endif

int main(void)
{
    const char* s = PASTE(foo);   /* "foo" -- exercises handle_stringize */
    int z = F(1, 2);              /* 102 -- exercises function-like args */

    return (GOT_ALPHA == 1 && GOT_ALPHA_NEG == 0
            && GOT_BETA_PAREN == 1 && GOT_BETA_NOPAREN == 1
            && GOT_GAMMA == 0
            && s[0] == 'f' && s[1] == 'o' && s[2] == 'o' && s[3] == '\0'
            && z == 102) ? 0 : 1;
}
