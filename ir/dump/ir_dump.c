/* ir_dump.c -- LLVM IR text dumper: type, value, condition printers */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ir_dump.h"

/* shared anonymous struct name table */
IR_Type* dump_anon_types[IR_MAX_ANON_TYPES];
int dump_anon_count = 0;

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
    case IR_UNION:
        if (ty->name.data) {
            fprintf(out, "%%struct.%.*s", ty->name.length, ty->name.data);
        } else if (ty->members) {
            /* Look up in dump_anon_types by pointer identity first,
             * then by members pointer (clones from clone_type_for_chain
             * share the members pointer but have a different IR_Type*).
             * Emit the ORIGINAL's name so clones resolve to the same
             * type. */
            int idx = -1;
            IR_Type* orig = ty;
            for (int i = 0; i < dump_anon_count; i++) {
                if (dump_anon_types[i] == ty) { idx = i; orig = ty; break; }
            }
            if (idx < 0) {
                for (int i = 0; i < dump_anon_count; i++) {
                    if (dump_anon_types[i]->members == ty->members) {
                        idx = i; orig = dump_anon_types[i]; break;
                    }
                }
            }
            if (idx >= 0)
                fprintf(out, "%%struct.anon.%d.p%p", idx, (void*)orig);
            else
                fprintf(out, "{}");
        } else
            fprintf(out, "{}");
        break;

    default: fprintf(out, "?"); break;
    }
}

/* ---------------------------------------------------------------
 *  Value printer
 * --------------------------------------------------------------- */

static void
dump_const_float(FILE* out, IR_Value* val)
{
    /* LLVM requires a decimal point or exponent in float literals.
     * %g prints integral values like 7.0 as "7", which clang
     * rejects as an integer constant — append ".0" when needed.
     * %g also prints 4e+09 for large values — clang's IR reader
     * rejects an exponent without a decimal point ("4e+09" is
     * lexed as an integer), so insert ".0" before the exponent. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", val->body.float_val);
    char* e = strpbrk(buf, "eE");
    if (e && !strchr(buf, '.')) {
        size_t n = (size_t)(e - buf);
        memmove(e + 2, e, strlen(e) + 1);
        buf[n] = '.';
        buf[n + 1] = '0';
    }
    fprintf(out, "%s", buf);
    if (!strchr(buf, '.') && !strchr(buf, 'e') &&
        !strchr(buf, 'E') && !strchr(buf, 'i') &&
        !strchr(buf, 'n'))
        fprintf(out, ".0");
}

static void
dump_const_aggregate(FILE* out, IR_Value* val)
{
    /* emit values with per-element types (no outer type).
     * arrays: [T v0, T v1, ...]
     * structs: {T v0, T v1, ...}
     * the type is provided by the caller (global line or parent). */
    if (val->body.aggregate.count == 0) {
        fprintf(out, "zeroinitializer");
    } else {
        fprintf(out, val->type->kind == IR_STRUCT ||
                val->type->kind == IR_UNION ? "{" : "[");
        for (int i = 0; i < val->body.aggregate.count; i++) {
            if (i > 0) fprintf(out, ", ");
            dump_type(out, val->body.aggregate.elems[i]->type);
            fprintf(out, " ");
            dump_value(out, val->body.aggregate.elems[i]);
        }
        fprintf(out, val->type->kind == IR_STRUCT ||
                val->type->kind == IR_UNION ? "}" : "]");
    }
}

void dump_value(FILE* out, IR_Value* val)
{
    if (!val) { fprintf(out, "void"); return; }

    switch (val->kind) {
    case VAL_CONST_INT:
        fprintf(out, "%lld", val->body.int_val);
        break;

    case VAL_CONST_FLOAT:
        dump_const_float(out, val);
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

    case VAL_CONST_AGGREGATE:
        dump_const_aggregate(out, val);
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
