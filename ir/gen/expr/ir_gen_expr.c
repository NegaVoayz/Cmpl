/* ir_gen_expr.c -- AST-to-IR expression generation (dispatch). */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>
#include <string.h>

/* identifier: load a local/global, else a function-name value
 * (function pointer), else an undefined i32 placeholder. */
static IR_Value*
gen_expr_ident(GenCtx* ctx, AST_Node* n)
{
    IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
    if (ptr) return ir_build_load(ctx->b, ptr);
    ptr = global_lookup(ctx->mod, n->body.ident.name);
    if (ptr) return ir_build_load(ctx->b, ptr);
    /* function name used as a value (function pointer) —
     * resolves to the global function symbol. */
    if (ctx->sig_map) {
        IR_Type* func_ty = func_type_lookup(ctx->sig_map, n->body.ident.name);
        if (func_ty) {
            IR_Value* fn_val = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            fn_val->kind = VAL_GLOBAL;
            fn_val->name = n->body.ident.name;
            fn_val->type = ir_ptr_type(ctx->b->arena, func_ty, 0);
            return fn_val;
        }
    }
    /* enumerator constant: opt_enum only substituted AST-foldable values,
     * so a non-literal-valued enum (sizeof-based, pointer arithmetic,
     * ternary) is registered in mod->enum_vals at IR gen and its uses
     * here stay AST_IDENT.  Const contexts resolve them via
     * resolve_enum_idents; function-body expressions must look the value
     * up in the same table (locals shadow enum names earlier). */
    { TypedefEntry* ev = (TypedefEntry*)ctx->mod->enum_vals;
      for (; ev; ev = ev->next) {
          if (ev->name.length == n->body.ident.name.length &&
              memcmp(ev->name.data, n->body.ident.name.data,
                     ev->name.length) == 0)
              return ir_const_int(ctx->b, t_i32,
                                  (int)(intptr_t)ev->aliased_type);
      } }
    return gen_undef(ctx->b, t_i32);
}

/* binary operator: short-circuit AND/OR, assignment/compound-assignment
 * (store through an lvalue), or a plain arithmetic/comparison op. */
static IR_Value*
gen_expr_binary(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    TokenKind op = n->body.binary.op;
    int is_cmpd = (op == TOK_PLUSEQ || op == TOK_MINUSEQ ||
                   op == TOK_STAREQ || op == TOK_SLASHEQ ||
                   op == TOK_PERCENTEQ || op == TOK_AMPEQ ||
                   op == TOK_PIPEEQ || op == TOK_CARETEQ ||
                   op == TOK_LTLTEQ || op == TOK_GTGTEQ);

    /* logical AND/OR: short-circuit (must not evaluate RHS if LHS
     * decides the result) */
    if (op == TOK_AMPAMP || op == TOK_PIPEPIPE)
        return gen_logical(ctx, op, n->body.binary.left,
                           n->body.binary.right);

    if (op == TOK_EQ || is_cmpd) {
        /* bit-field member assignment: RMW through the storage unit */
        if (n->body.binary.left && n->body.binary.left->type == AST_MEMBER) {
            BfLoc loc;
            if (bf_resolve_member(ctx, n->body.binary.left, &loc)) {
                IR_Value* rhs = gen_expr(ctx, n->body.binary.right);
                IR_Value* result;
                if (op == TOK_EQ) result = rhs;
                else { IR_Value* old = bf_load(ctx, &loc);
                       result = gen_binary_op(ctx, op, old, rhs); }
                bf_store(ctx, &loc, result);
                return result;
            }
        }
        IR_Value* rhs = gen_expr(ctx, n->body.binary.right);
        IR_Value* ptr = gen_store_ptr(ctx, n->body.binary.left);
        if (ptr) {
            IR_Value* result;
            if (op == TOK_EQ) result = rhs;
            else { IR_Value* old = ir_build_load(b, ptr);
                   result = gen_binary_op(ctx, op, old, rhs); }
            ir_build_store(b, result, ptr);
            return result; }
        if (op == TOK_EQ) return rhs;
        IR_Value* l = gen_expr(ctx, n->body.binary.left);
        return gen_binary_op(ctx, op, l, rhs); }
    IR_Value* l = gen_expr(ctx, n->body.binary.left);
    IR_Value* r = gen_expr(ctx, n->body.binary.right);
    return gen_binary_op(ctx, op, l, r);
}

/* <<<...>>> kernel launch: emit a call to the mangled host mock
 * __cmpl_kl_<name> with a FIXED 4-slot config header followed by the
 * kernel args.  The mock (vk_mock.c) splits on this exact boundary, so
 * the config count never has to be guessed from the arg count (that
 * heuristic corrupted every launch with >= 2 kernel args).
 *
 * The arg list is arena-allocated and sized from the actual arg count:
 * the old fixed IR_Value* ab[16] silently DROPPED every kernel arg past
 * the 12th (4 config slots + 12) while still passing the truncated
 * count to cmpl_vk_launch. */
static IR_Value*
gen_expr_kernel_launch(GenCtx* ctx, AST_Node* n)
{
    AST_Node* cn = n->body.kernel_launch.callee; String kn = {0,0};
    if (cn && cn->type == AST_IDENT) kn = cn->body.ident.name;
    /* buffer fits "__cmpl_kl_" + the longest C identifier (255 chars) */
    char pn[256]; int kl = kn.length > 240 ? 240 : kn.length;
    memcpy(pn, "__cmpl_kl_", 10); if (kl>0) memcpy(pn+10, kn.data, kl); pn[10+kl] = '\0';

    IR_Value* zero = ir_const_int(ctx->b, t_i32, 0);
    int n_ka = 0;
    for (AST_Node* a = n->body.kernel_launch.args; a; a = a->next) n_ka++;

    IR_Value** ab = arena_alloc(ctx->b->arena, (4 + n_ka) * sizeof(IR_Value*));
    int n_args = 0;
    AST_Node* c = n->body.kernel_launch.config;

    /* slots: grid, block, shared, stream — absent entries become 0 */
    for (int slot = 0; slot < 4; slot++) {
        ab[n_args++] = c ? gen_expr(ctx, c) : zero;
        if (c) c = c->next;
    }
    for (AST_Node* a = n->body.kernel_launch.args; a; a = a->next)
        ab[n_args++] = gen_expr(ctx, a);
    return ir_build_call(ctx->b, pn, t_void, ab, n_args);
}

/* sizeof(expr) codegen (implemented in ir_gen_sizeof.c) */

/* ---------------------------------------------------------------
 *  C11 _Generic selection (implemented in ir_gen_generic.c)
 * --------------------------------------------------------------- */

IR_Value*
gen_expr(GenCtx* ctx, AST_Node* n)
{
    if (!n) return NULL;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_INT_LIT:   return ir_const_int(b, n->body.literal.is_unsigned ? t_u32 : t_i32, n->body.literal.int_val);
    case AST_LONG_LIT:  return ir_const_int(b, n->body.literal.is_unsigned ? t_u64 : t_i64, n->body.literal.int_val);
    case AST_CHAR_LIT:  return n->body.literal.wide
                             ? ir_const_int(b, t_i32, n->body.literal.int_val)
                             : ir_const_int(b, t_i8, n->body.literal.char_val);
    case AST_FLOAT_LIT: return ir_const_float(ctx->b->arena, t_f32, n->body.literal.float_val);
    case AST_DOUBLE_LIT:return ir_const_float(ctx->b->arena, t_f64, n->body.literal.float_val);

    case AST_STRING_LIT:
    { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
      v->kind = VAL_CONST_STRING;
      v->type = ir_ptr_type(ctx->b->arena,
                  n->body.literal.wide ? t_i32 : t_i8, 0);
      v->is_wide = n->body.literal.wide;
      v->body.str_val = n->body.literal.str_val; return v; }

    case AST_IDENT: return gen_expr_ident(ctx, n);
    case AST_BINARY: return gen_expr_binary(ctx, n);
    case AST_UNARY: return gen_unary_expr(ctx, n);
    case AST_CALL: return gen_call_expr(ctx, n);
    case AST_KERNEL_LAUNCH: return gen_expr_kernel_launch(ctx, n);
    case AST_TERNARY: return gen_ternary_expr(ctx, n);
    case AST_CAST: return gen_cast(ctx, n);
    case AST_COMPOUND_LIT: return gen_compound_lit(ctx, n);
    case AST_INDEX: return gen_index_expr(ctx, n);
    case AST_MEMBER: return gen_member_expr(ctx, n);

    case AST_SIZEOF_TYPE:
    { IR_Type* t = ir_type_from_ast(ctx->b->arena, n->body.sizeof_type.type_expr);
      return ir_const_int(b, t_i32, ir_type_size(t)); }

    case AST_SIZEOF_EXPR: return gen_expr_sizeof_expr(ctx, n);

    /* _Alignof(type): the type's alignment in bytes */
    case AST_ALIGNOF_TYPE:
    { IR_Type* t = ir_type_from_ast(ctx->b->arena, n->body.sizeof_type.type_expr);
      return ir_const_int(b, t_i32, ir_type_align(t)); }

    /* _Alignof(expr): alignment of the expression's type (not evaluated) */
    case AST_ALIGNOF_EXPR:
    { IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
      IR_Type* t = sub ? sub->type : t_i32;
      return ir_const_int(b, t_i32, ir_type_align(t)); }

    case AST_POSTFIX: return gen_postfix_expr(ctx, n);

    case AST_GENERIC: return gen_expr_generic(ctx, n);

    case AST_VA_ARG: return gen_va_arg_expr(ctx, n);

    default:
    { return gen_undef(ctx->b, t_i32); }
    }
}
