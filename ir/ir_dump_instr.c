/* ir_dump_instr.c -- LLVM IR instruction printer */

#include "ir.h"

#include <stdio.h>

/* from ir_dump.c */
extern void        dump_type(FILE* out, IR_Type* ty);
extern void        dump_value(FILE* out, IR_Value* val);
extern const char* cond_str(IR_Cond cond);
extern const char* fcmp_cond_str(IR_Cond cond);

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
    case IROP_CALL:
        if (inst->type && inst->type->kind == IR_VOID) {
            fprintf(out, "  ");
            break;
        }
        fprintf(out, "  %%%d = ", inst->result->id);
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
        dump_type(out, inst->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;
    }

    case IROP_AND: case IROP_OR: case IROP_XOR:
    case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
    {
        const char* op_names[] = {"shl","lshr","ashr","and","or","xor"};
        int idx = inst->opcode - IROP_SHL;
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

    case IROP_FCMP:
        fprintf(out, "fcmp %s ", fcmp_cond_str(inst->cond));
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, ", ");
        dump_value(out, inst->operands[1]);
        break;

    case IROP_CALL:
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
    {   /* the GEP base type is what the pointer points to.
         * for a global array, operands[0]->type is IR_ARRAY (not ptr),
         * so use the full array type as the base (2D indexing needs
         * the outer dimension). for a ptr, use ->inner (the pointee). */
        IR_Type* base = inst->operands[0] ? inst->operands[0]->type : NULL;
        IR_Type* elem = base;
        if (base && base->kind == IR_PTR)
            elem = base->inner;

        if (!elem || elem->kind == IR_VOID) elem = t_i8;

        fprintf(out, "getelementptr ");
        dump_type(out, elem);
        { IR_Type* base_ty = inst->operands[0] ? inst->operands[0]->type : NULL;
          if (base_ty && base_ty->kind == IR_PTR && base_ty->addrspace > 0)
              fprintf(out, ", ptr addrspace(%d) ", base_ty->addrspace);
          else
              fprintf(out, ", ptr "); }
        dump_value(out, inst->operands[0]);

        int idx0_is_zero = (inst->operands[1] &&
                            inst->operands[1]->kind == VAL_CONST_INT &&
                            inst->operands[1]->body.int_val == 0);
        int idx1_is_variable = (inst->operands[2] &&
                                inst->operands[2]->kind != VAL_CONST_INT);

        /* For struct type with pattern [0, variable]: treat as array
         * access and emit only the variable index. LLVM requires
         * struct field indices to be constant. */
        if (idx0_is_zero && idx1_is_variable &&
            elem && (elem->kind == IR_STRUCT || elem->kind == IR_UNION)) {
            fprintf(out, ", ");
            dump_type(out, inst->operands[2]->type);
            fprintf(out, " ");
            dump_value(out, inst->operands[2]);
        } else if (idx0_is_zero && inst->operands[2] &&
                   (!elem || (elem->kind != IR_ARRAY && elem->kind != IR_STRUCT && elem->kind != IR_UNION))) {
            /* scalar: skip zero idx0, emit idx1 directly */
            fprintf(out, ", ");
            dump_type(out, inst->operands[2]->type);
            fprintf(out, " ");
            dump_value(out, inst->operands[2]);
        } else {
            fprintf(out, ", ");
            dump_type(out, inst->operands[1]->type);
            fprintf(out, " ");
            dump_value(out, inst->operands[1]);

            if (inst->operands[2]) {
                fprintf(out, ", ");
                dump_type(out, inst->operands[2]->type);
                fprintf(out, " ");
                dump_value(out, inst->operands[2]);
            }
        }
        break;
    }

    case IROP_BITCAST:
    {   IR_Type* src = inst->operands[0]->type;
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
        break;
    }

    case IROP_TRUNC:
        fprintf(out, "trunc ");
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_ZEXT:
        fprintf(out, "zext ");
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_SEXT:
        fprintf(out, "sext ");
        dump_type(out, inst->operands[0]->type);
        fprintf(out, " ");
        dump_value(out, inst->operands[0]);
        fprintf(out, " to ");
        dump_type(out, inst->type);
        break;

    case IROP_SELECT:
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
        break;

    case IROP_PHI:
        fprintf(out, "phi ");
        dump_type(out, inst->type);
        for (int i = 0; i < inst->n_incoming; i++) {
            fprintf(out, " [ ");
            dump_value(out, inst->in_vals[i]);
            fprintf(out, ", %%%s ]", inst->in_blocks[i] ?
                   inst->in_blocks[i]->name.data : "???");
        }
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
