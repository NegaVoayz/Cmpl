/* ir_builder_ops.c -- IR builder: arithmetic, bitwise, control flow, select */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

#include "ir_builder.h"

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
 *  Instruction builders -- select / unreachable
 * --------------------------------------------------------------- */

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
