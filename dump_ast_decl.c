/* dump_ast_decl.c -- AST debug pretty-printer: declaration nodes */

#include "dump_ast.h"
#include <stdio.h>

void dump_decl(AST_Node* n, int depth)
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
