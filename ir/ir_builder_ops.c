/* ir_builder_ops.c -- IR builder: arithmetic, bitwise, compare, CF, GEP, cast */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* from ir_builder.c */
extern IR_Value* make_vreg(IR_Builder* b, IR_Type* ty);
extern IR_Instr* make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty);
extern void      append_instr(IR_Builder* b, IR_Instr* inst);

/* macro for simple binary ops */
#define BINOP_BUILDER(FN, OP)                   \
IR_Value* FN(IR_Builder* b, IR_Value* l, IR_Value* r) { \
    IR_Instr* inst = make_instr(b, OP, l->type);        \
    inst->operands[0] = l; inst->operands[1] = r;       \
    append_instr(b, inst); return inst->result;          \
}

BINOP_BUILDER(ir_build_add,  IROP_ADD)
BINOP_BUILDER(ir_build_sub,  IROP_SUB)
BINOP_BUILDER(ir_build_mul,  IROP_MUL)
BINOP_BUILDER(ir_build_sdiv, IROP_SDIV)
BINOP_BUILDER(ir_build_srem, IROP_SREM)
BINOP_BUILDER(ir_build_udiv, IROP_UDIV)
BINOP_BUILDER(ir_build_urem, IROP_UREM)

BINOP_BUILDER(ir_build_fadd, IROP_FADD)
BINOP_BUILDER(ir_build_fsub, IROP_FSUB)
BINOP_BUILDER(ir_build_fmul, IROP_FMUL)
BINOP_BUILDER(ir_build_fdiv, IROP_FDIV)

BINOP_BUILDER(ir_build_and, IROP_AND)
BINOP_BUILDER(ir_build_or,  IROP_OR)
BINOP_BUILDER(ir_build_xor, IROP_XOR)
BINOP_BUILDER(ir_build_shl, IROP_SHL)
BINOP_BUILDER(ir_build_ashr, IROP_ASHR)
BINOP_BUILDER(ir_build_lshr, IROP_LSHR)

#undef BINOP_BUILDER

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
 *  Instruction builders -- control flow
 * --------------------------------------------------------------- */

IR_Value*
ir_build_call(IR_Builder* b, const char* callee, IR_Type* ret_ty,
              IR_Value** args, int n_args)
{
    IR_Instr* inst = make_instr(b, IROP_CALL, ret_ty);

    if (callee) {
        int clen = (int)strlen(callee);
        char* copy = arena_alloc(b->arena, clen + 1);
        memcpy(copy, callee, clen + 1);
        inst->callee.data = copy;
        inst->callee.length = clen;
    }
    inst->n_call_args = n_args;

    if (n_args > 0) {
        inst->call_args = arena_alloc(b->arena, n_args * sizeof(IR_Value*));
        memcpy(inst->call_args, args, n_args * sizeof(IR_Value*));
    }
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_call_ptr(IR_Builder* b, IR_Value* fn_ptr, IR_Type* ret_ty,
                  IR_Value** args, int n_args)
{
    IR_Instr* inst = make_instr(b, IROP_CALL, ret_ty);

    /* indirect call: store function pointer in operands[0],
     * leave callee empty to signal indirect call in dump */
    inst->operands[0] = fn_ptr;

    inst->n_call_args = n_args;

    if (n_args > 0) {
        inst->call_args = arena_alloc(b->arena, n_args * sizeof(IR_Value*));
        memcpy(inst->call_args, args, n_args * sizeof(IR_Value*));
    }
    append_instr(b, inst);
    return inst->result;
}

void
ir_build_ret(IR_Builder* b, IR_Value* val)
{
    IR_Instr* inst = make_instr(b, IROP_RET, t_void);
    inst->operands[0] = val;
    append_instr(b, inst);
}

void
ir_build_br(IR_Builder* b, IR_Block* target)
{
    IR_Instr* inst = make_instr(b, IROP_BR, t_void);
    inst->operands[0] = NULL;
    inst->in_blocks = arena_alloc(b->arena, sizeof(IR_Block*));
    inst->in_blocks[0] = target;
    inst->n_incoming = 1;
    append_instr(b, inst);
}

void
ir_build_cond_br(IR_Builder* b, IR_Value* cond,
                 IR_Block* then_blk, IR_Block* else_blk)
{
    IR_Instr* inst = make_instr(b, IROP_COND_BR, t_void);
    inst->operands[0] = cond;
    inst->in_blocks = arena_alloc(b->arena, 2 * sizeof(IR_Block*));
    inst->in_blocks[0] = then_blk;
    inst->in_blocks[1] = else_blk;
    inst->n_incoming = 2;
    append_instr(b, inst);
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
        /* after two-index GEP, result is ptr to the array element type */
        if (is_aggregate && ptr_ty->inner->inner)
            result_ty = ir_ptr_type(b->arena, ptr_ty->inner->inner, ptr_ty->addrspace);
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

/* ---------------------------------------------------------------
 *  Instruction builders -- cast / select
 * --------------------------------------------------------------- */

#define CAST_BUILDER(FN, OP)                                  \
IR_Value* FN(IR_Builder* b, IR_Value* v, IR_Type* to) {       \
    IR_Instr* inst = make_instr(b, OP, to);                    \
    inst->operands[0] = v; append_instr(b, inst);              \
    return inst->result;                                       \
}

CAST_BUILDER(ir_build_bitcast, IROP_BITCAST)
CAST_BUILDER(ir_build_trunc,   IROP_TRUNC)
CAST_BUILDER(ir_build_zext,    IROP_ZEXT)
CAST_BUILDER(ir_build_sext,    IROP_SEXT)
CAST_BUILDER(ir_build_sitofp,  IROP_SITOFP)
CAST_BUILDER(ir_build_fptosi,  IROP_FPTOSI)

#undef CAST_BUILDER

IR_Value*
ir_build_select(IR_Builder* b, IR_Value* cond, IR_Value* tv, IR_Value* fv)
{
    IR_Instr* inst = make_instr(b, IROP_SELECT, tv->type);
    inst->operands[0] = cond; inst->operands[1] = tv; inst->operands[2] = fv;
    append_instr(b, inst);
    return inst->result;
}

void
ir_build_unreachable(IR_Builder* b)
{
    IR_Instr* inst = make_instr(b, IROP_UNREACHABLE, t_void);
    append_instr(b, inst);
}
