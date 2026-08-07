/* dump_ast.c -- AST debug pretty-printer */

#include "ast.h"
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
    dump_indent(depth); printf("%s:\n", label);
    for (AST_Node* cur = n; cur; cur = cur->next) dump_ast(cur, depth + 1);
}

static void dump_lit(AST_Node* n)
{
    switch (n->type) {
    case AST_INT_LIT:
        printf("INT_LIT: %ld\n", n->body.literal.int_val); break;
    case AST_LONG_LIT:
        printf("LONG_LIT: %ldL\n", n->body.literal.int_val); break;
    case AST_CHAR_LIT:
        printf("CHAR_LIT: '%c'\n", n->body.literal.char_val); break;
    case AST_STRING_LIT:
        printf("STRING_LIT: \"%.*s\"\n", n->body.literal.str_val.length,
               n->body.literal.str_val.data); break;
    case AST_FLOAT_LIT: case AST_DOUBLE_LIT:
        printf("%s: %g\n",
               n->type == AST_FLOAT_LIT ? "FLOAT_LIT" : "DOUBLE_LIT",
               n->body.literal.float_val); break;
    default: break;
    }
}

static void dump_expr(AST_Node* n, int depth)
{
    switch (n->type) {
    case AST_IDENT:
        printf("IDENT: %.*s\n", n->body.ident.name.length,
               n->body.ident.name.data); break;
    case AST_BINARY:
        printf("BINARY: %d\n", n->body.binary.op);
        dump_ast(n->body.binary.left, depth + 1);
        dump_ast(n->body.binary.right, depth + 1); break;
    case AST_UNARY:
        printf("UNARY: %d\n", n->body.unary.op);
        dump_ast(n->body.unary.operand, depth + 1); break;
    case AST_POSTFIX:
        printf("POSTFIX: %d\n", n->body.postfix.op);
        dump_ast(n->body.postfix.operand, depth + 1); break;
    case AST_CALL:
        printf("CALL\n");
        dump_ast(n->body.call.callee, depth + 1);
        dump_node_list(n->body.call.args, depth + 1, "ARGS"); break;
    case AST_INDEX:
        printf("INDEX\n");
        dump_ast(n->body.subscript.array, depth + 1);
        dump_ast(n->body.subscript.index, depth + 1); break;
    case AST_MEMBER:
        printf("MEMBER: %d %.*s\n", n->body.member.op,
               n->body.member.member.length, n->body.member.member.data);
        dump_ast(n->body.member.record, depth + 1); break;
    case AST_TERNARY:
        printf("TERNARY\n");
        dump_ast(n->body.ternary.cond, depth + 1);
        dump_ast(n->body.ternary.then_expr, depth + 1);
        dump_ast(n->body.ternary.else_expr, depth + 1); break;
    case AST_SIZEOF_EXPR:
        printf("SIZEOF_EXPR\n");
        dump_ast(n->body.sizeof_expr.expr, depth + 1); break;
    case AST_SIZEOF_TYPE: printf("SIZEOF_TYPE\n"); break;
    case AST_CAST:
        printf("CAST\n");
        dump_ast(n->body.cast.cast_expr, depth + 1); break;
    case AST_COMPOUND_LIT:
        printf("COMPOUND_LIT\n");
        if (n->body.compound_lit.init)
            dump_ast(n->body.compound_lit.init, depth + 1);
        break;
    case AST_KERNEL_LAUNCH:
        printf("KERNEL_LAUNCH\n");
        dump_ast(n->body.kernel_launch.callee, depth + 1);
        dump_node_list(n->body.kernel_launch.config, depth + 1, "CONFIG");
        dump_node_list(n->body.kernel_launch.args, depth + 1, "ARGS"); break;
    default: break;
    }
}

static void dump_stmt(AST_Node* n, int depth)
{
    switch (n->type) {
    case AST_BLOCK:
        printf("BLOCK\n");
        dump_node_list(n->body.block.stmts, depth + 1, "STMTS"); break;
    case AST_IF:
        printf("IF\n");
        dump_ast(n->body.if_stmt.condition, depth + 1);
        dump_ast(n->body.if_stmt.then_branch, depth + 1);
        if (n->body.if_stmt.else_branch) {
            dump_indent(depth + 1); printf("ELSE:\n");
            dump_ast(n->body.if_stmt.else_branch, depth + 2); } break;
    case AST_WHILE:
        printf("WHILE\n");
        dump_ast(n->body.loop.condition, depth + 1);
        dump_ast(n->body.loop.body, depth + 1); break;
    case AST_DO_WHILE:
        printf("DO_WHILE\n");
        dump_ast(n->body.loop.body, depth + 1);
        dump_ast(n->body.loop.condition, depth + 1); break;
    case AST_FOR:
        printf("FOR\n");
        if (n->body.for_stmt.init) {
            dump_indent(depth + 1); printf("INIT:\n");
            dump_ast(n->body.for_stmt.init, depth + 2); }
        if (n->body.for_stmt.condition) {
            dump_indent(depth + 1); printf("COND:\n");
            dump_ast(n->body.for_stmt.condition, depth + 2); }
        if (n->body.for_stmt.update) {
            dump_indent(depth + 1); printf("UPDATE:\n");
            dump_ast(n->body.for_stmt.update, depth + 2); }
        dump_ast(n->body.for_stmt.body, depth + 1); break;
    case AST_RETURN:
        printf("RETURN\n");
        if (n->body.ret.expr) dump_ast(n->body.ret.expr, depth + 1); break;
    case AST_BREAK: printf("BREAK\n"); break;
    case AST_CONTINUE: printf("CONTINUE\n"); break;
    case AST_SWITCH:
        printf("SWITCH\n");
        dump_ast(n->body.switch_stmt.condition, depth + 1);
        dump_ast(n->body.switch_stmt.body, depth + 1); break;
    case AST_CASE: case AST_DEFAULT:
        printf("%s\n", n->type == AST_CASE ? "CASE" : "DEFAULT");
        if (n->body.case_stmt.value)
            dump_ast(n->body.case_stmt.value, depth + 1);
        else { dump_indent(depth + 1); printf("DEFAULT\n"); }
        dump_ast(n->body.case_stmt.stmt, depth + 1); break;
    case AST_GOTO:
        printf("GOTO: %.*s\n", n->body.jump.label.length,
               n->body.jump.label.data); break;
    case AST_LABEL:
        printf("LABEL: %.*s\n", n->body.label.name.length,
               n->body.label.name.data);
        dump_ast(n->body.label.stmt, depth + 1); break;
    case AST_EXPR_STMT:
        printf("EXPR_STMT\n");
        if (n->body.expr_stmt.expr)
            dump_ast(n->body.expr_stmt.expr, depth + 1); break;
    default: break;
    }
}

static void dump_decl(AST_Node* n, int depth)
{
    switch (n->type) {
    case AST_VAR_DECL:
    { static const char* as[] = {"host","global","shared","constant"};
      printf("VAR_DECL: %.*s [%s]\n", n->body.var_decl.name.length,
             n->body.var_decl.name.data, as[n->body.var_decl.addr_space & 3]);
      if (n->body.var_decl.init) {
          dump_indent(depth + 1); printf("INIT:\n");
          dump_ast(n->body.var_decl.init, depth + 2); } break; }
    case AST_FUNC_DEF:
    { static const char* ln[] = {"host","device","global","host_device"};
      printf("FUNC_DEF: %.*s [%s]\n", n->body.func_def.name.length,
             n->body.func_def.name.data, ln[n->body.func_def.linkage & 3]);
      dump_node_list(n->body.func_def.params, depth + 1, "PARAMS");
      if (n->body.func_def.body) dump_ast(n->body.func_def.body, depth + 1);
      break; }
    case AST_STRUCT_DEF: case AST_UNION_DEF:
        printf("%s: %.*s\n",
               n->type == AST_STRUCT_DEF ? "STRUCT_DEF" : "UNION_DEF",
               n->body.struct_def.name.length,
               n->body.struct_def.name.data);
        dump_node_list(n->body.struct_def.fields, depth + 1, "FIELDS"); break;
    case AST_ENUM_DEF:
        printf("ENUM_DEF: %.*s\n", n->body.enum_def.name.length,
               n->body.enum_def.name.data);
        dump_node_list(n->body.enum_def.enumerators, depth + 1, "ENUMERATORS"); break;
    case AST_ENUMERATOR: case AST_TYPEDEF: case AST_PARAM_DECL:
    { const char* tag = n->type == AST_ENUMERATOR ? "ENUMERATOR" :
                        n->type == AST_TYPEDEF ? "TYPEDEF" : "PARAM";
      String nm = n->type == AST_ENUMERATOR ? n->body.enumerator.name :
                  n->type == AST_TYPEDEF ? n->body.typedef_decl.name :
                  n->body.param_decl.name;
      printf("%s: %.*s\n", tag, nm.length, nm.data); } break;
    case AST_PROGRAM:
        printf("PROGRAM\n");
        dump_node_list(n->body.program.decls, depth + 1, "DECLS"); break;
    default: break;
    }
}

static void dump_ast(AST_Node* n, int depth)
{
    if (!n) return;
    dump_indent(depth); dump_lit(n);
    dump_expr(n, depth); dump_stmt(n, depth); dump_decl(n, depth);
}

void dump_ast_public(AST_Node* n, int depth) { dump_ast(n, depth); }
