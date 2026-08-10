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

    if (peek(lex) == '0' && (peek_next(lex) == 'x' || peek_next(lex) == 'X')) {
        buf[len++] = *lex->cur; advance(lex);
        buf[len++] = *lex->cur; advance(lex);
        while (isxdigit((unsigned char)peek(lex))) {
            buf[len++] = *lex->cur; advance(lex);
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
        }
    } else {
        /* handle integer suffixes: U, L, UL, LU, LL, ULL, LLU */
        int is_unsigned = 0;
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

    if (is_float) {
        tok->body.float_val = strtod(buf, NULL);
    } else {
        tok->body.int_val = strtol(buf, NULL, 0);
    }
    return tok;
}

Token*
read_char_or_string(Lexer* lex, char quote)
{
    int line = lex->line, col = lex->col;
    advance(lex);

    if (quote == '\'') {
        char val = 0;

        if (peek(lex) == '\\') {
            advance(lex);
            switch (peek(lex)) {
            case 'n':  val = '\n'; advance(lex); break;
            case 't':  val = '\t'; advance(lex); break;
            case 'r':  val = '\r'; advance(lex); break;
            case '\\': val = '\\'; advance(lex); break;
            case '\'': val = '\''; advance(lex); break;
            case '"':  val = '"';  advance(lex); break;
            case '0':  val = '\0'; advance(lex); break;
            case 'x': {
                advance(lex);
                char hex[3] = {0}; int h = 0;
                while (isxdigit((unsigned char)peek(lex)) && h < 2) {
                    hex[h++] = *lex->cur; advance(lex);
                }
                val = (char)strtol(hex, NULL, 16);
                break;
            }
            default: val = peek(lex); advance(lex); break;
            }
        } else {
            val = peek(lex); advance(lex);
        }
        if (peek(lex) != '\'') {
            return token_new(lex,TOK_ERROR, line, col);
        }
        advance(lex);
        Token* tok = token_new(lex,TOK_CHAR_LIT, line, col);
        tok->body.char_val = val;
        return tok;
    }

    /* string literal */
    char* buf = arena_alloc(lex->arena, 256);
    int   len = 0;

    while (peek(lex) != '"' && peek(lex) != '\0' && peek(lex) != '\n') {
        if (peek(lex) == '\\') {
            advance(lex);
            switch (peek(lex)) {
            case 'n':  buf[len++] = '\n'; break;
            case 't':  buf[len++] = '\t'; break;
            case 'r':  buf[len++] = '\r'; break;
            case '\\': buf[len++] = '\\'; break;
            case '"':  buf[len++] = '"';  break;
            case '\'': buf[len++] = '\''; break;
            case '0':  buf[len++] = '\0'; break;
            default:   buf[len++] = peek(lex); break;
            }
        } else {
            buf[len++] = peek(lex);
        }
        advance(lex);
    }

    if (peek(lex) != '"')
        return token_new(lex, TOK_ERROR, line, col);

    advance(lex);

    buf[len] = '\0';
    Token* tok = token_new(lex, TOK_STRING_LIT, line, col);
    tok->body.str_val.data = buf;
    tok->body.str_val.length = len;
    return tok;
}
