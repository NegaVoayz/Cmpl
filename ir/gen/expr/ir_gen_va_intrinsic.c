/* ir_gen_va_intrinsic.c -- va_start/va_end/va_copy → LLVM intrinsics.
 *
 * __builtin_va_start/va_end/va_copy lower to @llvm.va_start/@llvm.va_end/
 * @llvm.va_copy.  The va_list operand is an lvalue whose ADDRESS is passed
 * (the intrinsic reads/writes the 24-byte SysV struct in place); va_start's
 * `last` named-param argument is ignored — LLVM recomputes the initial
 * gp/fp offsets from the enclosing function's own signature.
 *
 * Returns NULL when the callee is not a va intrinsic (the caller continues
 * with the normal call path). */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <string.h>

IR_Value*
gen_va_intrinsic(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    AST_Node* callee = n->body.call.callee;
    const char* f = NULL;
    int need2 = 0;
    char nb[32];
    int nl;

    if (callee->type != AST_IDENT)
        return NULL;

    nl = callee->body.ident.name.length;
    if (nl > 31) nl = 31;
    memcpy(nb, callee->body.ident.name.data, nl);
    nb[nl] = '\0';

    if (strcmp(nb, "__builtin_va_start") == 0)
        f = "llvm.va_start";
    else if (strcmp(nb, "__builtin_va_end") == 0)
        f = "llvm.va_end";
    else if (strcmp(nb, "__builtin_va_copy") == 0) {
        f = "llvm.va_copy";
        need2 = 1;
    } else
        return NULL;

    AST_Node* a0 = n->body.call.args;
    AST_Node* a1 = a0 ? a0->next : NULL;

    if (!a0 || (need2 && !a1))
        return NULL;

    IR_Value* v0 = gen_store_ptr(ctx, a0);
    if (!v0)
        return NULL;

    if (need2) {
        IR_Value* v1 = gen_store_ptr(ctx, a1);
        IR_Value* args[2] = { v0, v1 };

        if (!v1) return NULL;
        return ir_build_call(b, f, t_void, args, 2);
    }
    return ir_build_call(b, f, t_void, &v0, 1);
}
