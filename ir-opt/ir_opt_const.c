/* ir_opt_const.c -- constant folding and algebraic simplifications */

#include "ir-opt.h"

#include <stdlib.h>

/* ---------------------------------------------------------------
 *  Try to fold a binary operation with constant operands
 *  Returns folded value or NULL
 * --------------------------------------------------------------- */

static IR_Value*
fold_binary(IR_Opcode op, IR_Value* lhs, IR_Value* rhs, IR_Type* ty)
{
    if (!lhs || !rhs || !ty) return NULL;
    if (lhs->kind != VAL_CONST_INT || rhs->kind != VAL_CONST_INT)
        return NULL;

    long long a = lhs->body.int_val, b = rhs->body.int_val, r = 0;

    switch (op) {
    case IROP_ADD:  r = a + b; break;
    case IROP_SUB:  r = a - b; break;
    case IROP_MUL:  r = a * b; break;
    case IROP_SDIV: if (b == 0) return NULL; r = a / b; break;
    case IROP_SREM: if (b == 0) return NULL; r = a % b; break;
    case IROP_AND:  r = a & b; break;
    case IROP_OR:   r = a | b; break;
    case IROP_XOR:  r = a ^ b; break;
    case IROP_SHL:  r = a << b; break;
    case IROP_LSHR: r = (unsigned long long)a >> b; break;
    case IROP_ASHR: r = a >> b; break;
    default: return NULL;
    }

    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_CONST_INT; v->type = ty; v->body.int_val = r;
    return v;
}

/* ---------------------------------------------------------------
 *  Identity simplifications: x+0→x, x*1→x, x*0→0, etc.
 *  Returns simplified value or NULL
 * --------------------------------------------------------------- */

static IR_Value*
simplify(IR_Opcode op, IR_Value* lhs, IR_Value* rhs)
{
    int lc = (lhs && lhs->kind == VAL_CONST_INT);
    int rc = (rhs && rhs->kind == VAL_CONST_INT);
    long long lv = lc ? lhs->body.int_val : 0;
    long long rv = rc ? rhs->body.int_val : 0;

    switch (op) {
    case IROP_ADD:
        if (rc && rv == 0) return lhs;
        if (lc && lv == 0) return rhs;
        break;
    case IROP_SUB:
        if (rc && rv == 0) return lhs;
        break;
    case IROP_MUL:
        if (rc && rv == 0) return rhs;       /* x*0 = 0 */
        if (lc && lv == 0) return lhs;       /* 0*x = 0 */
        if (rc && rv == 1) return lhs;       /* x*1 = x */
        if (lc && lv == 1) return rhs;       /* 1*x = x */
        break;
    case IROP_SDIV:
        if (rc && rv == 1) return lhs;       /* x/1 = x */
        break;
    case IROP_AND:
        if (rc && rv == 0) return rhs;       /* x&0 = 0 */
        if (lc && lv == 0) return lhs;       /* 0&x = 0 */
        break;
    case IROP_OR:
        if (rc && rv == 0) return lhs;       /* x|0 = x */
        if (lc && lv == 0) return rhs;       /* 0|x = x */
        break;
    case IROP_XOR:
        if (rc && rv == 0) return lhs;       /* x^0 = x */
        if (lc && lv == 0) return rhs;       /* 0^x = x */
        break;
    default: break;
    }
    return NULL;
}

/* ---------------------------------------------------------------
 *  Fold constants in one function.
 *  Each helper folds one instruction family and returns changed.
 * --------------------------------------------------------------- */

static int
fold_arith(IR_Instr* inst)
{
    IR_Value* v0 = inst->operands[0];
    IR_Value* v1 = inst->operands[1];
    if (!v0 || !v1) return 0;

    /* try identity first (returns existing operand, no alloc) */
    IR_Value* s = simplify(inst->opcode, v0, v1);
    int from_fold = 0;
    if (!s) { s = fold_binary(inst->opcode, v0, v1, inst->type);
              from_fold = 1; }
    if (!s) return 0;

    /* replace instruction result with folded constant */
    if (!inst->result) return 0;
    inst->result->kind = s->kind;
    inst->result->body = s->body;
    /* fold_binary allocates with calloc — must free.
     * simplify returns an existing operand — never free. */
    if (from_fold) free(s);
    return 1;
}

static int
fold_icmp(IR_Instr* inst)
{
    IR_Value *v0 = inst->operands[0], *v1 = inst->operands[1];
    if (!v0 || !v1) return 0;
    if (v0->kind != VAL_CONST_INT ||
        v1->kind != VAL_CONST_INT) return 0;

    /* normalize both operands to their type width: a u32
     * 0xFFFFFFFF may be stored raw as 4294967295 or -1 (same
     * bits).  EQ/NE and the signed conditions need it too. */
    int u64 = (v0->type && v0->type->kind == IR_I64);
    unsigned long long ua =
        (unsigned long long)v0->body.int_val;
    unsigned long long ub =
        (unsigned long long)v1->body.int_val;
    if (!u64) { ua = (unsigned int)ua; ub = (unsigned int)ub; }
    long long sa = u64 ? (long long)ua : (int)ua;
    long long sb = u64 ? (long long)ub : (int)ub;
    int r = 0;

    switch (inst->cond) {
    case IR_COND_EQ:  r = (ua == ub); break;
    case IR_COND_NE:  r = (ua != ub); break;
    case IR_COND_SGT: r = (sa > sb);  break;
    case IR_COND_SGE: r = (sa >= sb); break;
    case IR_COND_SLT: r = (sa < sb);  break;
    case IR_COND_SLE: r = (sa <= sb); break;
    case IR_COND_UGT: r = (ua > ub);  break;
    case IR_COND_UGE: r = (ua >= ub); break;
    case IR_COND_ULT: r = (ua < ub);  break;
    case IR_COND_ULE: r = (ua <= ub); break;
    default: break;
    }
    inst->result->kind = VAL_CONST_INT;
    inst->result->type = t_i1;
    inst->result->body.int_val = r;
    return 1;
}

static int
fold_select(IR_Instr* inst)
{
    IR_Value* cond = inst->operands[0];
    if (!cond || cond->kind != VAL_CONST_INT) return 0;

    IR_Value* pick = cond->body.int_val ?
                     inst->operands[1] : inst->operands[2];
    if (pick) {
        inst->result->body = pick->body;
        return 1;
    }
    return 0;
}

static int
fold_func(IR_Func* fn)
{
    int changed = 0;

    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_FOR_INST(inst, blk) {

            /* only fold binary/icmp instructions */
            switch (inst->opcode) {
            case IROP_ADD: case IROP_SUB: case IROP_MUL:
            case IROP_SDIV: case IROP_SREM:
            case IROP_AND: case IROP_OR: case IROP_XOR:
            case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
                changed |= fold_arith(inst);
                break;
            case IROP_ICMP:
                changed |= fold_icmp(inst);
                break;
            case IROP_SELECT:
                changed |= fold_select(inst);
                break;
            default: break;
            }
        }
    }
    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_const_fold(IR_Module* mod)
{
    int changed = 0;
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->blocks)
            changed |= fold_func(fn);
    return changed;
}
