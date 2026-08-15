/* ir_dump_instr.c -- LLVM IR instruction printer dispatcher.  The per-opcode
 * family printers live in ir_dump_instr_extra.c (call/terminator/bitcast/
 * cast/select/phi) and ir_dump_instr_gep.c (getelementptr). */

#include "ir.h"

#include <stdio.h>

#include "../ir_dump.h"

/* ---------------------------------------------------------------
 *  Family printers (called by dump_instr below)
 * --------------------------------------------------------------- */

/* result-vreg prefix: emits "  %%%d = " for value-producing instructions
 * and nothing (or just the indent) for terminators / void calls. */
static void
print_result(FILE* out, IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_STORE: case IROP_RET: case IROP_BR: case IROP_COND_BR:
    case IROP_UNREACHABLE:
        return;
    case IROP_CALL:
        if (inst->type && inst->type->kind == IR_VOID) {
            fprintf(out, "  ");
            return;
        }
        break;
    default:
        break;
    }
    fprintf(out, "  %%%d = ", inst->result->id);
}

static void
dump_memory_op(FILE* out, IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_ALLOCA:
        fprintf(out, "alloca ");
        dump_type(out, inst->type->inner ? inst->type->inner : inst->type);
        break;

    case IROP_LOAD:
        fprintf(out, "load ");
        dump_type(out, inst->type);
        fprintf(out, ", ptr ");
        dump_value(out, inst->operands[0]);
        break;

    case IROP_STORE:
        fprintf(out, "  store ");
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ptr ");
        dump_value(out, inst->operands[1]);
        break;

    default:
        break;
    }
}

/* generic two-operand arithmetic/bitwise printer. `names` must be indexed
 * by (opcode - base); the opcodes grouped in each dispatch case are
 * required to be contiguous starting at `base`. */
static void
dump_binop(FILE* out, IR_Instr* inst, const char** names, IR_Opcode base)
{
    int idx = inst->opcode - base;
    fprintf(out, "%s ", names[idx]);
    dump_type(out, inst->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, ", ");
    dump_value(out, inst->operands[1]);
}

static void
dump_cmp(FILE* out, IR_Instr* inst, const char* mnemonic, const char* cond)
{
    fprintf(out, "%s %s ", mnemonic, cond);
    dump_type(out, inst->operands[0]->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, ", ");
    dump_value(out, inst->operands[1]);
}

/* ---------------------------------------------------------------
 *  Instruction printer -- opcode dispatcher
 * --------------------------------------------------------------- */

void dump_instr(FILE* out, IR_Instr* inst)
{
    if (!inst) return;

    print_result(out, inst);

    switch (inst->opcode) {
    case IROP_ALLOCA: case IROP_LOAD: case IROP_STORE:
        dump_memory_op(out, inst);
        break;

    case IROP_ADD: case IROP_SUB: case IROP_MUL:
    case IROP_SDIV: case IROP_SREM:
    case IROP_UDIV: case IROP_UREM:
    {   static const char* names[] = {"add","sub","mul","sdiv","srem","udiv","urem"};
        dump_binop(out, inst, names, IROP_ADD);
        break; }

    case IROP_FADD: case IROP_FSUB: case IROP_FMUL: case IROP_FDIV:
    {   static const char* names[] = {"fadd","fsub","fmul","fdiv"};
        dump_binop(out, inst, names, IROP_FADD);
        break; }

    case IROP_AND: case IROP_OR: case IROP_XOR:
    case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
    {   static const char* names[] = {"shl","lshr","ashr","and","or","xor"};
        dump_binop(out, inst, names, IROP_SHL);
        break; }

    case IROP_ICMP:
        dump_cmp(out, inst, "icmp", cond_str(inst->cond));
        break;

    case IROP_FCMP:
        dump_cmp(out, inst, "fcmp", fcmp_cond_str(inst->cond));
        break;

    case IROP_CALL:
        dump_call(out, inst);
        break;

    case IROP_RET: case IROP_BR: case IROP_COND_BR: case IROP_UNREACHABLE:
        dump_term_op(out, inst);
        break;

    case IROP_GEP:
        dump_gep(out, inst);
        break;

    case IROP_BITCAST:
        dump_bitcast(out, inst);
        break;

    case IROP_TRUNC:  dump_cast(out, inst, "trunc");  break;
    case IROP_ZEXT:   dump_cast(out, inst, "zext");   break;
    case IROP_SEXT:   dump_cast(out, inst, "sext");   break;
    case IROP_SITOFP: dump_cast(out, inst, "sitofp"); break;
    case IROP_UITOFP: dump_cast(out, inst, "uitofp"); break;
    case IROP_FPTOSI: dump_cast(out, inst, "fptosi"); break;
    case IROP_FPTOUI: dump_cast(out, inst, "fptoui"); break;

    case IROP_SELECT:
        dump_select(out, inst);
        break;

    case IROP_PHI:
        dump_phi(out, inst);
        break;

    default:
        fprintf(out, "  ; unknown opcode %d", inst->opcode);
        break;
    }
    fprintf(out, "\n");
}
