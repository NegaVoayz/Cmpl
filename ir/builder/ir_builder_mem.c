/* ir_builder_mem.c -- IR builder: compare + GEP instruction builders.
 *
 * Split out of ir_builder_ops.c: ir_build_icmp, ir_build_fcmp and
 * ir_build_gep.  All three are declared in ir_api.h; make_instr /
 * append_instr come from ir_builder.h.
 */

#include "ir.h"

#include "arena.h"

#include "ir_builder.h"

/* ---------------------------------------------------------------
 *  Instruction builders -- compare
 * --------------------------------------------------------------- */

IR_Value*
ir_build_icmp(IR_Builder* b, IR_Cond cond, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_ICMP, t_i1);
    inst->cond = cond;
    inst->operands[0] = lhs; inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_fcmp(IR_Builder* b, IR_Cond cond, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_FCMP, t_i1);
    inst->cond = cond;
    inst->operands[0] = lhs; inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- memory access
 * --------------------------------------------------------------- */

IR_Value*
ir_build_gep(IR_Builder* b, IR_Value* ptr, IR_Value* idx0, IR_Value* idx1)
{
    /* fallback: if ptr has no inner type or void inner, use ptr-to-i8.
     * normalize: globals store the pointee type directly (not ptr-to-T),
     * so wrap them to get a proper pointer type for aggregate detection. */
    IR_Type* ptr_ty = ptr->type;
    if (ptr_ty && ptr_ty->kind != IR_PTR)
        ptr_ty = ir_ptr_type(b->arena, ptr_ty, 0);
    if (!ptr_ty || !ptr_ty->inner || ptr_ty->inner->kind == IR_VOID)
        ptr_ty = ir_ptr_type(b->arena, t_i8, 0);

    /* coerce index operands to integer type (LLVM requires integer indices) */
    if (idx0 && idx0->type && idx0->type->kind == IR_PTR)
        idx0 = ir_build_bitcast(b, idx0, t_i64);
    if (idx1 && idx1->type && idx1->type->kind == IR_PTR)
        idx1 = ir_build_bitcast(b, idx1, t_i64);

    IR_Instr* inst = make_instr(b, IROP_GEP, ptr_ty);
    inst->operands[0] = ptr;
    inst->operands[1] = idx0;

    /* compute result type: if indexing into aggregate with idx1,
     * the result is ptr-to-element, not ptr-to-aggregate */
    IR_Type* result_ty = ptr_ty;
    int is_aggregate = (ptr_ty->inner &&
        (ptr_ty->inner->kind == IR_ARRAY || ptr_ty->inner->kind == IR_STRUCT ||
         ptr_ty->inner->kind == IR_UNION));
    int skip_idx1 = (!is_aggregate && idx1 &&
        idx1->kind == VAL_CONST_INT && idx1->body.int_val == 0);

    if (idx1 && !skip_idx1) {
        inst->operands[2] = idx1;
        inst->call_args = arena_alloc(b->arena, sizeof(IR_Value*));
        inst->call_args[0] = idx1;
        inst->n_call_args = 1;
        /* after two-index GEP, result is ptr to the aggregate element:
         * array → element type; struct/union → member at const idx1.
         * (structs store members in a linked list, not in `inner`, so the
         * old inner->inner path left the type as ptr-to-aggregate, which
         * made chained member GEPs compute offsets with the outer layout) */
        if (is_aggregate) {
            IR_Type* ag = ptr_ty->inner;
            if (ag->kind == IR_ARRAY && ag->inner) {
                result_ty = ir_ptr_type(b->arena, ag->inner,
                                        ptr_ty->addrspace);
            } else if ((ag->kind == IR_STRUCT || ag->kind == IR_UNION) &&
                       idx1->kind == VAL_CONST_INT) {
                IR_Type* m = ag->members;
                long long k = idx1->body.int_val;
                while (m && k > 0) { m = m->next; k--; }
                if (m)
                    result_ty = ir_ptr_type(b->arena, m, ptr_ty->addrspace);
            }
        }
    } else if (is_aggregate && !idx1) {
        /* single-index GEP on aggregate: result still ptr-to-aggregate */
        /* (array decay needs the second index to reach the element) */
    }

    /* update the instruction's type to the actual result type */
    inst->type = result_ty;
    if (inst->result)
        inst->result->type = result_ty;

    append_instr(b, inst);
    return inst->result;
}

/* base[idx] element pointer.  A raw pointer base (pointee that is NOT an
 * array) must GEP with a single index: `getelementptr T, ptr %base, i32 %idx`.
 * The two-index (0, idx) form would index into the aggregate's FIRST member
 * (cont[1] would become &cont[1].idx, not &cont[1]) — see the const-init
 * cursor crash fixed by this helper.  Array-typed bases keep the two-index
 * form (first index selects the array, second the element). */
IR_Value*
ir_build_elem_ptr(IR_Builder* b, IR_Value* base, IR_Value* idx)
{
    if (base && base->type && base->type->kind == IR_PTR &&
        base->type->inner && base->type->inner->kind != IR_ARRAY)
        return ir_build_gep(b, base, idx, NULL);
    return ir_build_gep(b, base, ir_const_int(b, t_i32, 0), idx);
}
