#ifndef PARSE_H
#define PARSE_H

#include "types.h"   /* String */

typedef enum {
    // key
    TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_RETURN,
    TOK_INT, TOK_CHAR, TOK_VOID, TOK_STRUCT, TOK_TYPEDEF,
    TOK_SIZEOF, TOK_BREAK, TOK_CONTINUE, TOK_SWITCH, TOK_CASE,
    TOK_DEFAULT, TOK_DO, TOK_GOTO, TOK_ENUM, TOK_UNION,
    TOK_CONST, TOK_VOLATILE, TOK_STATIC, TOK_EXTERN, TOK_REGISTER,
    TOK_SIGNED, TOK_UNSIGNED, TOK_SHORT, TOK_LONG, TOK_DOUBLE,
    TOK_FLOAT,
    TOK_KW_GLOBAL,     /* __global__ */
    TOK_KW_DEVICE,     /* __device__ */
    TOK_KW_HOST,       /* __host__ */
    TOK_KW_SHARED,     /* __shared__ */
    TOK_KW_CONSTANT,   /* __constant__ */
    TOK_ATTRIBUTE,     /* __attribute__ */

    // lit
    TOK_INT_LIT,      // 42
    TOK_LONG_LIT,      // 42l
    TOK_CHAR_LIT,     // 'a'
    TOK_STRING_LIT,   // "hello"
    TOK_FLOAT_LIT,    // 3.14f
    TOK_DOUBLE_LIT,    // 3.14

    // id
    TOK_IDENT,        // foo, bar

    // op
    TOK_PLUS,         // +
    TOK_MINUS,        // -
    TOK_STAR,         // *
    TOK_SLASH,        // /
    TOK_PERCENT,      // %
    TOK_EQ,           // =
    TOK_EQEQ,         // ==
    TOK_BANGEQ,       // !=
    TOK_LT,           // <
    TOK_GT,           // >
    TOK_LTEQ,         // <=
    TOK_GTEQ,         // >=
    TOK_AMPAMP,       // &&
    TOK_PIPEPIPE,     // ||
    TOK_BANG,         // !
    TOK_AMP,          // &
    TOK_PIPE,         // |
    TOK_CARET,        // ^
    TOK_TILDE,        // ~
    TOK_LTLT,         // <<
    TOK_GTGT,         // >>
    TOK_PLUSEQ,       // +=
    TOK_MINUSEQ,      // -=
    TOK_STAREQ,       // *=
    TOK_SLASHEQ,      // /=
    TOK_PERCENTEQ,    // %=
    TOK_AMPEQ,        // &=
    TOK_PIPEEQ,       // |=
    TOK_CARETEQ,      // ^=
    TOK_LTLTEQ,       // <<=
    TOK_GTGTEQ,       // >>=
    TOK_PLUSPLUS,     // ++
    TOK_MINUSMINUS,   // --
    TOK_LTLTLT,       // <<<
    TOK_GTGTGT,       // >>>
    TOK_ARROW,        // ->
    TOK_DOT,          // .
    TOK_ELLIPSIS,     // ...

    // sep
    TOK_LPAREN,       // (
    TOK_RPAREN,       // )
    TOK_LBRACKET,     // [
    TOK_RBRACKET,     // ]
    TOK_LBRACE,       // {
    TOK_RBRACE,       // }
    TOK_SEMI,         // ;
    TOK_COMMA,        // ,
    TOK_COLON,        // :
    TOK_QUESTION,     // ?

    // spec
    TOK_EOF,
    TOK_ERROR,
    NUM_TOKEN_KINDS
} TokenKind;

#define NUM_TOKENS NUM_TOKEN_KINDS

typedef struct {
    int line;
    int col;
} SourceLoc;

typedef struct Token Token;

// the char* here is a copy, so the caller is responsible for freeing up the char* if required.
struct Token {
    TokenKind kind;
    SourceLoc loc;
    Token*    next;

    union {
        long long int_val;      // TOK_INT_LIT / TOK_LONG_LIT (64-bit)
        char char_val;          // TOK_CHAR_LIT
        double float_val;       // TOK_FLOAT_LIT
        String str_val;              // TOK_STRING_LIT
        String ident;                // TOK_IDENT
    } body;
};

typedef struct Arena Arena;

Token* parse(const char* code, Arena* a);

#endif /* PARSE_H */