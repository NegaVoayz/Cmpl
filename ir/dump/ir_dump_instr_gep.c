/* ir_dump_instr_gep.c -- getelementptr instruction printer.  Split out of
 * ir_dump_instr.c because the multi-index / struct-vs-array layout logic is
 * large enough to keep its own file. */

#include "ir.h"

#include <stdio.h>

#include "ir_dump.h"

void
dump_gep(FILE* out, IR_Instr* inst)
{
    /* the GEP base type is what the pointer points to.
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
}
