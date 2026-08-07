/* ir_type.c -- IR type constructors and AST-to-IR type conversion */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* ---------------------------------------------------------------
 *  Common type singletons
 * --------------------------------------------------------------- */

IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
IR_Type *t_f32, *t_f64;

static int singletons_inited = 0;

static IR_Type*
make_singleton(IR_TypeKind kind)
{
    IR_Type* t = calloc(1, sizeof(IR_Type));
    t->kind = kind;
    return t;
}

static void
init_singletons(void)
{
    if (singletons_inited) return;

    t_void = make_singleton(IR_VOID);
    t_i1   = make_singleton(IR_I1);
    t_i8   = make_singleton(IR_I8);
    t_i16  = make_singleton(IR_I16);
    t_i32  = make_singleton(IR_I32);
    t_i64  = make_singleton(IR_I64);
    t_f32  = make_singleton(IR_F32);
    t_f64  = make_singleton(IR_F64);
    singletons_inited = 1;
}

/* ---------------------------------------------------------------
 *  Type constructors
 * --------------------------------------------------------------- */

IR_Type*
ir_type_new(IR_TypeKind kind)
{
    IR_Type* t = calloc(1, sizeof(IR_Type));
    t->kind = kind;
    return t;
}

IR_Type*
ir_ptr_type(IR_Type* inner, int addrspace)
{
    IR_Type* t = ir_type_new(IR_PTR);
    t->inner = inner;
    t->addrspace = addrspace;
    return t;
}

IR_Type*
ir_array_type(IR_Type* elem, int size)
{
    IR_Type* t = ir_type_new(IR_ARRAY);
    t->inner = elem;
    t->size = size;
    return t;
}

IR_Type*
ir_func_type(IR_Type* ret, IR_Type* params)
{
    IR_Type* t = ir_type_new(IR_FUNC);
    t->inner = ret;
    t->members = params;
    return t;
}

/* ---------------------------------------------------------------
 *  AST-to-IR type conversion
 * --------------------------------------------------------------- */

static IR_Type*
ast_to_ir_type(Type* ast)
{
    if (!ast) return t_void;

    switch (ast->kind) {
    case TYPE_VOID:   return t_void;
    case TYPE_CHAR:   return t_i8;
    case TYPE_SHORT:  return t_i16;
    case TYPE_INT:    return t_i32;
    case TYPE_LONG:   return t_i64;
    case TYPE_FLOAT:  return t_f32;
    case TYPE_DOUBLE: return t_f64;
    case TYPE_ENUM:   return t_i32;

    case TYPE_SIGNED:
    case TYPE_UNSIGNED:
        if (ast->next) return ast_to_ir_type(ast->next);
        return t_i32;
    case TYPE_PTR:
    { IR_Type* inner = ast_to_ir_type(ast->inner); int as = ast->is_const ? 3 : 0;
      return ir_ptr_type(inner, as); }
    case TYPE_ARRAY:
    { IR_Type* inner = ast_to_ir_type(ast->inner);
      return ir_array_type(inner, ast->arr_size > 0 ? ast->arr_size : 0); }
    case TYPE_FUNC:
    {
        IR_Type* ret = ast_to_ir_type(ast->inner);
        IR_Type *params = NULL, **tail = &params;

        for (AST_Node* p = ast->params; p; p = p->next) {
            IR_Type* pt = ast_to_ir_type(p->body.param_decl.param_type);
            *tail = pt;
            tail = &pt->next;
        }
        return ir_func_type(ret, params);
    }

    case TYPE_STRUCT:
    case TYPE_UNION:
    { IR_Type* t = ir_type_new(IR_STRUCT); t->name = ast->name; return t; }
    case TYPE_NAMED:
        if (ast->inner) return ast_to_ir_type(ast->inner);
        return t_i32;
    default:
        return t_i32;
    }
}

IR_Type*
ir_type_from_ast(Type* ast_type)
{
    init_singletons();
    return ast_to_ir_type(ast_type);
}

/* ---------------------------------------------------------------
 *  Type utilities
 * --------------------------------------------------------------- */

int
ir_type_eq(IR_Type* a, IR_Type* b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case IR_PTR:
        return a->addrspace == b->addrspace && ir_type_eq(a->inner, b->inner);
    case IR_ARRAY:
        return a->size == b->size && ir_type_eq(a->inner, b->inner);
    case IR_FUNC:
        return ir_type_eq(a->inner, b->inner) && ir_type_eq(a->members, b->members);
    case IR_STRUCT:
        return a->name.data == b->name.data;  /* compare by tag */
    default:
        return 1;
    }
}

const char*
ir_type_name(IR_Type* t)
{
    if (!t) return "void";

    switch (t->kind) {
    case IR_VOID:  return "void";
    case IR_I1:    return "i1";
    case IR_I8:    return "i8";
    case IR_I16:   return "i16";
    case IR_I32:   return "i32";
    case IR_I64:   return "i64";
    case IR_F32:   return "float";
    case IR_F64:   return "double";
    case IR_PTR:   return "ptr";
    case IR_ARRAY: return "array";
    case IR_STRUCT:return "struct";
    case IR_FUNC:  return "func";
    default:       return "?";
    }
}
