/* ir_dump.c -- LLVM IR text dumper: value and condition printers.
 *
 * The type printer (dump_type) lives in ir_dump_type.c and the constant
 * printers (sign_extend_int / dump_const_float / dump_const_aggregate)
 * in ir_dump_const.c; this file holds the shared anonymous-struct name
 * table, the value printer and the icmp/fcmp condition printers.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ir_dump.h"

/* shared anonymous struct name table */
IR_Type* dump_anon_types[IR_MAX_ANON_TYPES];
int dump_anon_count = 0;

/* ---------------------------------------------------------------
 *  Value printer
 * --------------------------------------------------------------- */

void dump_value(FILE* out, IR_Value* val)
{
    if (!val) { fprintf(out, "void"); return; }

    switch (val->kind) {
    case VAL_CONST_INT:
        /* a zero integer constant of pointer type must dump as `null`
         * (LLVM rejects `ptr 0` in typed constants) */
        if (val->type && val->type->kind == IR_PTR &&
            val->body.int_val == 0)
            fprintf(out, "null");
        else
            fprintf(out, "%lld", sign_extend_int(val->body.int_val, val->type));
        break;

    case VAL_CONST_FLOAT:
        dump_const_float(out, val);
        break;

    case VAL_CONST_NULL:
        fprintf(out, "null");
        break;

    case VAL_CONST_STRING:
        {
            int idx = dump_str_index(val->body.str_val, val->is_wide);
            if (idx >= 0)
                fprintf(out, val->is_wide ? "@.wstr.%d" : "@.str.%d", idx);
            else
                fprintf(out, "null");
        }
        break;

    case VAL_PARAM:
        fprintf(out, "%%%d", val->id);
        break;

    case VAL_INSTR:
        fprintf(out, "%%%d", val->id);
        break;

    case VAL_GLOBAL:
        fprintf(out, "@%.*s", val->name.length, val->name.data);
        break;

    case VAL_GLOBAL_GEP:
        /* address constant with byte offset (&garr[1], &s.b, &g + 2):
         * getelementptr (i8, ptr @name, i64 off).  i8 element type makes
         * the index a pure byte offset — the size math already happened
         * in the ICE evaluator — and clang lowers it to a @name+off
         * relocation, matching gcc's address constant. */
        fprintf(out, "getelementptr (i8, ptr @%.*s, i64 %lld)",
                val->name.length, val->name.data, val->body.int_val);
        break;

    case VAL_UNDEF:
        fprintf(out, "undef");
        break;

    case VAL_CONST_AGGREGATE:
        dump_const_aggregate(out, val);
        break;

    case VAL_CONST_BITCAST:
        dump_const_cast(out, val, "bitcast");
        break;

    case VAL_CONST_INTTOPTR:
        dump_const_cast(out, val, "inttoptr");
        break;

    default: fprintf(out, "?"); break;
    }
}

/* ---------------------------------------------------------------
 *  Condition printer
 * --------------------------------------------------------------- */

const char* cond_str(IR_Cond cond)
{
    switch (cond) {
    case IR_COND_EQ:  return "eq";
    case IR_COND_NE:  return "ne";
    case IR_COND_UGT: return "ugt";
    case IR_COND_UGE: return "uge";
    case IR_COND_ULT: return "ult";
    case IR_COND_ULE: return "ule";
    case IR_COND_SGT: return "sgt";
    case IR_COND_SGE: return "sge";
    case IR_COND_SLT: return "slt";
    case IR_COND_SLE: return "sle";
    default: return "?";
    }
}

/* fcmp ordered predicates: strip 's'/'u' prefix from icmp codes.
 * fcmp uses "olt"/"ogt"/"ole"/"oge"/"oeq"/"one" etc.
 * We default to ordered (o) comparisons. */
const char* fcmp_cond_str(IR_Cond cond)
{
    switch (cond) {
    case IR_COND_EQ:  return "oeq";
    case IR_COND_NE:  return "one";
    case IR_COND_SGT: case IR_COND_UGT: return "ogt";
    case IR_COND_SGE: case IR_COND_UGE: return "oge";
    case IR_COND_SLT: case IR_COND_ULT: return "olt";
    case IR_COND_SLE: case IR_COND_ULE: return "ole";
    default: return "oeq";
    }
}
