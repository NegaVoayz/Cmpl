/* test_comma_ternary.c -- comma expressions must yield the RIGHT
 * operand's value while evaluating the left for side effects (C11 6.5.17).
 *
 * Regression for a self-hosting wrong-code bug: gen_binary_op's default
 * case returned the LEFT operand for TOK_COMMA, so
 * `cond ? (p++, 7) : 14` produced the pre-increment pointer instead of 7.
 * pp_eval.c's lexer uses exactly this idiom for two-char operators
 * (l->kind = (*l->p == '&') ? (l->p++, TK_AMPAMP) : TK_AMP;), so in the
 * self-built compiler &&, ||, ==, != mis-lexed and every multi-operator
 * #if evaluated false (see test_pp_if_macro).  Constant comma also folded
 * to 0 (fold_binary_int default) instead of the right operand.
 */

int lexlike(char** p)
{
    return (**p == '&') ? ((*p)++, 7) : 14;
}

int main(void)
{
    char buf[4] = "&&x";
    char* p = buf;
    int a = lexlike(&p);
    int b = (p == buf + 1);          /* side effect happened exactly once */
    char buf2[2] = "!y";
    char* r = buf2;
    int d = lexlike(&r);             /* '!' -> else branch -> 14 */

    int c = (1, 2, 3);               /* plain comma chain -> 3 */
    int e = (1 ? (2, 9) : 0);        /* comma in ternary then -> 9 */
    int f = (0 ? 0 : (3, 11));       /* comma in ternary else -> 11 */
    const int g = (7, 8);            /* constant comma -> 8 */

    if (a != 7) return 1;
    if (b != 1) return 2;
    if (d != 14) return 3;
    if (c != 3) return 4;
    if (e != 9) return 5;
    if (f != 11) return 6;
    if (g != 8) return 7;
    return 0;
}
