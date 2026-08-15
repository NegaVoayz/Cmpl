/* ir_dump_instr_extra.c -- per-opcode IR instruction family printers: call,
 * terminator, bitcast, cast, select, and phi.  Dispatched from dump_instr
 * in ir_dump_instr.c; the getelementptr printer lives in ir_dump_instr_gep.c. */

#include "ir.h"

#include <stdio.h>

#include "ir_dump.h"

void
dump_call(FILE* out, IR_Instr* inst)
{
    fprintf(out, "call ");
    dump_type(out, inst->type);
    if (inst->callee.length > 0)
        fprintf(out, " @%.*s(", inst->callee.length, inst->callee.data);
    else {
        /* indirect call through function pointer */
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, "(");
    }

    for (int i = 0; i < inst->n_call_args; i++) {
        if (i > 0) fprintf(out, ", ");
        dump_type(out, inst->call_args[i]->type);
        fprintf(out, " ");
        dump_value(out, inst->call_args[i]);
    }
    fprintf(out, ")");
}

void
dump_term_op(FILE* out, IR_Instr* inst)
{
    switch (inst->opcode) {
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

    case IROP_UNREACHABLE:
        fprintf(out, "  unreachable");
        break;

    default:
        break;
    }
}

void
dump_bitcast(FILE* out, IR_Instr* inst)
{
    IR_Type* src = inst->operands[0]->type;
    IR_Type* dst = inst->type;
    int src_ptr = src && src->kind == IR_PTR;
    int dst_ptr = dst && dst->kind == IR_PTR;
    int src_fp = src && (src->kind == IR_F32 || src->kind == IR_F64);
    int dst_fp = dst && (dst->kind == IR_F32 || dst->kind == IR_F64);

    if (src_ptr && !dst_ptr)
        fprintf(out, "ptrtoint ");
    else if (!src_ptr && dst_ptr)
        fprintf(out, "inttoptr ");
    else if (src_fp && dst_fp &&
             ir_type_size(src) < ir_type_size(dst))
        fprintf(out, "fpext ");
    else if (src_fp && dst_fp &&
             ir_type_size(src) > ir_type_size(dst))
        fprintf(out, "fptrunc ");
    else if (src && dst && !src_ptr && !dst_ptr &&
             ir_type_size(src) < ir_type_size(dst))
        fprintf(out, "zext ");
    else if (src && dst && !src_ptr && !dst_ptr &&
             ir_type_size(src) > ir_type_size(dst))
        fprintf(out, "trunc ");
    else
        fprintf(out, "bitcast ");

    dump_type(out, src);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, " to ");
    dump_type(out, dst);
}

/* trunc/zext/sext/sitofp/uitofp/fptosi/fptoui all share this shape:
 *   <op> <src-ty> <val> to <dst-ty> */
void
dump_cast(FILE* out, IR_Instr* inst, const char* mnemonic)
{
    fprintf(out, "%s ", mnemonic);
    dump_type(out, inst->operands[0]->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[0]);
    fprintf(out, " to ");
    dump_type(out, inst->type);
}

void
dump_select(FILE* out, IR_Instr* inst)
{
    fprintf(out, "select i1 ");
    dump_value(out, inst->operands[0]);
    fprintf(out, ", ");
    dump_type(out, inst->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[1]);
    fprintf(out, ", ");
    dump_type(out, inst->type);
    fprintf(out, " ");
    dump_value(out, inst->operands[2]);
}

void
dump_phi(FILE* out, IR_Instr* inst)
{
    fprintf(out, "phi ");
    dump_type(out, inst->type);
    fprintf(out, " ");
    for (int i = 0; i < inst->n_incoming; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "[ ");
        dump_value(out, inst->in_vals[i]);
        fprintf(out, ", %%%s ]", inst->in_blocks[i] ?
               inst->in_blocks[i]->name.data : "???");
    }
}
