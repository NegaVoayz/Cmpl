/* ir_gen_ice_type.c -- constant-expression type inference.
 *
 * C11 6.5.3.4p2: sizeof never evaluates its operand; sizeof(expr) is
 * the size of the expression's type.  The ICE evaluator (ir_gen_sa.c)
 * needs that type for operands that are not literals: globals
 * (sizeof(garr) == full array size), binary arithmetic (sizeof(2*1.5)
 * == 8, double), casts (sizeof((int)2.5+1) == 4), subscripts and member
 * accesses.  ice_expr_type infers the IR type of an expression;
 * collect_global_types builds the file-scope var name -> Type* table
 * that identifiers resolve against (declaration order, so array bounds
 * are set by the time sizeof uses them).
 */

#include "ir.h"

#include <string.h>

#include "ast.h"
#include "hash.h"
#include "ir_gen.h"

/* infer the IR type of an expression for sizeof/_Alignof.  NULL when
 * the type cannot be determined (unknown ident, function name, call,
 * VLA-dependent bound, ...). */
IR_Type*
ice_expr_type(Arena* a, AST_Node* e, HashMap* globals)
{
    return ice_expr_type_ctx(a, e, globals, NULL);
}

/* like ice_expr_type, but identifiers may also resolve to local
 * variables through the GenCtx symbol table — the runtime sizeof path
 * needs local member/index chains (sizeof(l.arr), sizeof(m[0])). */
IR_Type*
ice_expr_type_ctx(Arena* a, AST_Node* e, HashMap* globals, GenCtx* ctx)
{
    if (!e) return NULL;

    ir_init_types();   /* t_i8/t_i32/... singletons (no ir_type_from_ast yet) */

    switch (e->type) {
    case AST_INT_LIT:
        /* decimal constant typing: int if it fits, else long */
        if (e->body.literal.is_unsigned)
            return (e->body.literal.int_val > 4294967295LL) ? t_u64 : t_u32;
        return (e->body.literal.int_val > 2147483647LL ||
                e->body.literal.int_val < -2147483648LL) ? t_i64 : t_i32;

    case AST_LONG_LIT:
        return e->body.literal.is_unsigned ? t_u64 : t_i64;

    case AST_CHAR_LIT:
        return t_i32;   /* character constants have type int */

    case AST_FLOAT_LIT:  return t_f32;
    case AST_DOUBLE_LIT: return t_f64;

    case AST_STRING_LIT:
        /* char array with NUL; wide strings have 4-byte elements */
        return ir_array_type(a, e->body.literal.wide ? t_i32 : t_i8,
                             (int)e->body.literal.str_val.length + 1);

    case AST_IDENT:
        if (ctx) {
            /* local variable: the symbol table holds the alloca — a
             * pointer to the declared type.  Unwrap so sizeof(m[0]) /
             * sizeof(l.arr) peel array levels like the global path. */
            IR_Value* lv = sym_lookup(ctx, e->body.ident.name);
            if (lv && lv->type) {
                IR_Type* dt = (lv->type->kind == IR_PTR)
                    ? lv->type->inner : lv->type;
                return (dt && dt->kind == IR_ARRAY && dt->size <= 0)
                    ? NULL : dt;
            }
        }
        if (!globals) return NULL;
    {   Type* t = (Type*)hashmap_get(globals, e->body.ident.name);
        if (!t) return NULL;
        /* an array whose bound is not resolvable here (unresolved expr
         * bound, incomplete) is not a constant sizeof operand — gcc
         * rejects sizeof of an incomplete type */
        if (t->kind == TYPE_ARRAY && t->arr_size <= 0)
            return NULL;
        return ir_type_from_ast(a, t);
    }

    case AST_BINARY:
    {   IR_Type* l = ice_expr_type_ctx(a, e->body.binary.left, globals, ctx);
        IR_Type* r = ice_expr_type_ctx(a, e->body.binary.right, globals, ctx);
        if (!l || !r) return NULL;
        /* array operands decay to pointers in value contexts */
        if (l->kind == IR_ARRAY) l = ir_ptr_type(a, l->inner, 0);
        if (r->kind == IR_ARRAY) r = ir_ptr_type(a, r->inner, 0);
        if (l->kind == IR_PTR || r->kind == IR_PTR)
            return ir_ptr_type(a, t_i8, 0);
        /* usual arithmetic conversions: float wins, then 64-bit */
        if (l->kind == IR_F64 || r->kind == IR_F64) return t_f64;
        if (l->kind == IR_F32 || r->kind == IR_F32) return t_f32;
        if (l->kind == IR_I64 || r->kind == IR_I64) return t_i64;
        return t_i32;
    }

    case AST_UNARY:
    {   TokenKind op = e->body.unary.op;
        if (op == TOK_BANG) return t_i32;
        IR_Type* ot = ice_expr_type_ctx(a, e->body.unary.operand, globals, ctx);
        if (!ot) return NULL;
        switch (op) {
        case TOK_AMP:
            return ir_ptr_type(a, ot, 0);
        case TOK_STAR:
            if (ot->kind == IR_PTR) return ot->inner;
            if (ot->kind == IR_ARRAY) return ot->inner;  /* *arr = elem */
            return NULL;
        case TOK_MINUS: case TOK_PLUS: case TOK_TILDE:
            /* integer promotion: char/short operands promote to int */
            if (ot->kind == IR_I1 || ot->kind == IR_I8 ||
                ot->kind == IR_I16)
                return t_i32;
            return ot;
        default:
            return NULL;
        }
    }

    case AST_CAST:
        return ir_type_from_ast(a, e->body.cast.type_expr);

    case AST_INDEX:
    {   IR_Type* at = ice_expr_type_ctx(a, e->body.subscript.array, globals, ctx);
        if (!at) return NULL;
        if (at->kind == IR_ARRAY) return at->inner;
        if (at->kind == IR_PTR) return at->inner;
        return NULL;
    }

    case AST_MEMBER:
    {   IR_Type* rec = ice_expr_type_ctx(a, e->body.member.record, globals, ctx);
        if (!rec) return NULL;
        if (rec->kind == IR_PTR) rec = rec->inner;      /* p->a */
        if (!rec || (rec->kind != IR_STRUCT && rec->kind != IR_UNION))
            return NULL;
        Type* ast = ir_struct_ast_lookup(rec);
        if (!ast) return NULL;
        String want = e->body.member.member;
        for (AST_Node* f = ast->params; f; f = f->next) {
            if (f->type != AST_VAR_DECL) continue;
            String fn = f->body.var_decl.name;
            if (fn.length == want.length &&
                memcmp(fn.data, want.data, fn.length) == 0)
                return ir_type_from_ast(a, f->body.var_decl.var_type);
        }
        return NULL;
    }

    case AST_TERNARY:
    {   IR_Type* x = ice_expr_type_ctx(a, e->body.ternary.then_expr, globals, ctx);
        IR_Type* y = ice_expr_type_ctx(a, e->body.ternary.else_expr, globals, ctx);
        if (!x || !y) return NULL;
        if (x->kind == y->kind) return x;
        if (x->kind == IR_F64 || y->kind == IR_F64) return t_f64;
        if (x->kind == IR_F32 || y->kind == IR_F32) return t_f32;
        if (x->kind == IR_I64 || y->kind == IR_I64) return t_i64;
        if (x->kind == IR_PTR || y->kind == IR_PTR)
            return ir_ptr_type(a, t_i8, 0);
        return t_i32;
    }

    case AST_POSTFIX:
        return ice_expr_type_ctx(a, e->body.postfix.operand, globals, ctx);

    case AST_SIZEOF_EXPR: case AST_SIZEOF_TYPE:
    case AST_ALIGNOF_EXPR: case AST_ALIGNOF_TYPE:
        return t_i64;   /* sizeof/_Alignof results are size_t */

    default:
        return NULL;
    }
}

/* collect file-scope variable declarations into a name -> Type* table.
 * Struct refs must already be resolved (resolve_struct_refs_all), and
 * this must run before resolve_array_sizes_pass so the table's array
 * Types see their bounds resolved in declaration order. */
void
collect_global_types(Arena* a, AST_Node* root, HashMap* gmap)
{
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_VAR_DECL) continue;
        hashmap_put(gmap, decl->body.var_decl.name,
                    decl->body.var_decl.var_type);
    }
}
