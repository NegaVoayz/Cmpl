#include "parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
dump_ast(AST_Node* n, int depth)
{
    if (!n) return;

    for (int i = 0; i < depth; i++)
        printf("  ");

    switch (n->type) {
    case AST_INT_LIT:
        printf("INT_LIT: %ld\n", n->body.literal.int_val);
        break;
    case AST_LONG_LIT:
        printf("LONG_LIT: %ldL\n", n->body.literal.int_val);
        break;
    case AST_CHAR_LIT:
        printf("CHAR_LIT: '%c'\n", n->body.literal.char_val);
        break;
    case AST_STRING_LIT:
        printf("STRING_LIT: \"%.*s\"\n", n->body.literal.str_val.length,
               n->body.literal.str_val.data);
        break;
    case AST_FLOAT_LIT:
        printf("FLOAT_LIT: %gf\n", n->body.literal.float_val);
        break;
    case AST_DOUBLE_LIT:
        printf("DOUBLE_LIT: %g\n", n->body.literal.float_val);
        break;
    case AST_IDENT:
        printf("IDENT: %.*s\n", n->body.ident.name.length,
               n->body.ident.name.data);
        break;
    case AST_BINARY:
        printf("BINARY: %d\n", n->body.binary.op);
        dump_ast(n->body.binary.left, depth + 1);
        dump_ast(n->body.binary.right, depth + 1);
        break;
    case AST_UNARY:
        printf("UNARY: %d\n", n->body.unary.op);
        dump_ast(n->body.unary.operand, depth + 1);
        break;
    case AST_POSTFIX:
        printf("POSTFIX: %d\n", n->body.postfix.op);
        dump_ast(n->body.postfix.operand, depth + 1);
        break;
    case AST_CALL:
        printf("CALL\n");
        dump_ast(n->body.call.callee, depth + 1);
        dump_ast(n->body.call.args, depth + 1);
        break;
    case AST_INDEX:
        printf("INDEX\n");
        dump_ast(n->body.subscript.array, depth + 1);
        dump_ast(n->body.subscript.index, depth + 1);
        break;
    case AST_MEMBER:
        printf("MEMBER: %d %.*s\n", n->body.member.op,
               n->body.member.member.length, n->body.member.member.data);
        dump_ast(n->body.member.record, depth + 1);
        break;
    case AST_TERNARY:
        printf("TERNARY\n");
        dump_ast(n->body.ternary.cond, depth + 1);
        dump_ast(n->body.ternary.then_expr, depth + 1);
        dump_ast(n->body.ternary.else_expr, depth + 1);
        break;
    case AST_SIZEOF_EXPR:
        printf("SIZEOF_EXPR\n");
        dump_ast(n->body.sizeof_expr.expr, depth + 1);
        break;
    default:
        printf("AST_Type=%d\n", n->type);
        break;
    }
}

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

static char*
read_file(const char* path)
{
    FILE* f = fopen(path, "rb");

    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = malloc(size + 1);

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

int
main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
        return 1;
    }

    char* code = read_file(argv[1]);

    if (!code) {
        return 1;
    }

    Token* tokens = parse(code);

    /* Parse the first expression */
    printf("\n--- Parsing expression ---\n");
    LR1_Parser* parser = lr1_parser_new(tokens);
    AST_Node*   expr = lr1_parse_expr(parser);

    if (expr) {
        printf("\nAST:\n");
        dump_ast(expr, 0);
    } else {
        printf("Parse error!\n");
    }

    lr1_parser_free(parser);
    free_tokens(tokens);
    free(code);
    return 0;
}
