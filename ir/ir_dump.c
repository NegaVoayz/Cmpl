/* ir_dump.c -- LLVM IR text dumper: type, value, condition printers */

#include "ir.h"

#include <stdio.h>

/* from ir_dump_str.c */
extern int  dump_str_index(String s);

/* ---------------------------------------------------------------
 *  Type printer
 * --------------------------------------------------------------- */

/* ---------------------------------------------------------------
 *  Type printer
 * --------------------------------------------------------------- */

void dump_type(FILE* out, IR_Type* ty)
{
    if (!ty) { fprintf(out, "void"); return; }

    switch (ty->kind) {
    case IR_VOID:  fprintf(out, "void"); break;
    case IR_I1:    fprintf(out, "i1"); break;
    case IR_I8:    fprintf(out, "i8"); break;
    case IR_I16:   fprintf(out, "i16"); break;
    case IR_I32:   fprintf(out, "i32"); break;
    case IR_I64:   fprintf(out, "i64"); break;
    case IR_F32:   fprintf(out, "float"); break;
    case IR_F64:   fprintf(out, "double"); break;

    case IR_PTR:
        if (ty->addrspace > 0)
            fprintf(out, "ptr addrspace(%d)", ty->addrspace);
        else
            fprintf(out, "ptr");
        break;

    case IR_ARRAY:
        fprintf(out, "[%d x ", ty->size);
        dump_type(out, ty->inner);
        fprintf(out, "]");
        break;

    case IR_FUNC:
        dump_type(out, ty->inner);
        fprintf(out, " (");

        for (IR_Type* p = ty->members; p; p = p->next) {
            if (p != ty->members) fprintf(out, ", ");
            dump_type(out, p);
        }
        fprintf(out, ")");
        break;

    case IR_STRUCT:
        if (ty->name.data)
            fprintf(out, "%%struct.%.*s", ty->name.length, ty->name.data);
        else
            fprintf(out, "{}");
        break;

    default: fprintf(out, "?"); break;
    }
}

/* ---------------------------------------------------------------
 *  Value printer
 * --------------------------------------------------------------- */

void dump_value(FILE* out, IR_Value* val)
{
    if (!val) { fprintf(out, "void"); return; }

    switch (val->kind) {
    case VAL_CONST_INT:
        fprintf(out, "%ld", val->body.int_val);
        break;

    case VAL_CONST_FLOAT:
        /* LLVM requires decimal point in float literals.
         * %g strips trailing zeros including the decimal for 0.0,
         * so handle 0.0 specially. */
        if (val->body.float_val == 0.0)
            fprintf(out, "0.0");
        else
            fprintf(out, "%g", val->body.float_val);
        break;

    case VAL_CONST_NULL:
        fprintf(out, "null");
        break;

    case VAL_CONST_STRING:
        {
            int idx = dump_str_index(val->body.str_val);
            if (idx >= 0)
                fprintf(out, "@.str.%d", idx);
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

    case VAL_UNDEF:
        fprintf(out, "undef");
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
