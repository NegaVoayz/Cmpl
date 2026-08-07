/* ir_dump_instr.c -- LLVM IR instruction printer */

#include "ir.h"

#include <stdio.h>

/* from ir_dump.c */
extern void        dump_type(FILE* out, IR_Type* ty);
extern void        dump_value(FILE* out, IR_Value* val);
extern const char* cond_str(IR_Cond cond);

/* ---------------------------------------------------------------
 *  Instruction printer
 * --------------------------------------------------------------- */

void dump_instr(FILE* out, IR_Instr* inst)
{
    if (!inst) return;

    /* print result vreg for instructions that produce values */
    switch (inst->opcode) {
    case IROP_STORE: case IROP_RET: case IROP_BR: case IROP_COND_BR:
    case IROP_UNREACHABLE:
        break;
    default:
        fprintf(out, "  %%%d = ", inst->result->id);
        break;
    }

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

    case IROP_ADD: case IROP_SUB: case IROP_MUL:
    case IROP_SDIV: case IROP_SREM:
    {
        const char* op_names[] = {"add","sub","mul","sdiv","srem"};
        int idx = inst->opcode - IROP_ADD;
        fprintf(out, "%s ", op_names[idx]);
        dump_type(out, inst->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_FADD: case IROP_FSUB: case IROP_FMUL: case IROP_FDIV:
    {
        const char* op_names[] = {"fadd","fsub","fmul","fdiv"};
        int idx = inst->opcode - IROP_FADD;
        fprintf(out, "%s ", op_names[idx]);
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_AND: case IROP_OR: case IROP_XOR:
    case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
    {
        const char* op_names[] = {"and","or","xor","shl","lshr","ashr"};
        int idx = inst->opcode - IROP_AND;
        fprintf(out, "%s ", op_names[idx]);
        dump_type(out, inst->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_ICMP:
        fprintf(out, "icmp %s ", cond_str(inst->cond));
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;

    case IROP_CALL:
        fprintf(out, "call ");
        dump_type(out, inst->type);
        fprintf(out, " @%.*s(", inst->callee.length, inst->callee.data);

        for (int i = 0; i < inst->n_call_args; i++) {
            if (i > 0) fprintf(out, ", ");
            dump_type(out, inst->call_args[i]->type);
            fprintf(out, " ");
            dump_value(out, inst->call_args[i]);
        }
        fprintf(out, ")");
        break;

    case IROP_RET:
        fprintf(out, "  ret ");

        if (inst->operands[0]) {
            dump_type(out, inst->operands[0]->type);
            fprintf(out, " ");
            dump_value(out, inst->operands[0]);
        } else {
            fprintf(out, "void");
        }
        break;

    case IROP_BR:
        fprintf(out, "  br label %%%s",
                inst->in_blocks[0]->name.data);
        break;

    case IROP_COND_BR:
        fprintf(out, "  br i1 ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", label %%%s, label %%%s",
                inst->in_blocks[0]->name.data,
                inst->in_blocks[1]->name.data);
        break;

    case IROP_GEP:
        fprintf(out, "getelementptr ");
        dump_type(out, inst->operands[0]->type->inner);
        fprintf(out, ", ptr ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");

        if (inst->operands[1])
            dump_value(out, inst->operands[1]);

        if (inst->operands[2])
            fprintf(out, ", %%%d", inst->operands[2]->id);
        break;

    case IROP_BITCAST:
        fprintf(out, "bitcast ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_TRUNC:
        fprintf(out, "trunc ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_ZEXT:
        fprintf(out, "zext ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_SEXT:
        fprintf(out, "sext ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_SELECT:
        fprintf(out, "select ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[2]);
        break;

    case IROP_UNREACHABLE:
        fprintf(out, "  unreachable");
        break;

    default:
        fprintf(out, "  ; unknown opcode %d", inst->opcode);
        break;
    }
    fprintf(out, "\n");
}
