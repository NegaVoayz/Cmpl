/* number.c -- numeric literal scanning (read_number).
 *
 * The char/string literal reader (read_char_or_string + escape decoding)
 * moved to charstring.c (B-11).  read_number's mantissa scan and the
 * integer-suffix parsing live in the static helpers below. */

#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* scan the mantissa of a number literal into buf: hex form (0x... with
 * optional fraction + binary exponent) or decimal (digits, optional
 * fraction, optional decimal exponent).  sets *is_float when a fraction
 * or exponent was seen; accumulates the integer VALUE into *v during
 * the integer-part scan (base 16/8/10 by prefix), so read_number need
 * not re-parse buf with strtoull. */
static void
read_number_mantissa(Lexer* lex, char* buf, int* len, int* is_float,
                     unsigned long long* v)
{
    if (peek(lex) == '0' && (peek_next(lex) == 'x' || peek_next(lex) == 'X')) {
        buf[(*len)++] = *lex->cur; advance(lex);
        buf[(*len)++] = *lex->cur; advance(lex);
        while (isxdigit((unsigned char)peek(lex))) {
            char c = *lex->cur;

            buf[(*len)++] = c; advance(lex);
            *v = *v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        /* C99 hex float: 0x1.8p3, 0x.8p-2 — fraction + binary exponent.
         * strtod() below already parses this form natively. */
        if (peek(lex) == '.') {
            *is_float = 1;
            buf[(*len)++] = *lex->cur; advance(lex);
            while (isxdigit((unsigned char)peek(lex))) {
                buf[(*len)++] = *lex->cur; advance(lex);
            }
        }
        if (peek(lex) == 'p' || peek(lex) == 'P') {
            *is_float = 1;
            buf[(*len)++] = *lex->cur; advance(lex);
            if (peek(lex) == '+' || peek(lex) == '-') {
                buf[(*len)++] = *lex->cur; advance(lex);
            }
            while (isdigit((unsigned char)peek(lex))) {
                buf[(*len)++] = *lex->cur; advance(lex);
            }
        }
    } else {
        int base = (peek(lex) == '0' &&
                    isdigit((unsigned char)peek_next(lex))) ? 8 : 10;
        int ok = 1;   /* still inside the valid octal prefix */

        while (isdigit((unsigned char)peek(lex))) {
            char c = *lex->cur;

            buf[(*len)++] = c; advance(lex);
            /* invalid octal digit: strtoull stops the VALUE at it; latch
             * ok so later valid digits are skipped too */
            if (base == 8 && (c < '0' || c > '7')) ok = 0;
            if (ok) *v = *v * base + (c - '0');
        }
        if (peek(lex) == '.') {
            *is_float = 1;
            buf[(*len)++] = *lex->cur; advance(lex);
            while (isdigit((unsigned char)peek(lex))) {
                buf[(*len)++] = *lex->cur; advance(lex);
            }
        }
        if (peek(lex) == 'e' || peek(lex) == 'E') {
            *is_float = 1;
            buf[(*len)++] = *lex->cur; advance(lex);
            if (peek(lex) == '+' || peek(lex) == '-') {
                buf[(*len)++] = *lex->cur; advance(lex);
            }
            while (isdigit((unsigned char)peek(lex))) {
                buf[(*len)++] = *lex->cur; advance(lex);
            }
        }
    }
}

/* consume integer literal suffixes (U, L, UL, LU, LL, ULL, LLU); sets
 * *is_unsigned and returns whether a long/long-long suffix was seen. */
static int
read_int_suffix(Lexer* lex, int* is_unsigned)
{
    int is_long = 0;
    int is_long_long = 0;

    if (peek(lex) == 'u' || peek(lex) == 'U') {
        *is_unsigned = 1; advance(lex);
    }
    if (peek(lex) == 'l' || peek(lex) == 'L') {
        advance(lex);
        if (peek(lex) == 'l' || peek(lex) == 'L') {
            is_long_long = 1; advance(lex);
        } else {
            is_long = 1;
        }
    }
    if (!*is_unsigned && (peek(lex) == 'u' || peek(lex) == 'U')) {
        *is_unsigned = 1; advance(lex);
    }
    return is_long || is_long_long;
}

Token*
read_number(Lexer* lex)
{
    int line = lex->line, col = lex->col;
    char buf[256];
    int  len = 0;
    int  is_float = 0;
    TokenKind kind = TOK_INT_LIT;
    int is_unsigned = 0;  /* integer literal u/U suffix */
    unsigned long long v = 0;

    read_number_mantissa(lex, buf, &len, &is_float, &v);

    if (is_float) {
        kind = TOK_DOUBLE_LIT;
        if (peek(lex) == 'f' || peek(lex) == 'F') {
            kind = TOK_FLOAT_LIT; advance(lex);
        } else if (peek(lex) == 'l' || peek(lex) == 'L') {
            /* long-double suffix: we have no f80 type, so keep the
             * double literal but consume the suffix (was a stray
             * TOK_IDENT 'L' → syntax error). */
            advance(lex);
        }
    } else {
        /* handle integer suffixes: U, L, UL, LU, LL, ULL, LLU */
        if (read_int_suffix(lex, &is_unsigned))
            kind = TOK_LONG_LIT;
    }

    buf[len] = '\0';
    Token* tok = token_new(lex,kind, line, col);
    tok->is_unsigned = is_unsigned;

    if (is_float) {
        tok->body.float_val = strtod(buf, NULL);
    } else {
        tok->body.int_val = (long long)v;
    }
    return tok;
}
