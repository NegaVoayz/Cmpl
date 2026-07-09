#include "parse.h"
#include "preprocess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char*
token_kind_name(TokenKind kind)
{
    static const char* names[] = {
        [TOK_IF]          = "TOK_IF",
        [TOK_ELSE]        = "TOK_ELSE",
        [TOK_WHILE]       = "TOK_WHILE",
        [TOK_FOR]         = "TOK_FOR",
        [TOK_RETURN]      = "TOK_RETURN",
        [TOK_INT]         = "TOK_INT",
        [TOK_CHAR]        = "TOK_CHAR",
        [TOK_VOID]        = "TOK_VOID",
        [TOK_STRUCT]      = "TOK_STRUCT",
        [TOK_TYPEDEF]     = "TOK_TYPEDEF",
        [TOK_SIZEOF]      = "TOK_SIZEOF",
        [TOK_BREAK]       = "TOK_BREAK",
        [TOK_CONTINUE]    = "TOK_CONTINUE",
        [TOK_SWITCH]      = "TOK_SWITCH",
        [TOK_CASE]        = "TOK_CASE",
        [TOK_DEFAULT]     = "TOK_DEFAULT",
        [TOK_DO]          = "TOK_DO",
        [TOK_GOTO]        = "TOK_GOTO",
        [TOK_ENUM]        = "TOK_ENUM",
        [TOK_UNION]       = "TOK_UNION",
        [TOK_CONST]       = "TOK_CONST",
        [TOK_VOLATILE]    = "TOK_VOLATILE",
        [TOK_STATIC]      = "TOK_STATIC",
        [TOK_EXTERN]      = "TOK_EXTERN",
        [TOK_REGISTER]    = "TOK_REGISTER",
        [TOK_SIGNED]      = "TOK_SIGNED",
        [TOK_UNSIGNED]    = "TOK_UNSIGNED",
        [TOK_SHORT]       = "TOK_SHORT",
        [TOK_LONG]        = "TOK_LONG",
        [TOK_DOUBLE]      = "TOK_DOUBLE",
        [TOK_FLOAT]       = "TOK_FLOAT",
        [TOK_INT_LIT]     = "TOK_INT_LIT",
        [TOK_LONG_LIT]    = "TOK_LONG_LIT",
        [TOK_CHAR_LIT]    = "TOK_CHAR_LIT",
        [TOK_STRING_LIT]  = "TOK_STRING_LIT",
        [TOK_FLOAT_LIT]   = "TOK_FLOAT_LIT",
        [TOK_DOUBLE_LIT]  = "TOK_DOUBLE_LIT",
        [TOK_IDENT]       = "TOK_IDENT",
        [TOK_PLUS]        = "TOK_PLUS",
        [TOK_MINUS]       = "TOK_MINUS",
        [TOK_STAR]        = "TOK_STAR",
        [TOK_SLASH]       = "TOK_SLASH",
        [TOK_PERCENT]     = "TOK_PERCENT",
        [TOK_EQ]          = "TOK_EQ",
        [TOK_EQEQ]        = "TOK_EQEQ",
        [TOK_BANGEQ]      = "TOK_BANGEQ",
        [TOK_LT]          = "TOK_LT",
        [TOK_GT]          = "TOK_GT",
        [TOK_LTEQ]        = "TOK_LTEQ",
        [TOK_GTEQ]        = "TOK_GTEQ",
        [TOK_AMPAMP]      = "TOK_AMPAMP",
        [TOK_PIPEPIPE]    = "TOK_PIPEPIPE",
        [TOK_BANG]        = "TOK_BANG",
        [TOK_AMP]         = "TOK_AMP",
        [TOK_PIPE]        = "TOK_PIPE",
        [TOK_CARET]       = "TOK_CARET",
        [TOK_TILDE]       = "TOK_TILDE",
        [TOK_LTLT]        = "TOK_LTLT",
        [TOK_GTGT]        = "TOK_GTGT",
        [TOK_PLUSEQ]      = "TOK_PLUSEQ",
        [TOK_MINUSEQ]     = "TOK_MINUSEQ",
        [TOK_STAREQ]      = "TOK_STAREQ",
        [TOK_SLASHEQ]     = "TOK_SLASHEQ",
        [TOK_PLUSPLUS]    = "TOK_PLUSPLUS",
        [TOK_MINUSMINUS]  = "TOK_MINUSMINUS",
        [TOK_ARROW]       = "TOK_ARROW",
        [TOK_DOT]         = "TOK_DOT",
        [TOK_LPAREN]      = "TOK_LPAREN",
        [TOK_RPAREN]      = "TOK_RPAREN",
        [TOK_LBRACKET]    = "TOK_LBRACKET",
        [TOK_RBRACKET]    = "TOK_RBRACKET",
        [TOK_LBRACE]      = "TOK_LBRACE",
        [TOK_RBRACE]      = "TOK_RBRACE",
        [TOK_SEMI]        = "TOK_SEMI",
        [TOK_COMMA]       = "TOK_COMMA",
        [TOK_COLON]       = "TOK_COLON",
        [TOK_QUESTION]    = "TOK_QUESTION",
        [TOK_EOF]         = "TOK_EOF",
        [TOK_ERROR]       = "TOK_ERROR",
    };

    return names[kind] ? names[kind] : "UNKNOWN";
}

static void
free_tokens(Token* head)
{
    while (head) {
        Token* next = head->next;

        if (head->kind == TOK_STRING_LIT && head->body.str_val.data) {
            free((void*)head->body.str_val.data);
        }
        free(head);
        head = next;
    }
}

int
main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return 1;
    }

    char* code = preprocess(argv[1]);

    if (!code) {
        return 1;
    }

    Token* tokens = parse(code);
    int    count = 0;

    for (Token* t = tokens; t; t = t->next) {
        printf("%d:%d  %-16s", t->loc.line, t->loc.col, token_kind_name(t->kind));

        switch (t->kind) {
        case TOK_INT_LIT:
            printf(" %ld", t->body.int_val);
            break;
        case TOK_LONG_LIT:
            printf(" %ldL", t->body.int_val);
            break;
        case TOK_CHAR_LIT:
            printf(" '%c'", t->body.char_val);
            break;
        case TOK_STRING_LIT:
            printf(" \"%.*s\"", t->body.str_val.length, t->body.str_val.data);
            break;
        case TOK_FLOAT_LIT:
            printf(" %gf", t->body.float_val);
            break;
        case TOK_DOUBLE_LIT:
            printf(" %g", t->body.float_val);
            break;
        case TOK_IDENT:
            printf(" %.*s", t->body.ident.length, t->body.ident.data);
            break;
        default:
            break;
        }
        printf("\n");
        count++;
    }

    printf("\nTotal: %d tokens\n", count);
    free_tokens(tokens);
    free(code);
    return 0;
}
