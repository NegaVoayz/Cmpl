/* ir_gen_call.c -- function call lowering (direct + indirect). */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>
#include <string.h>

/* innermost IDENT of a callee expression, unwrapping deref/paren
 * wrappers: (f) / (*f) / ((f)) all yield the name f */
static AST_Node*
fnptr_callee_base(AST_Node* e)
{
    while (e) {
        if (e->type == AST_IDENT) return e;
        if (e->type == AST_UNARY && e->body.unary.op == TOK_STAR)
            e = e->body.unary.operand;
        else
            return NULL;
    }
    return NULL;
}

/* Recover an indirect call's callee signature from the pointer's
 * PTR(FUNC) type (local loads keep the slot type; global loads come back
 * opaque, so also consult the declaring symbol: globals store the element
 * type, locals the alloca PTR(vt)).  Fixes calls through fnptr vars
 * defaulting to an i32 return type. */
static IR_Type*
recover_indirect_sig(GenCtx* ctx, AST_Node* callee, IR_Value* fn_ptr,
                     IR_Type* func_ty)
{
    IR_Type* ft = fn_ptr->type;

    if (!(ft && ft->kind == IR_PTR && ft->inner &&
          ft->inner->kind == IR_FUNC)) {
        AST_Node* base = fnptr_callee_base(callee);
        IR_Value* sym = base
            ? sym_lookup(ctx, base->body.ident.name) : NULL;
        if (!sym) sym = base
            ? global_lookup(ctx->mod, base->body.ident.name) : NULL;
        if (sym && sym->type) {
            ft = sym->type;
            if (ft->kind == IR_PTR && ft->inner &&
                ft->inner->kind == IR_PTR)
                ft = ft->inner;      /* local alloca: PTR(vt) */
        }
    }
    if (ft && ft->kind == IR_PTR && ft->inner &&
        ft->inner->kind == IR_FUNC)
        return ft->inner;
    return func_ty;
}

/* Coerce call arguments to the declared parameter types, then apply the
 * default argument promotion for variadic functions (char/short → int,
 * float → double, C11 6.5.2.2p6).  Mutates arg_buf in place. */
static void
coerce_call_args(IR_Builder* b, IR_Type* func_ty, IR_Value** arg_buf, int n_args)
{
    /* fix up argument types to match the function signature: integer
     * promotions (i8->i32 like an isalpha arg), integer truncation, and
     * ptr/int mismatches so call + declare have correct types. */
    if (func_ty && func_ty->members) {
        IR_Type* expected = func_ty->members;
        for (int i = 0; i < n_args && expected;
             i++, expected = expected->next) {
            if (!arg_buf[i] || !arg_buf[i]->type) continue;
            IR_Type* at = arg_buf[i]->type;
            if (at == expected) continue;
            /* any scalar/pointer arg → _Bool param: nonzero → i1 */
            if (expected->kind == IR_I1) {
                arg_buf[i] = coerce_to_i1(b, arg_buf[i]);
                continue;
            }
            int at_int = (at->kind >= IR_I1 && at->kind <= IR_I64);
            int ex_int = (expected->kind >= IR_I1 &&
                          expected->kind <= IR_I64);
            if (at_int && ex_int) {
                int at_sz = ir_type_size(at);
                int ex_sz = ir_type_size(expected);
                if (at_sz < ex_sz)
                    arg_buf[i] = widen_zext(at)
                        ? ir_build_zext(b, arg_buf[i], expected)
                        : ir_build_sext(b, arg_buf[i], expected);
                else if (at_sz > ex_sz)
                    arg_buf[i] = ir_build_trunc(b, arg_buf[i], expected);
            } else if (at_int &&
                       (expected->kind == IR_F32 ||
                        expected->kind == IR_F64)) {
                /* int arg → float param: sitofp/uitofp */
                arg_buf[i] = at->is_unsigned
                    ? ir_build_uitofp(b, arg_buf[i], expected)
                    : ir_build_sitofp(b, arg_buf[i], expected);
            } else if ((at->kind == IR_F32 || at->kind == IR_F64) &&
                       ex_int) {
                /* float arg → int param: fptosi/fptoui */
                arg_buf[i] = expected->is_unsigned
                    ? ir_build_fptoui(b, arg_buf[i], expected)
                    : ir_build_fptosi(b, arg_buf[i], expected);
            } else if (at->kind != expected->kind) {
                arg_buf[i] = ir_build_bitcast(b, arg_buf[i], expected);
            }
        }
    }

    if (func_ty && func_ty->is_variadic) {
        int n_fixed = 0;
        for (IR_Type* m = func_ty->members; m; m = m->next) n_fixed++;
        for (int i = n_fixed; i < n_args; i++) {
            if (!arg_buf[i] || !arg_buf[i]->type) continue;
            IR_Type* at = arg_buf[i]->type;
            if (at->kind == IR_I1 || at->kind == IR_I8 || at->kind == IR_I16)
                arg_buf[i] = widen_zext(at)
                    ? ir_build_zext(b, arg_buf[i], t_i32)
                    : ir_build_sext(b, arg_buf[i], t_i32);
            else if (at->kind == IR_F32)
                arg_buf[i] = ir_build_bitcast(b, arg_buf[i], t_f64);
        }
    }
}

IR_Value*
gen_call_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* up to 16 args inline; longer calls get an arena buffer so a long
     * printf argument list is never truncated */
    int n_args = 0; IR_Value* arg_buf[16]; IR_Value** dyn_buf = NULL;
    IR_Value** args = arg_buf; String cn = {0,0};
    IR_Value* fn_ptr = NULL;
    if (n->body.call.callee->type == AST_IDENT) {
        cn = n->body.call.callee->body.ident.name;
        /* if name resolves to a local or global function-pointer
         * variable, use an indirect call; otherwise direct call by name */
        IR_Value* local = sym_lookup(ctx, cn);
        if (local) fn_ptr = ir_build_load(b, local);
        else {
            IR_Value* gv = global_lookup(ctx->mod, cn);
            if (gv) fn_ptr = ir_build_load(b, gv);
        }
    } else
        fn_ptr = gen_expr(ctx, n->body.call.callee);

    /* __builtin_va_start/va_end/va_copy — lowered to LLVM intrinsics
     * before the generic arg-gen loop (which would load the va_list
     * lvalue as a 24-byte value, not its address) */
    {
        IR_Value* v = gen_va_intrinsic(ctx, n);

        if (v) return v;
    }

    for (AST_Node* a = n->body.call.args; a; a = a->next) n_args++;
    if (n_args > 16) {
        dyn_buf = arena_alloc(b->arena, n_args * sizeof(IR_Value*));
        args = dyn_buf;
    }
    n_args = 0;
    for (AST_Node* a = n->body.call.args; a; a = a->next)
        args[n_args++] = gen_expr(ctx, a);
    IR_Type* ret_t = t_i32;
    IR_Type* func_ty = NULL;
    if (cn.length > 0) {
        func_ty = func_type_lookup(ctx->sig_map, cn);
        if (func_ty) ret_t = func_ty->inner;
    }
    /* Indirect call: recover the callee signature from the pointer. */
    if (!func_ty && fn_ptr)
        func_ty = recover_indirect_sig(ctx, n->body.call.callee,
                                       fn_ptr, func_ty);
    if (func_ty && func_ty->inner) ret_t = func_ty->inner;
    if (!ret_t) ret_t = t_i32;

    coerce_call_args(b, func_ty, args, n_args);

    if (fn_ptr) {
        /* indirect call through function pointer */
        IR_Value* result = ir_build_call_ptr(b, fn_ptr, ret_t,
                                             args, n_args);
        if (result && result->def_instr)
            result->def_instr->func_type = func_ty;
        return result;
    }
    char nb[128]; int nl = cn.length; if (nl > 127) nl = 127;
    memcpy(nb, cn.data, nl); nb[nl] = '\0';
    { IR_Value* result = ir_build_call(b, nb, ret_t, args, n_args);
      if (result && result->def_instr)
          result->def_instr->func_type = func_ty;
      return result; }
}
