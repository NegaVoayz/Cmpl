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
    if (!lhs || !rhs) return NULL;
    if (lhs->kind != VAL_CONST_INT || rhs->kind != VAL_CONST_INT)
        return NULL;

    long a = lhs->body.int_val, b = rhs->body.int_val, r = 0;

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
    case IROP_LSHR: r = (unsigned long)a >> b; break;
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
    long lv = lc ? lhs->body.int_val : 0;
    long rv = rc ? rhs->body.int_val : 0;

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
 *  Fold constants in one function
 * --------------------------------------------------------------- */

static int
fold_func(IR_Func* fn)
{
    int changed = 0;

    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {

            /* only fold binary/icmp instructions */
            switch (inst->opcode) {
            case IROP_ADD: case IROP_SUB: case IROP_MUL:
            case IROP_SDIV: case IROP_SREM:
            case IROP_AND: case IROP_OR: case IROP_XOR:
            case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
            {
                IR_Value* v0 = inst->operands[0];
                IR_Value* v1 = inst->operands[1];

                /* try identity first */
                IR_Value* s = simplify(inst->opcode, v0, v1);
                if (!s) s = fold_binary(inst->opcode, v0, v1, inst->type);
                if (!s) break;

                /* replace instruction result with folded constant */
                inst->result->kind = s->kind;
                inst->result->body = s->body;
                free(s);
                changed = 1;
                break;
            }
            case IROP_ICMP:
            {
                IR_Value *v0 = inst->operands[0], *v1 = inst->operands[1];
                if (!v0 || !v1) break;
                if (v0->kind != VAL_CONST_INT ||
                    v1->kind != VAL_CONST_INT) break;

                long a = v0->body.int_val, b = v1->body.int_val;
                int r = 0;

                switch (inst->cond) {
                case IR_COND_EQ:  r = (a == b); break;
                case IR_COND_NE:  r = (a != b); break;
                case IR_COND_SGT: r = (a > b);  break;
                case IR_COND_SGE: r = (a >= b); break;
                case IR_COND_SLT: r = (a < b);  break;
                case IR_COND_SLE: r = (a <= b); break;
                default: break;
                }
                inst->result->kind = VAL_CONST_INT;
                inst->result->type = t_i1;
                inst->result->body.int_val = r;
                changed = 1;
                break;
            }
            case IROP_SELECT:
            {
                IR_Value* cond = inst->operands[0];
                if (!cond || cond->kind != VAL_CONST_INT) break;

                IR_Value* pick = cond->body.int_val ?
                                 inst->operands[1] : inst->operands[2];
                if (pick) {
                    inst->result->kind = pick->kind;
                    inst->result->body = pick->body;
                    changed = 1;
                }
                break;
            }
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
