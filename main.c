#include "parse.h"
#include "pp.h"
#include "optimize.h"

#include <stdio.h>

static void dump_ast(AST_Node* n, int depth);

static void dump_indent(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

static void dump_node_list(AST_Node* n, int depth, const char* label)
{
    if (!n) return;

    dump_indent(depth);
    printf("%s:\n", label);

    for (AST_Node* cur = n; cur; cur = cur->next)
        dump_ast(cur, depth + 1);
}

static void dump_ast(AST_Node* n, int depth)
{
    if (!n) return;

    dump_indent(depth);

    switch (n->type) {
    /* literals */
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

    /* primary */
    case AST_IDENT:
        printf("IDENT: %.*s\n", n->body.ident.name.length,
               n->body.ident.name.data);
        break;

    /* expressions */
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
        dump_node_list(n->body.call.args, depth + 1, "ARGS");
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
    case AST_SIZEOF_TYPE:
        printf("SIZEOF_TYPE\n");
        break;
    case AST_CAST:
        printf("CAST\n");
        dump_ast(n->body.cast.cast_expr, depth + 1);
        break;
    case AST_KERNEL_LAUNCH:
        printf("KERNEL_LAUNCH\n");
        dump_ast(n->body.kernel_launch.callee, depth + 1);
        dump_node_list(n->body.kernel_launch.config, depth + 1, "CONFIG");
        dump_node_list(n->body.kernel_launch.args, depth + 1, "ARGS");
        break;

    /* statements */
    case AST_BLOCK:
        printf("BLOCK\n");
        dump_node_list(n->body.block.stmts, depth + 1, "STMTS");
        break;
    case AST_IF:
        printf("IF\n");
        dump_ast(n->body.if_stmt.condition, depth + 1);
        dump_ast(n->body.if_stmt.then_branch, depth + 1);

        if (n->body.if_stmt.else_branch) {
            dump_indent(depth + 1);
            printf("ELSE:\n");
            dump_ast(n->body.if_stmt.else_branch, depth + 2);
        }
        break;
    case AST_WHILE:
        printf("WHILE\n");
        dump_ast(n->body.loop.condition, depth + 1);
        dump_ast(n->body.loop.body, depth + 1);
        break;
    case AST_DO_WHILE:
        printf("DO_WHILE\n");
        dump_ast(n->body.loop.body, depth + 1);
        dump_ast(n->body.loop.condition, depth + 1);
        break;
    case AST_FOR:
        printf("FOR\n");

        if (n->body.for_stmt.init) {
            dump_indent(depth + 1);
            printf("INIT:\n");
            dump_ast(n->body.for_stmt.init, depth + 2);
        }

        if (n->body.for_stmt.condition) {
            dump_indent(depth + 1);
            printf("COND:\n");
            dump_ast(n->body.for_stmt.condition, depth + 2);
        }

        if (n->body.for_stmt.update) {
            dump_indent(depth + 1);
            printf("UPDATE:\n");
            dump_ast(n->body.for_stmt.update, depth + 2);
        }
        dump_ast(n->body.for_stmt.body, depth + 1);
        break;
    case AST_RETURN:
        printf("RETURN\n");

        if (n->body.ret.expr)
            dump_ast(n->body.ret.expr, depth + 1);
        break;
    case AST_BREAK:
        printf("BREAK\n");
        break;
    case AST_CONTINUE:
        printf("CONTINUE\n");
        break;
    case AST_SWITCH:
        printf("SWITCH\n");
        dump_ast(n->body.switch_stmt.condition, depth + 1);
        dump_ast(n->body.switch_stmt.body, depth + 1);
        break;
    case AST_CASE:
        printf("CASE\n");

        if (n->body.case_stmt.value)
            dump_ast(n->body.case_stmt.value, depth + 1);
        else {
            dump_indent(depth + 1);
            printf("DEFAULT\n");
        }
        dump_ast(n->body.case_stmt.stmt, depth + 1);
        break;
    case AST_DEFAULT:
        printf("DEFAULT\n");
        dump_ast(n->body.case_stmt.stmt, depth + 1);
        break;
    case AST_GOTO:
        printf("GOTO: %.*s\n", n->body.jump.label.length,
               n->body.jump.label.data);
        break;
    case AST_LABEL:
        printf("LABEL: %.*s\n", n->body.label.name.length,
               n->body.label.name.data);
        dump_ast(n->body.label.stmt, depth + 1);
        break;
    case AST_EXPR_STMT:
        printf("EXPR_STMT\n");

        if (n->body.expr_stmt.expr)
            dump_ast(n->body.expr_stmt.expr, depth + 1);
        break;

    /* declarations */
    case AST_VAR_DECL:
        printf("VAR_DECL: %.*s\n", n->body.var_decl.name.length,
               n->body.var_decl.name.data);

        if (n->body.var_decl.init) {
            dump_indent(depth + 1);
            printf("INIT:\n");
            dump_ast(n->body.var_decl.init, depth + 2);
        }
        break;
    case AST_FUNC_DEF:
        printf("FUNC_DEF: %.*s\n", n->body.func_def.name.length,
               n->body.func_def.name.data);
        dump_node_list(n->body.func_def.params, depth + 1, "PARAMS");

        if (n->body.func_def.body)
            dump_ast(n->body.func_def.body, depth + 1);
        break;
    case AST_STRUCT_DEF:
        printf("STRUCT_DEF: %.*s\n", n->body.struct_def.name.length,
               n->body.struct_def.name.data);
        dump_node_list(n->body.struct_def.fields, depth + 1, "FIELDS");
        break;
    case AST_UNION_DEF:
        printf("UNION_DEF: %.*s\n", n->body.struct_def.name.length,
               n->body.struct_def.name.data);
        dump_node_list(n->body.struct_def.fields, depth + 1, "FIELDS");
        break;
    case AST_ENUM_DEF:
        printf("ENUM_DEF: %.*s\n", n->body.enum_def.name.length,
               n->body.enum_def.name.data);
        dump_node_list(n->body.enum_def.enumerators, depth + 1, "ENUMERATORS");
        break;
    case AST_ENUMERATOR:
        printf("ENUMERATOR: %.*s", n->body.enumerator.name.length,
               n->body.enumerator.name.data);

        if (n->body.enumerator.value) {
            printf(" = ");
            /* value is an expression, print inline */
        }
        printf("\n");
        break;
    case AST_TYPEDEF:
        printf("TYPEDEF: %.*s\n", n->body.typedef_decl.name.length,
               n->body.typedef_decl.name.data);
        break;
    case AST_PARAM_DECL:
        printf("PARAM: %.*s\n", n->body.param_decl.name.length,
               n->body.param_decl.name.data);
        break;
    case AST_PROGRAM:
        printf("PROGRAM\n");
        dump_node_list(n->body.program.decls, depth + 1, "DECLS");
        break;
    }
}

int
main(int argc, char** argv)
{
    const char* filename = NULL;
    PPCtx       pp_ctx;

    pp_ctx_init(&pp_ctx);

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2] != '\0') {
            pp_add_include_path(&pp_ctx, argv[i] + 2);
        } else if (argv[i][0] == '-' && argv[i][1] == 'I' && argv[i][2] == '\0'
                   && i + 1 < argc) {
            pp_add_include_path(&pp_ctx, argv[++i]);
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [-I dir]... <source-file>\n", argv[0]);
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    char* code = pp_preprocess(&pp_ctx, filename);

    if (!code) {
        pp_ctx_free(&pp_ctx);
        return 1;
    }

    printf("--- Parsing ---\n");
    AST_Node* root = parse_program(code);

    if (root) {
        printf("\n--- Optimizing ---\n");
        root = optimize(root);

        printf("\nAST:\n");
        dump_ast(root, 0);
    } else {
        printf("Parse error!\n");
    }

    /* NOTE: code must not be freed here -- AST String fields are
     * non-owning pointers into token data which references source text. */
    return 0;
}
