#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

Token*
read_number(Lexer* lex)
{
    int line = lex->line, col = lex->col;
    char buf[256];
    int  len = 0;
    int  is_float = 0;
    TokenKind kind = TOK_INT_LIT;
    int is_unsigned = 0;  /* integer literal u/U suffix */

    if (peek(lex) == '0' && (peek_next(lex) == 'x' || peek_next(lex) == 'X')) {
        buf[len++] = *lex->cur; advance(lex);
        buf[len++] = *lex->cur; advance(lex);
        while (isxdigit((unsigned char)peek(lex))) {
            buf[len++] = *lex->cur; advance(lex);
        }
        /* C99 hex float: 0x1.8p3, 0x.8p-2 — fraction + binary exponent.
         * strtod() below already parses this form natively. */
        if (peek(lex) == '.') {
            is_float = 1;
            buf[len++] = *lex->cur; advance(lex);
            while (isxdigit((unsigned char)peek(lex))) {
                buf[len++] = *lex->cur; advance(lex);
            }
        }
        if (peek(lex) == 'p' || peek(lex) == 'P') {
            is_float = 1;
            buf[len++] = *lex->cur; advance(lex);
            if (peek(lex) == '+' || peek(lex) == '-') {
                buf[len++] = *lex->cur; advance(lex);
            }
            while (isdigit((unsigned char)peek(lex))) {
                buf[len++] = *lex->cur; advance(lex);
            }
        }
    } else {
        while (isdigit((unsigned char)peek(lex))) {
            buf[len++] = *lex->cur; advance(lex);
        }
        if (peek(lex) == '.') {
            is_float = 1;
            buf[len++] = *lex->cur; advance(lex);
            while (isdigit((unsigned char)peek(lex))) {
                buf[len++] = *lex->cur; advance(lex);
            }
        }
        if (peek(lex) == 'e' || peek(lex) == 'E') {
            is_float = 1;
            buf[len++] = *lex->cur; advance(lex);
            if (peek(lex) == '+' || peek(lex) == '-') {
                buf[len++] = *lex->cur; advance(lex);
            }
            while (isdigit((unsigned char)peek(lex))) {
                buf[len++] = *lex->cur; advance(lex);
            }
        }
    }

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
        int is_long = 0;
        int is_long_long = 0;

        if (peek(lex) == 'u' || peek(lex) == 'U') {
            is_unsigned = 1; advance(lex);
        }
        if (peek(lex) == 'l' || peek(lex) == 'L') {
            advance(lex);
            if (peek(lex) == 'l' || peek(lex) == 'L') {
                is_long_long = 1; advance(lex);
            } else {
                is_long = 1;
            }
        }
        if (!is_unsigned && (peek(lex) == 'u' || peek(lex) == 'U')) {
            is_unsigned = 1; advance(lex);
        }

        if (is_long_long)
            kind = TOK_LONG_LIT;
        else if (is_long)
            kind = TOK_LONG_LIT;
    }

    buf[len] = '\0';
    Token* tok = token_new(lex,kind, line, col);
    tok->is_unsigned = is_unsigned;

    if (is_float) {
        tok->body.float_val = strtod(buf, NULL);
    } else {
        tok->body.int_val = (long long)strtoull(buf, NULL, 0);
    }
    return tok;
}

/* decode an octal (<= 3 digits) or hex (any digits) escape at the
 * cursor (already past the escape letter); consumes the digits and
 * returns the value masked to 8 bits (gcc warns on out-of-range) */
static char
read_escape_digits(Lexer* lex, int is_hex)
{
    int v = 0, nd = 0;
    if (is_hex) {
        while (isxdigit((unsigned char)peek(lex))) {
            char d = *lex->cur;
            v = (v * 16) + (d <= '9' ? d - '0'
                                   : (d | 0x20) - 'a' + 10);
            advance(lex);
        }
    } else {
        while (peek(lex) >= '0' && peek(lex) <= '7' && nd < 3) {
            v = v * 8 + (*lex->cur - '0');
            advance(lex); nd++;
        }
    }
    return (char)(v & 0xFF);
}

Token*
read_char_or_string(Lexer* lex, char quote, int wide, int u8)
{
    int line = lex->line, col = lex->col;

    /* consume the L / u8 prefix (the caller verified it precedes the
     * quote); the quote is consumed below */
    if (wide)
        advance(lex);
    else if (u8) {
        advance(lex);
        advance(lex);
    }
    advance(lex);

    if (quote == '\'') {
        char val = 0;

        if (peek(lex) == '\\') {
            advance(lex);
            char ec = peek(lex);
            switch (ec) {
            case 'n':  val = '\n'; advance(lex); break;
            case 't':  val = '\t'; advance(lex); break;
            case 'r':  val = '\r'; advance(lex); break;
            case 'a':  val = '\a'; advance(lex); break;
            case 'b':  val = '\b'; advance(lex); break;
            case 'f':  val = '\f'; advance(lex); break;
            case 'v':  val = '\v'; advance(lex); break;
            case '?':  val = '?';  advance(lex); break;
            case '\\': val = '\\'; advance(lex); break;
            case '\'': val = '\''; advance(lex); break;
            case '"':  val = '"';  advance(lex); break;
            case 'x':
                advance(lex);
                val = read_escape_digits(lex, 1);
                break;
            default:
                if (ec >= '0' && ec <= '7') {
                    val = read_escape_digits(lex, 0);
                    break;
                }
                val = ec; advance(lex); break;
            }
        } else {
            val = peek(lex); advance(lex);
        }
        if (peek(lex) != '\'') {
            return token_new(lex,TOK_ERROR, line, col);
        }
        advance(lex);
        Token* tok = token_new(lex,TOK_CHAR_LIT, line, col);
        tok->wide = wide;
        /* a wide char literal is an int-sized wchar value */
        if (wide)
            tok->body.int_val = (unsigned char)val;
        else
            tok->body.char_val = val;
        return tok;
    }

    /* string literal */
    char* buf = arena_alloc(lex->arena, 256);
    int   len = 0;

    while (peek(lex) != '"' && peek(lex) != '\0' && peek(lex) != '\n') {
        if (peek(lex) == '\\') {
            advance(lex);
            char ec = peek(lex);
            switch (ec) {
            case 'n':  buf[len++] = '\n'; break;
            case 't':  buf[len++] = '\t'; break;
            case 'r':  buf[len++] = '\r'; break;
            case 'a':  buf[len++] = '\a'; break;
            case 'b':  buf[len++] = '\b'; break;
            case 'f':  buf[len++] = '\f'; break;
            case 'v':  buf[len++] = '\v'; break;
            case '?':  buf[len++] = '?';  break;
            case '\\': buf[len++] = '\\'; break;
            case '"':  buf[len++] = '"';  break;
            case '\'': buf[len++] = '\''; break;
            case 'x':
                /* hex escape: any number of digits, masked to 8 bits */
                advance(lex);
                buf[len++] = read_escape_digits(lex, 1);
                goto escape_done;
            default:
                if (ec >= '0' && ec <= '7') {
                    /* octal escape: up to 3 digits, value <= 255 */
                    buf[len++] = read_escape_digits(lex, 0);
                    goto escape_done;
                }
                buf[len++] = ec;   /* unknown escape: keep the char */
                break;
            }
            advance(lex);
escape_done:
            continue;
        }
        buf[len++] = peek(lex);
        advance(lex);
    }

    if (peek(lex) != '"')
        return token_new(lex, TOK_ERROR, line, col);

    advance(lex);

    buf[len] = '\0';
    Token* tok = token_new(lex, TOK_STRING_LIT, line, col);
    tok->wide = wide;
    tok->u8str = u8;
    tok->body.str_val.data = buf;
    tok->body.str_val.length = len;
    return tok;
}
