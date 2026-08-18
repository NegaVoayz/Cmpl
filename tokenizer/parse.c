#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Token*
token_new(Lexer* lex, TokenKind kind, int line, int col)
{
    Token* tok = arena_alloc(lex->arena, sizeof(Token));

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

/* Merge adjacent string literals ("a" "b" -> "ab").
 * C11 5.1.1.2 Translation phase 6: adjacent string literal tokens are
 * concatenated, but only when they carry the SAME prefix (C11 6.4.5p5:
 * "identically prefixed"); a prefix mismatch (L"a" "b", u8"a" L"b") is a
 * constraint violation and fails the compile.  Uses arena for merged
 * string data.  Returns 1 on a prefix mismatch (message already
 * printed). */
static int
merge_adjacent_strings(Token* head, Arena* a)
{
    Token* prev = head;

    while (prev) {
        if (prev->kind != TOK_STRING_LIT) {
            prev = prev->next;
            continue;
        }

        Token* cur = prev->next;

        while (cur && cur->kind == TOK_STRING_LIT) {
            if (prev->wide != cur->wide || prev->u8str != cur->u8str) {
                fprintf(stderr, "tokenizer: adjacent string literals with "
                        "different prefixes at line %d (C11 6.4.5p5)\n",
                        cur->loc.line);
                return 1;
            }
            /* merge prev and cur: arena-allocate the joined string */
            int new_len = prev->body.str_val.length + cur->body.str_val.length;
            char* new_data = arena_alloc(a, new_len + 1);

            memcpy(new_data, prev->body.str_val.data, prev->body.str_val.length);
            memcpy(new_data + prev->body.str_val.length,
                   cur->body.str_val.data, cur->body.str_val.length);
            new_data[new_len] = '\0';

            prev->body.str_val.data = new_data;
            prev->body.str_val.length = new_len;

            /* remove cur from list (token nodes are arena-allocated, no free) */
            prev->next = cur->next;
            cur = prev->next;
        }

        prev = prev->next;
    }
    return 0;
}

/* --- public API --- */

Token*
parse(const char* code, Arena* a)
{
    Lexer lex;

    lex.src = code;
    lex.cur = code;
    lex.line = 1;
    lex.col = 1;
    lex.head = NULL;
    lex.tail = NULL;
    lex.arena = a;

    while (1) {
        skip_ws_and_comments(&lex);

        char c = peek(&lex);

        if (c == '\0') {
            lexer_append(&lex, token_new(&lex, TOK_EOF, lex.line, lex.col));
            break;
        }

        Token* tok = NULL;

        if (isdigit((unsigned char)c)) {
            tok = read_number(&lex);
        } else if (c == '"' || c == '\'') {
            tok = read_char_or_string(&lex, c, 0, 0);
        } else if (c == 'L' &&
                   (peek_next(&lex) == '"' || peek_next(&lex) == '\'')) {
            /* wide string/char literal L"..." / L'...' */
            tok = read_char_or_string(&lex, peek_next(&lex), 1, 0);
        } else if (c == 'u' && peek_next(&lex) == '8' &&
                   (lex.cur[2] == '"' || lex.cur[2] == '\'')) {
            /* u8"..." UTF-8 string literal (u8'x' is C23; treat it as
             * a plain char literal) */
            tok = read_char_or_string(&lex, lex.cur[2], 0, 1);
        } else if (isalpha((unsigned char)c) || c == '_') {
            tok = read_ident_or_keyword(&lex);
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

    if (merge_adjacent_strings(lex.head, a) && lex.head) {
        /* prefix mismatch: mark the head so parse_program reports a
         * parse failure (the message was already printed) */
        lex.head->kind = TOK_ERROR;
    }
    return lex.head;
}
