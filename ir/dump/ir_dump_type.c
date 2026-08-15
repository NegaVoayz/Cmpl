/* ir_dump_type.c -- LLVM IR type printer (dump_type).
 *
 * Split out of ir_dump.c so that file stays under the line limit.
 * dump_type is the recursive IR_Type -> textual form printer; it
 * references the shared anonymous-struct name table defined in
 * ir_dump.c via the externs in ir_dump.h.
 */

#include "ir.h"

#include <stdio.h>

#include "ir_dump.h"

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
