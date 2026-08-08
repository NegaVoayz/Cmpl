/* ir_builder_ops.c -- IR builder: arithmetic, bitwise, compare, CF, GEP, cast */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

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

BINOP_BUILDER(ir_build_fadd, IROP_FADD)
BINOP_BUILDER(ir_build_fsub, IROP_FSUB)
BINOP_BUILDER(ir_build_fmul, IROP_FMUL)
BINOP_BUILDER(ir_build_fdiv, IROP_FDIV)

BINOP_BUILDER(ir_build_and, IROP_AND)
BINOP_BUILDER(ir_build_or,  IROP_OR)
BINOP_BUILDER(ir_build_xor, IROP_XOR)
BINOP_BUILDER(ir_build_shl, IROP_SHL)

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
        char* copy = malloc(strlen(callee) + 1);
        strcpy(copy, callee);
        inst->callee.data = copy;
        inst->callee.length = strlen(callee);
    }
    inst->n_call_args = n_args;

    if (n_args > 0) {
        inst->call_args = calloc(n_args, sizeof(IR_Value*));
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
    /* callee.data stays NULL, callee.length stays 0 from calloc */

    inst->n_call_args = n_args;

    if (n_args > 0) {
        inst->call_args = calloc(n_args, sizeof(IR_Value*));
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
    inst->in_blocks = calloc(1, sizeof(IR_Block*));
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
    inst->in_blocks = calloc(2, sizeof(IR_Block*));
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
    /* fallback: if ptr has no inner type or void inner, use ptr-to-i8 */
    IR_Type* ptr_ty = ptr->type;
    if (!ptr_ty || !ptr_ty->inner || ptr_ty->inner->kind == IR_VOID)
        ptr_ty = ir_ptr_type(t_i8, 0);

    IR_Instr* inst = make_instr(b, IROP_GEP, ptr_ty);
    inst->operands[0] = ptr;
    inst->operands[1] = idx0;

    /* omit idx1 when it's constant 0 (LLVM 19 opaque-ptr rejects
     * redundant trailing index on scalar types like i8) */
    int skip_idx1 = (idx1 && idx1->kind == VAL_CONST_INT && idx1->body.int_val == 0);

    if (idx1 && !skip_idx1) {
        inst->operands[2] = idx1;
        inst->call_args = calloc(1, sizeof(IR_Value*));
        inst->call_args[0] = idx1;
        inst->n_call_args = 1;
    }
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
