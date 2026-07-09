#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>

Token*
token_new(TokenKind kind, int line, int col)
{
    Token* tok = calloc(1, sizeof(Token));

    tok->kind = kind;
    tok->loc.line = line;
    tok->loc.col = col;
    return tok;
}

void
lexer_append(Lexer* lex, Token* tok)
{
    if (lex->tail) {
        lex->tail->next = tok;
    } else {
        lex->head = tok;
    }
    lex->tail = tok;
}

char
peek(Lexer* lex)
{
    return *lex->cur;
}

char
peek_next(Lexer* lex)
{
    if (*lex->cur == '\0') {
        return '\0';
    }
    return lex->cur[1];
}

void
advance(Lexer* lex)
{
    if (*lex->cur == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    lex->cur++;
}

void
skip_ws_and_comments(Lexer* lex)
{
    while (1) {
        char c = peek(lex);

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance(lex);
        } else if (c == '/' && peek_next(lex) == '/') {
            while (peek(lex) != '\0' && peek(lex) != '\n') {
                advance(lex);
            }
        } else if (c == '/' && peek_next(lex) == '*') {
            advance(lex);
            advance(lex);
            while (peek(lex) != '\0') {
                if (peek(lex) == '*' && peek_next(lex) == '/') {
                    advance(lex);
                    advance(lex);
                    break;
                }
                advance(lex);
            }
        } else {
            break;
        }
    }
}

/* --- public API --- */

Token*
parse(const char* code)
{
    Lexer lex;

    lex.src = code;
    lex.cur = code;
    lex.line = 1;
    lex.col = 1;
    lex.head = NULL;
    lex.tail = NULL;

    while (1) {
        skip_ws_and_comments(&lex);

        char c = peek(&lex);

        if (c == '\0') {
            lexer_append(&lex, token_new(TOK_EOF, lex.line, lex.col));
            break;
        }

        Token* tok = NULL;

        if (isdigit((unsigned char)c)) {
            tok = read_number(&lex);
        } else if (isalpha((unsigned char)c) || c == '_') {
            tok = read_ident_or_keyword(&lex);
        } else if (c == '"' || c == '\'') {
            tok = read_char_or_string(&lex, c);
        } else {
            tok = read_operator(&lex);
        }

        if (tok == NULL) {
            continue;
        }

        lexer_append(&lex, tok);

        if (tok->kind == TOK_ERROR) {
            break;
        }
    }

    return lex.head;
}
