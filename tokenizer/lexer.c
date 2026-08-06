#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* name;
    TokenKind kind;
} Keyword;

static const Keyword keywords[] = {
    {"break",    TOK_BREAK},
    {"case",     TOK_CASE},
    {"char",     TOK_CHAR},
    {"const",    TOK_CONST},
    {"continue", TOK_CONTINUE},
    {"default",  TOK_DEFAULT},
    {"do",       TOK_DO},
    {"double",   TOK_DOUBLE},
    {"else",     TOK_ELSE},
    {"enum",     TOK_ENUM},
    {"extern",   TOK_EXTERN},
    {"float",    TOK_FLOAT},
    {"for",      TOK_FOR},
    {"goto",     TOK_GOTO},
    {"if",       TOK_IF},
    {"int",      TOK_INT},
    {"long",     TOK_LONG},
    {"register", TOK_REGISTER},
    {"return",   TOK_RETURN},
    {"short",    TOK_SHORT},
    {"signed",   TOK_SIGNED},
    {"sizeof",   TOK_SIZEOF},
    {"static",   TOK_STATIC},
    {"struct",   TOK_STRUCT},
    {"switch",   TOK_SWITCH},
    {"typedef",  TOK_TYPEDEF},
    {"union",    TOK_UNION},
    {"unsigned", TOK_UNSIGNED},
    {"void",     TOK_VOID},
    {"volatile", TOK_VOLATILE},
    {"while",    TOK_WHILE},
};

#define KW_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static int
keyword_cmp(const void* a, const void* b)
{
    return strcmp(((const Keyword*)a)->name, ((const Keyword*)b)->name);
}

Token*
read_ident_or_keyword(Lexer* lex)
{
    int line = lex->line, col = lex->col;
    const char* start = lex->cur;

    while (isalnum((unsigned char)peek(lex)) || peek(lex) == '_') {
        advance(lex);
    }
    int len = (int)(lex->cur - start);

    Keyword key = {.name = NULL};
    char    name_buf[128];

    memcpy(name_buf, start, len);
    name_buf[len] = '\0';
    key.name = name_buf;

    Keyword* found = bsearch(&key, keywords, KW_COUNT, sizeof(Keyword), keyword_cmp);

    if (found) {
        Token* tok = token_new(found->kind, line, col);
        return tok;
    }

    Token* tok = token_new(TOK_IDENT, line, col);

    tok->body.ident.data = start;
    tok->body.ident.length = len;
    return tok;
}

Token*
read_operator(Lexer* lex)
{
    int line = lex->line, col = lex->col;
    char c = peek(lex);

    advance(lex);
    switch (c) {
    case '+':
        if (peek(lex) == '+') { advance(lex); return token_new(TOK_PLUSPLUS, line, col); }
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_PLUSEQ, line, col); }
        return token_new(TOK_PLUS, line, col);
    case '-':
        if (peek(lex) == '-') { advance(lex); return token_new(TOK_MINUSMINUS, line, col); }
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_MINUSEQ, line, col); }
        if (peek(lex) == '>') { advance(lex); return token_new(TOK_ARROW, line, col); }
        return token_new(TOK_MINUS, line, col);
    case '*':
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_STAREQ, line, col); }
        return token_new(TOK_STAR, line, col);
    case '/':
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_SLASHEQ, line, col); }
        return token_new(TOK_SLASH, line, col);
    case '%': return token_new(TOK_PERCENT, line, col);
    case '=':
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_EQEQ, line, col); }
        return token_new(TOK_EQ, line, col);
    case '!':
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_BANGEQ, line, col); }
        return token_new(TOK_BANG, line, col);
    case '<':
        if (peek(lex) == '<' && peek_next(lex) == '<') {
            advance(lex); advance(lex);
            return token_new(TOK_LTLTLT, line, col);
        }
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_LTEQ, line, col); }
        if (peek(lex) == '<') { advance(lex); return token_new(TOK_LTLT, line, col); }
        return token_new(TOK_LT, line, col);
    case '>':
        if (peek(lex) == '>' && peek_next(lex) == '>') {
            advance(lex); advance(lex);
            return token_new(TOK_GTGTGT, line, col);
        }
        if (peek(lex) == '=') { advance(lex); return token_new(TOK_GTEQ, line, col); }
        if (peek(lex) == '>') { advance(lex); return token_new(TOK_GTGT, line, col); }
        return token_new(TOK_GT, line, col);
    case '&':
        if (peek(lex) == '&') { advance(lex); return token_new(TOK_AMPAMP, line, col); }
        return token_new(TOK_AMP, line, col);
    case '|':
        if (peek(lex) == '|') { advance(lex); return token_new(TOK_PIPEPIPE, line, col); }
        return token_new(TOK_PIPE, line, col);
    case '^': return token_new(TOK_CARET, line, col);
    case '~': return token_new(TOK_TILDE, line, col);
    case '(': return token_new(TOK_LPAREN, line, col);
    case ')': return token_new(TOK_RPAREN, line, col);
    case '[': return token_new(TOK_LBRACKET, line, col);
    case ']': return token_new(TOK_RBRACKET, line, col);
    case '{': return token_new(TOK_LBRACE, line, col);
    case '}': return token_new(TOK_RBRACE, line, col);
    case ';': return token_new(TOK_SEMI, line, col);
    case ',': return token_new(TOK_COMMA, line, col);
    case ':': return token_new(TOK_COLON, line, col);
    case '?': return token_new(TOK_QUESTION, line, col);
    case '.':
        if (isdigit((unsigned char)peek(lex))) {
            lex->cur--;
            lex->col--;
            return read_number(lex);
        }
        return token_new(TOK_DOT, line, col);
    case '#': {
        while (peek(lex) != '\0' && peek(lex) != '\n') {
            advance(lex);
        }
        return NULL;
    }
    default:
        return token_new(TOK_ERROR, line, col);
    }
}
