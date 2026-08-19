/* ir_gen_const_ice.c -- const-init fallback through the ICE evaluator.
 *
 * gen_const_init handles literals, idents, casts and unary +-/~/&
 * directly; every other constant initializer expression (sizeof,
 * _Alignof, ternary selections, arithmetic over constants, mixed
 * float/int) is evaluated here with the same C integer-constant-
 * expression semantics as _Static_assert (ir_gen_sa.c).  Address
 * constants (&g, &arr[0], &g + 0) become VAL_GLOBAL values.  Before
 * this file existed those expressions fell into the silent default
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

    if (ice_eval(a, init, &val, &why, globals)) {
        if (err) *err = 1;
        fprintf(stderr, "cmpl: error: initializer element is not constant "
                "(line %d)\n", init->loc.line);
        return NULL;
    }

    /* a NULL/constant-base address ((struct S*)0)->m has no named root:
     * it is a plain integer byte address.  A pointer target gets
     * inttoptr (or null at offset 0); an integer target gets the raw
     * offset — gcc folds both.  A NAMED address stays a getelementptr
     * on @name below. */
    if (val.is_ptr && val.ptr_name.length == 0)
        return gen_const_scalar(a, target_type, val.ptr_off,
                                (double)val.ptr_off);

    /* an address constant: &g, &garr[1], &s.b, &g + k in a file-scope
     * init.  offset 0 is the bare @name (VAL_GLOBAL); a nonzero offset
     * dumps as getelementptr (i8, ptr @name, i64 off).  Only a pointer
     * target can hold an address — an integer target ((long)&g) is not
     * constant in our model (gcc accepts it via a relocation), so it
     * fails loudly instead of emitting mismatched IR. */
    if (val.is_ptr) {
        if (target_type->kind != IR_PTR) {
            if (err) *err = 1;
            fprintf(stderr, "cmpl: error: initializer element is not "
                    "constant (line %d)\n", init->loc.line);
            return NULL;
        }
        IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = (val.ptr_off != 0) ? VAL_GLOBAL_GEP : VAL_GLOBAL;
        v->name = ir_const_ir_name(globals, val.ptr_name);
        v->type = target_type;
        v->body.int_val = val.ptr_off;
        return v;
    }

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
