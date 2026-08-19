/* ir_gen_const_ice.c -- const-init fallback through the ICE evaluator.
 *
 * gen_const_init handles literals, idents, casts and unary +-/~/&
 * directly; every other constant initializer expression (sizeof,
 * _Alignof, ternary selections, arithmetic over constants, mixed
 * float/int) is evaluated here with the same C integer-constant-
 * expression semantics as _Static_assert (ir_gen_sa.c).  Before this
 * file existed those expressions fell into the silent default
 * fallback of gen_const_init and emitted 0 (wrong code).
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdio.h>

IR_Value*
gen_const_ice_eval(Arena* a, AST_Node* init, IR_Type* target_type,
                   TypedefEntry* enum_vals, HashMap* globals, int* err)
{
    if (!init || !target_type) return NULL;

    resolve_enum_idents(init, enum_vals);

    ICEVal val;
    const char* why = NULL;

    if (ice_eval(a, init, &val, &why, globals)) return NULL;

    if (val.is_float) {
        if (target_type->kind == IR_F32 || target_type->kind == IR_F64)
            return gen_const_scalar(a, target_type, 0, val.f);
        /* float constant assigned to an integer: truncate toward zero
         * (C conversion; mirrors the AST_DOUBLE_LIT path) */
        return gen_const_scalar(a, target_type, (long long)val.f,
                                (double)(long long)val.f);
    }
    return gen_const_scalar(a, target_type, val.v, (double)val.v);
}
