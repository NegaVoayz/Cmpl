/* charstring.c -- character / string literal scanning (read_char_or_string)
 * and escape-sequence decoding, split out of number.c (B-11).
 *
 * The char-literal and string-literal escape switches were near-identical;
 * decode_escape serves both.  read_escape_digits decodes the \x hex and
 * octal digit runs. */

#include "lexer.h"

#include <ctype.h>
#include <string.h>

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

/* decode one escape sequence (cursor just past the backslash): consumes
 * the escape — the letter, the hex/octal digit run, or the unknown char
 * itself — and returns the decoded byte.  Both the char and string paths
 * allow \' and \", \x with any digits, and octal with <= 3 digits. */
static char
decode_escape(Lexer* lex)
{
    char ec = peek(lex);
    switch (ec) {
    case 'n':  advance(lex); return '\n';
    case 't':  advance(lex); return '\t';
    case 'r':  advance(lex); return '\r';
    case 'a':  advance(lex); return '\a';
    case 'b':  advance(lex); return '\b';
    case 'f':  advance(lex); return '\f';
    case 'v':  advance(lex); return '\v';
    case '?':  advance(lex); return '?';
    case '\\': advance(lex); return '\\';
    case '\'': advance(lex); return '\'';
    case '"':  advance(lex); return '"';
    case 'x':
        /* hex escape: any number of digits, masked to 8 bits */
        advance(lex);
        return read_escape_digits(lex, 1);
    default:
        if (ec >= '0' && ec <= '7') {
            /* octal escape: up to 3 digits, value <= 255 */
            return read_escape_digits(lex, 0);
        }
        advance(lex);   /* unknown escape: keep the char */
        return ec;
    }
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
            val = decode_escape(lex);
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
    char* buf = arena_alloc(lex->arena, 64);
    int   len = 0;
    int   cap = 64;

    while (peek(lex) != '"' && peek(lex) != '\0' && peek(lex) != '\n') {
        /* grow before appending (room for this char + the NUL): a long
         * string used to write past the fixed 256-byte arena chunk */
        if (len + 2 >= cap) {
            int ncap = cap * 2;
            char* nb = arena_alloc(lex->arena, ncap);
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
        if (peek(lex) == '\\') {
            advance(lex);
            buf[len++] = decode_escape(lex);
        } else {
            buf[len++] = peek(lex);
            advance(lex);
        }
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
