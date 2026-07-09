#ifndef LEXER_H
#define LEXER_H

#include "token.h"

/* Internal lexer state shared across split files */
typedef struct {
    const char* src;
    const char* cur;
    int line;
    int col;
    Token* head;
    Token* tail;
} Lexer;

/* Token creation and list management */
Token* token_new(TokenKind kind, int line, int col);
void   lexer_append(Lexer* lex, Token* tok);

/* Cursor operations */
char peek(Lexer* lex);
char peek_next(Lexer* lex);
void advance(Lexer* lex);
void skip_ws_and_comments(Lexer* lex);

/* Token readers (implemented across split files) */
Token* read_number(Lexer* lex);
Token* read_char_or_string(Lexer* lex, char quote);
Token* read_ident_or_keyword(Lexer* lex);
Token* read_operator(Lexer* lex);

#endif
